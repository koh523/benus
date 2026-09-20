/**
 * @file blenus.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief BLE NUS device driver (mSDI). RX/TX buffers and the bridge to SoftDevice.
 * @date 2026-08-10
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/machine.h>
#include <tk/tkernel.h>
#include <tstdlib.h>

#include "blenus.h"
#include "../include/dev_def.h"
#if DEV_BLENUS_ENABLE

/*
 *	blenus.c
 *	BLE NUS device driver
 *	Low-level (SoftDevice) part: sysdepend/nrf5/blenus_nrf5.c
 */

/*----------------------------------------------------------------------*/
/* Device driver Control block
 */
#if TK_SUPPORT_MEMLIB

LOCAL T_BLENUS_DCB	*dev_blenus_cb[DEV_BLENUS_UNITNM];
#define		get_dcb_ptr(unit)	(dev_blenus_cb[unit])
#define		get_dcb_mem(unit,a)	(dev_blenus_cb[unit]->a)

#else

LOCAL T_BLENUS_DCB	dev_blenus_cb[DEV_BLENUS_UNITNM];
#define		get_dcb_ptr(unit)	(&dev_blenus_cb[unit])
#define		get_dcb_mem(unit,a)	(dev_blenus_cb[unit].a)

#endif

/*
 * SoftDevice SVC and interrupt return assume an 8-word (basic) exception frame.
 * If FPCA=1 in the window where SWI2 / TIMER interrupts right after EI, INVSTATE occurs.
 * Clear it just before resuming interrupts as well, in addition to SVCALL in nrf_svc.h.
 */
LOCAL void blenus_fpca_clear(void)
{
	UW	control;

	__asm volatile ("mrs %0, control" : "=r" (control));
	control &= ~(1u << 2);	/* CONTROL.FPCA */
	__asm volatile ("msr control, %0" :: "r" (control) : "memory");
	__asm volatile ("isb" ::: "memory");
}

/**
 * @brief Re-enable interrupts after DI(), clearing CONTROL.FPCA first.
 * @param imask Interrupt mask saved by the preceding DI()
 * @note This driver calls this function instead of EI(), so that FPCA is 0 when
 *       interrupts resume (see blenus_fpca_clear).
 */
LOCAL void blenus_ei(UINT imask)
{
	blenus_fpca_clear();
	EI(imask);
}

LOCAL BOOL	drv_ready = FALSE;

/**
 * @brief Get the control block of a unit.
 * @param unit Unit number
 * @return Pointer to the control block, or NULL if the driver is not initialized yet
 *         (drv_ready is FALSE) or unit is out of range
 * @note Also used from SoftDevice event context, so callers must check for NULL.
 */
LOCAL T_BLENUS_DCB *get_dcb(UW unit)
{
	if (!drv_ready || unit >= DEV_BLENUS_UNITNM) {
		return NULL;
	}
#if TK_SUPPORT_MEMLIB
	return dev_blenus_cb[unit];
#else
	return &dev_blenus_cb[unit];
#endif
}

/**
 * @brief Wake up waiting tasks.
 * @details Wakes the receive-waiting task with tk_wup_tsk if rcv_buff has data, and the
 *          send-waiting task if tx_busy is cleared.
 *          Intended to be called from SWI0 (APP_LOW) context, not from SWI2 / SysTick.
 * @note Calling it is optional. Even if it is not called, waiting tasks work by rechecking
 *       the state on each tk_slp_tsk timeout (see blenus_ll_wake_irq_start in blenus_nrf5.h).
 */
EXPORT void blenus_wake_waiters(void)
{
	UW		unit;
	T_BLENUS_DCB	*p_dcb;
	UINT		imask;
	ID		tid;

	for (unit = 0; unit < DEV_BLENUS_UNITNM; unit++) {
		p_dcb = get_dcb(unit);
		if (p_dcb == NULL) {
			continue;
		}

		tid = 0;
		DI(imask);
		if (p_dcb->rcv_buff.wait_tskid != 0 &&
		    p_dcb->rcv_buff.top != p_dcb->rcv_buff.tail) {
			tid = p_dcb->rcv_buff.wait_tskid;
			p_dcb->rcv_buff.wait_tskid = 0;
		}
		blenus_ei(imask);
		if (tid != 0) {
			(void)tk_wup_tsk(tid);
		}

		tid = 0;
		DI(imask);
		if (p_dcb->snd_buff.wait_tskid != 0 && !p_dcb->tx_busy) {
			tid = p_dcb->snd_buff.wait_tskid;
			p_dcb->snd_buff.wait_tskid = 0;
		}
		blenus_ei(imask);
		if (tid != 0) {
			(void)tk_wup_tsk(tid);
		}
	}
}

/*----------------------------------------------------------------------*/
/* Send buffer helpers
 */

/**
 * @brief Return the number of bytes queued in a ring buffer.
 * @param p_buff Ring buffer
 * @return Number of bytes between tail and top
 * @note top and tail are read with interrupts disabled.
 */
LOCAL UW buff_used(T_BLENUS_BUFF *p_buff)
{
	INT	used;
	UINT	imask;

	DI(imask);
	used = (INT)p_buff->top - (INT)p_buff->tail;
	blenus_ei(imask);
	if (used < 0) {
		used += DEVCONF_BLENUS_BUFFSIZE;
	}
	return (UW)used;
}

/**
 * @brief Discard all data queued in a ring buffer (set tail to top).
 * @param p_buff Ring buffer
 * @note Runs with interrupts disabled. write_data uses it to drop a partially
 *       queued message on error.
 */
LOCAL void buff_flush(T_BLENUS_BUFF *p_buff)
{
	UINT	imask;

	DI(imask);
	p_buff->tail = p_buff->top;
	blenus_ei(imask);
}

/*----------------------------------------------------------------------*/
/* TX kick: drain snd_buff until empty or SoftDevice returns busy.
 *
 * MUST be called from task context (not SoftDevice BLE event), because
 * ble_nus_data_send() must not race with the BLE observer.
 *
 * Returns E_BUSY when the SoftDevice notification queue is full. The caller
 * waits for BLE_NUS_EVT_TX_RDY and calls again; the unsent bytes stay queued.
 */
LOCAL ER kick_tx(T_BLENUS_DCB *p_dcb)
{
	T_BLENUS_BUFF	*p_buff;
	UB		chunk[DEVCNF_BLENUS_MAXCHUNK];
	UINT		len;
	UINT		sent;
	UINT		imask;
	UINT		maxchunk;
	UW		tail;
	ID		tid;
	ER		err;

	if (p_dcb == NULL) {
		return E_PAR;
	}
	if (!p_dcb->connected) {
		return E_OBJ;
	}

	/* comm_started is not tested here: ble_nus_data_send() reports
	 * NRF_ERROR_INVALID_STATE (-> E_OBJ) while the peer has not subscribed,
	 * so a missed BLE_NUS_EVT_COMM_STARTED cannot wedge the transmitter.
	 */
	p_buff = &p_dcb->snd_buff;
	maxchunk = blenus_ll_max_payload();
	if (maxchunk == 0 || maxchunk > DEVCNF_BLENUS_MAXCHUNK) {
		maxchunk = DEVCNF_BLENUS_MAXCHUNK;
	}

	for (;;) {
		len = 0;
		DI(imask);
		tail = p_buff->tail;
		while (len < maxchunk && p_buff->top != tail) {
			chunk[len++] = p_buff->data[tail];
			if (++tail >= DEVCONF_BLENUS_BUFFSIZE) {
				tail = 0;
			}
		}
		blenus_ei(imask);

		if (len == 0) {
			return E_OK;
		}

		/* Arm the TX_RDY flag before sending, so a completion arriving
		 * while we are inside SoftDevice cannot be missed by wait_tx_rdy().
		 */
		DI(imask);
		p_dcb->tx_busy = TRUE;
		blenus_ei(imask);

		sent = len;
		err = blenus_ll_send(chunk, &sent);
		if (err != E_OK) {
			if (err != E_BUSY) {
				DI(imask);
				p_dcb->tx_busy = FALSE;
				blenus_ei(imask);
			}
			return err;
		}

		DI(imask);
		p_buff->tail = (p_buff->tail + sent) % DEVCONF_BLENUS_BUFFSIZE;
		tid = p_buff->wait_tskid;
		p_buff->wait_tskid = 0;
		blenus_ei(imask);
		if (tid != 0) {
			(void)tk_wup_tsk(tid);
		}
	}
}

/*----------------------------------------------------------------------*/
/* Wait until SoftDevice accepts notifications again (task context only).
 */
LOCAL ER wait_tx_rdy(T_BLENUS_DCB *p_dcb)
{
	UINT	imask;
	ER	err;

	DI(imask);
	if (!p_dcb->tx_busy) {
		blenus_ei(imask);
		return E_OK;
	}
	p_dcb->snd_buff.wait_tskid = tk_get_tid();
	blenus_ei(imask);

	/* We are not woken by TX_RDY, so with TMO_FEVR check at short intervals, as the
	 * receive wait does, and return to the upper retry loop (TXRDY_RETRY times).
	 * The unit of the wait time is ms. Even if 5 ms is specified, the timer period
	 * (CNF_TIMER_PERIOD = 10 ms) is added and the actual wait is about 20 ms. */
	err = tk_slp_tsk((p_dcb->snd_tmo == TMO_FEVR) ? 5 : p_dcb->snd_tmo);

	DI(imask);
	p_dcb->snd_buff.wait_tskid = 0;
	blenus_ei(imask);

	return err;
}

/* In SoftDevice context only clear tx_busy. Waking waiters is done from SWI0. */
LOCAL void wake_tx_waiter(T_BLENUS_DCB *p_dcb)
{
	if (p_dcb == NULL) {
		return;
	}
	p_dcb->tx_busy = FALSE;
	/* Do not call tk_wup from an IRQ. The TX wait is checked by the timeout of wait_tx_rdy. */
}

/*----------------------------------------------------------------------*/
/* Empty snd_buff, waiting for TX_RDY whenever SoftDevice reports busy.
 * Task context only.
 */
LOCAL ER drain_snd_buff(T_BLENUS_DCB *p_dcb)
{
	ER	err;
	INT	retry;

	for (retry = 0; ; retry++) {
		err = kick_tx(p_dcb);
		if (err != E_BUSY) {
			break;
		}
		if (retry >= DEVCNF_BLENUS_TXRDY_RETRY) {
			p_dcb->com_error |= DEV_BLENUS_ERR_TOVR;
			err = E_TMOUT;
			break;
		}
		err = wait_tx_rdy(p_dcb);
		if (err != E_OK && err != E_TMOUT) {
			break;
		}
		err = E_BUSY;
	}

	return err;
}

/*----------------------------------------------------------------------*/
/* SoftDevice / NUS event bridge
 */
/**
 * @brief Add a communication error flag to com_error.
 * @param unit Unit number
 * @param err  DEV_BLENUS_ERR_* bit
 * @note Not protected by disabling interrupts. It is cleared by read_atr (TDN_BLENUS_COMERR).
 */
EXPORT void dev_blenus_notify_err(UW unit, UW err)
{
	T_BLENUS_DCB	*p_dcb = get_dcb(unit);

	if (p_dcb == NULL) {
		return;
	}
	p_dcb->com_error |= err;
}

/**
 * @brief Put one received byte into rcv_buff.
 * @param unit Unit number
 * @param data Received byte
 * @details Called from SoftDevice observer (SWI2) context (nus_data_handler).
 *          It does not call tk_wup_tsk; the waiting task notices the data by polling in read_data.
 *          Only this function writes (top) and only read_data reads (tail), so the ring is
 *          updated without disabling interrupts.
 * @note When the ring is full, the byte is discarded and DEV_BLENUS_ERR_ROVR is set.
 */
EXPORT void dev_blenus_notify_rcv(UW unit, UW data)
{
	T_BLENUS_DCB	*p_dcb;
	T_BLENUS_BUFF	*p_buff;
	INT		next;

	p_dcb = get_dcb(unit);
	if (p_dcb == NULL) {
		return;
	}

	p_buff = &p_dcb->rcv_buff;
	next = (INT)p_buff->top + 1;
	if (next >= DEVCONF_BLENUS_BUFFSIZE) {
		next = 0;
	}
	if (next != (INT)p_buff->tail) {
		p_buff->data[p_buff->top] = (UB)data;
		p_buff->top = (UW)next;
	} else {
		p_dcb->com_error |= DEV_BLENUS_ERR_ROVR;
	}
}

/**
 * @brief Reflect the SoftDevice TX_RDY event (the notification queue has room again).
 * @param unit Unit number
 * @details Called from SoftDevice observer context. It only clears tx_busy; the waiting
 *          task notices it by polling in wait_tx_rdy. Sending is done from task context
 *          in write_data.
 */
EXPORT void dev_blenus_notify_tx_rdy(UW unit)
{
	/* SoftDevice event context: wake only. Send from task via write_data. */
	wake_tx_waiter(get_dcb(unit));
}

/**
 * @brief Reflect a GAP connect / disconnect.
 * @param unit      Unit number
 * @param connected TRUE when connected, FALSE when disconnected
 * @details Called from SoftDevice observer context.
 *          On disconnect, comm_started and tx_busy are also cleared and the TX wait is released.
 */
EXPORT void dev_blenus_notify_conn(UW unit, BOOL connected)
{
	T_BLENUS_DCB	*p_dcb = get_dcb(unit);
	UINT		imask;

	if (p_dcb == NULL) {
		return;
	}

	DI(imask);
	p_dcb->connected = connected;
	if (!connected) {
		p_dcb->comm_started = FALSE;
		p_dcb->tx_busy = FALSE;
	}
	blenus_ei(imask);

	if (!connected) {
		wake_tx_waiter(p_dcb);
	}
}

/**
 * @brief Reflect NUS Notify enable / disable (write to the CCCD).
 * @param unit    Unit number
 * @param started TRUE when enabled, FALSE when disabled
 * @details Called from SoftDevice observer context. When enabled, the TX wait is released.
 */
EXPORT void dev_blenus_notify_comm(UW unit, BOOL started)
{
	T_BLENUS_DCB	*p_dcb = get_dcb(unit);
	UINT		imask;

	if (p_dcb == NULL) {
		return;
	}

	DI(imask);
	p_dcb->comm_started = started;
	blenus_ei(imask);

	if (started) {
		wake_tx_waiter(p_dcb);
	}
}

/*----------------------------------------------------------------------*/
/* Attribute data control
 */

/**
 * @brief Read an attribute.
 * @param p_dcb Control block of the device driver
 * @param req   Request. req->start gives the attribute number (negative, TDN_*) and
 *              req->buf / req->size give the destination. req->asize returns the size of the attribute
 * @return E_OK / E_PAR (unknown attribute, or size is neither 0 nor large enough for the attribute)
 * @details The readable attributes are:
 *          - TDN_EVENT: ID of the MBF for event notification
 *          - TDN_BLENUS_CONN: connection state (BOOL)
 *          - TDN_BLENUS_COMM: whether Notify is enabled (BOOL)
 *          - TDN_BLENUS_COMERR: communication error flags (UW). Cleared when read
 *          - TDN_BLENUS_SNDTMO / TDN_BLENUS_RCVTMO: send / receive timeout (TMO, ms)
 * @note When req->size is 0, no value is read and the required size is returned in asize.
 */
LOCAL ER read_atr(T_BLENUS_DCB *p_dcb, T_DEVREQ *req)
{
	UINT	imask;
	ER	err = E_OK;

	switch (req->start) {
	case TDN_EVENT:
		if (req->size >= (SZ)sizeof(ID)) {
			*(ID *)req->buf = p_dcb->evtmbfid;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(ID);
		break;

	case TDN_BLENUS_CONN:
		if (req->size >= (SZ)sizeof(BOOL)) {
			*(BOOL *)req->buf = p_dcb->connected ? TRUE : FALSE;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(BOOL);
		break;

	case TDN_BLENUS_COMM:
		if (req->size >= (SZ)sizeof(BOOL)) {
			*(BOOL *)req->buf = p_dcb->comm_started ? TRUE : FALSE;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(BOOL);
		break;

	case TDN_BLENUS_COMERR:
		if (req->size >= (SZ)sizeof(UW)) {
			DI(imask);
			*(UW *)req->buf = p_dcb->com_error;
			p_dcb->com_error = 0;
			blenus_ei(imask);
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(UW);
		break;

	case TDN_BLENUS_SNDTMO:
		if (req->size >= (SZ)sizeof(TMO)) {
			*(TMO *)req->buf = p_dcb->snd_tmo;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(TMO);
		break;

	case TDN_BLENUS_RCVTMO:
		if (req->size >= (SZ)sizeof(TMO)) {
			*(TMO *)req->buf = p_dcb->rcv_tmo;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(TMO);
		break;

	default:
		err = E_PAR;
		break;
	}

	return err;
}

/**
 * @brief Write an attribute.
 * @param p_dcb Control block of the device driver
 * @param req   Request. req->start gives the attribute number (negative, TDN_*) and
 *              req->buf / req->size give the value to write. req->asize returns the size of the attribute
 * @return E_OK / E_PAR (unknown or read-only attribute, or size is neither 0 nor large
 *         enough for the attribute)
 * @details The writable attributes are TDN_EVENT, TDN_BLENUS_SNDTMO and TDN_BLENUS_RCVTMO.
 *          TDN_BLENUS_CONN / TDN_BLENUS_COMM / TDN_BLENUS_COMERR are read-only.
 * @note When req->size is 0, no value is written and the required size is returned in asize.
 */
LOCAL ER write_atr(T_BLENUS_DCB *p_dcb, T_DEVREQ *req)
{
	ER	err = E_OK;

	switch (req->start) {
	case TDN_EVENT:
		if (req->size >= (SZ)sizeof(ID)) {
			p_dcb->evtmbfid = *(ID *)req->buf;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(ID);
		break;

	case TDN_BLENUS_SNDTMO:
		if (req->size >= (SZ)sizeof(TMO)) {
			p_dcb->snd_tmo = *(TMO *)req->buf;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(TMO);
		break;

	case TDN_BLENUS_RCVTMO:
		if (req->size >= (SZ)sizeof(TMO)) {
			p_dcb->rcv_tmo = *(TMO *)req->buf;
		} else if (req->size != 0) {
			err = E_PAR;
			break;
		}
		req->asize = sizeof(TMO);
		break;

	default:
		err = E_PAR;
		break;
	}

	return err;
}

/*----------------------------------------------------------------------*/
/* Device-specific data control (buffer path)
 */

/**
 * @brief Read req->size bytes from rcv_buff. Task context only.
 * @param p_dcb Control block of the device driver
 * @param req   Request. req->buf / req->size give the destination.
 *              req->asize returns the number of bytes read
 * @return E_OK / E_TMOUT (size bytes did not arrive within rcv_tmo) /
 *         other errors of tk_slp_tsk
 * @details When there is not enough data, wait by polling with tk_slp_tsk. Each wait specifies 5 ms
 *          (the timer period CNF_TIMER_PERIOD is added, so it takes about 20 ms in practice).
 *          SoftDevice never wakes the task (see the event bridge in blenus.h for the reason).
 *          - rcv_tmo == TMO_FEVR: keep waiting until the requested number of bytes has arrived.
 *          - otherwise: return E_TMOUT if size bytes have not arrived within rcv_tmo ms of the call.
 *            This is the total time until they arrive; receiving a byte does not restart the wait.
 *            asize is the number of bytes already received. TMO_POL does not wait.
 * @note The elapsed time is measured with tk_get_otm. The actual length of one wait differs from the
 *       specified value, so it is not measured by accumulating the specified values.
 * @note When req->size is 0, nothing is read and the number of received bytes is returned in asize.
 */
LOCAL ER read_data(T_BLENUS_DCB *p_dcb, T_DEVREQ *req)
{
	T_BLENUS_BUFF	*p_buff;
	UW		tail;
	UB		*pd;
	SZ		rsize;
	TMO		tmo;		/* rcv_tmo (ms). TMO_FEVR means no limit */
	SYSTIM		t0;		/* Time the call started (used only when tmo is finite) */
	ER		err;
	UINT		imask;

	p_buff	= &p_dcb->rcv_buff;
	pd	= req->buf;
	rsize	= req->size;
	tmo	= p_dcb->rcv_tmo;
	err	= E_OK;
	t0.hi	= 0;
	t0.lo	= 0;

	if (rsize != 0) {
		if (tmo >= 0) {
			tk_get_otm(&t0);
		}

		DI(imask);
		tail = p_buff->tail;
		blenus_ei(imask);

		while (rsize) {
			DI(imask);
			if (p_buff->top != tail) {
				*pd++ = p_buff->data[tail++];
				if (tail >= DEVCONF_BLENUS_BUFFSIZE) {
					tail = 0;
				}
				p_buff->tail = tail;
				blenus_ei(imask);
				rsize--;
			} else {
				TMO	slice;
				SYSTIM	now;
				UW	elapsed;

				/* Do not call tk_wup_tsk from an IRQ. DI()/BASEPRI right after a write
				 * masks SoftDevice and the link drops from CONN_SUP_TIMEOUT. */
				blenus_ei(imask);
				slice = 5;	/* ms. CNF_TIMER_PERIOD is added, so the actual wait is about 20 ms */
				if (tmo >= 0) {
					tk_get_otm(&now);
					elapsed = now.lo - t0.lo;
					if (elapsed >= (UW)tmo) {
						err = E_TMOUT;	/* Wait time is up (includes TMO_POL) */
						break;
					}
					if ((UW)tmo - elapsed < (UW)slice) {
						slice = (TMO)((UW)tmo - elapsed);
					}
				}
				err = tk_slp_tsk(slice);
				DI(imask);
				tail = p_buff->tail;
				blenus_ei(imask);
				if (err == E_TMOUT) {
					err = E_OK;	/* Go back to the top and recheck the buffer */
					continue;
				}
				if (err != E_OK) {
					break;
				}
			}
		}
		req->asize = req->size - rsize;
	} else {
		DI(imask);
		rsize = (SZ)p_buff->top - (SZ)p_buff->tail;
		blenus_ei(imask);
		if (rsize < 0) {
			rsize += DEVCONF_BLENUS_BUFFSIZE;
		}
		req->asize = rsize;
	}

	return err;
}

/**
 * @brief Queue the whole request in snd_buff, then hand it to SoftDevice in one go. Task context only.
 * @param p_dcb Control block of the device driver
 * @param req   Request. req->buf / req->size give the data to send.
 *              req->asize returns the number of bytes sent
 * @return E_OK / E_OBJ (not connected, or Notify not subscribed) /
 *         E_TMOUT (retries ran out without TX_RDY; sets DEV_BLENUS_ERR_TOVR) /
 *         E_IO (other send errors)
 * @details Queue the whole request first, then hand it to SoftDevice in one go.
 *          Kicking the transmitter per byte would emit one NUS notification per
 *          character: hvn_tx_queue_size is 1, so throughput collapses to one byte per
 *          connection event and the peer sees the message split across notifications.
 *
 *          When the ring becomes full, keep going while draining it with drain_snd_buff.
 * @note When req->size is 0, nothing is written and the free space of snd_buff is returned in asize.
 * @note On error, the data queued by this call is discarded and asize is set to 0.
 */
LOCAL ER write_data(T_BLENUS_DCB *p_dcb, T_DEVREQ *req)
{
	T_BLENUS_BUFF	*p_buff;
	UW		next;
	UB		*pd;
	INT		ssize;
	ER		err;
	UINT		imask;

	p_buff	= &p_dcb->snd_buff;
	pd	= req->buf;
	ssize	= req->size;
	err	= E_OK;

	if (ssize == 0) {
		req->asize = DEVCONF_BLENUS_BUFFSIZE - 1 - buff_used(p_buff);
		return E_OK;
	}

	while (ssize > 0) {
		next = p_buff->top + 1;
		if (next >= DEVCONF_BLENUS_BUFFSIZE) {
			next = 0;
		}

		DI(imask);
		if (next != p_buff->tail) {
			p_buff->data[p_buff->top] = *pd++;
			p_buff->top = next;
			blenus_ei(imask);
			ssize--;
			continue;
		}
		blenus_ei(imask);

		/* Ring full: drain what is queued, then keep filling. */
		err = drain_snd_buff(p_dcb);
		if (err != E_OK) {
			break;
		}
	}

	if (err == E_OK) {
		err = drain_snd_buff(p_dcb);
	}
	if (err != E_OK) {
		/* Never leave a partial message behind: it would be prepended to
		 * the next write and corrupt the record boundary on the peer.
		 */
		buff_flush(p_buff);
		req->asize = 0;
	} else {
		req->asize = req->size - ssize;
	}

	return err;
}

/*----------------------------------------------------------------------
 * Open device
 */

/**
 * @brief Open the device (mSDI openfn).
 * @param devid Device ID (unused)
 * @param omode Open mode (TD_READ / TD_WRITE / TD_UPDATE)
 * @param p_msdi mSDI management information. exinf holds the control block of the device driver
 * @return E_OK / error of blenus_open
 * @details The buffers and the error flags are initialized only if the device is not already
 *          open (opened is FALSE). Then SoftDevice / GAP / GATT / NUS / advertising are
 *          brought up by blenus_open() (idempotent), and connected is synchronized with
 *          the current link state.
 */
LOCAL ER dev_blenus_openfn(ID devid, UINT omode, T_MSDI *p_msdi)
{
	T_BLENUS_DCB	*p_dcb;
	ER		err;

	(void)devid;
	p_dcb = (T_BLENUS_DCB *)p_msdi->dmsdi.exinf;

	p_dcb->omode = omode;
	/* Keep buffers across re-open; only clear on first open. */
	if (!p_dcb->opened) {
		blenus_buff_init(&p_dcb->snd_buff);
		blenus_buff_init(&p_dcb->rcv_buff);
		p_dcb->tx_busy = FALSE;
		p_dcb->com_error = 0;
	}

	/* SoftDevice / GAP / GATT / NUS / advertising (idempotent). */
	err = blenus_open();
	if (err == E_OK) {
		p_dcb->opened = TRUE;
		/* Sync link state (normally disconnected just after first enable). */
		p_dcb->connected = blenus_is_connected() ? TRUE : FALSE;
	}
	return err;
}

/*----------------------------------------------------------------------
 * Close Device
 */

/**
 * @brief Close the device (mSDI closefn).
 * @param devid  Device ID (unused)
 * @param option Close option (unused)
 * @param p_msdi mSDI management information. exinf holds the control block of the device driver
 * @return E_OK (the error itself, if blenus_close returns an error other than E_NOSPT)
 * @details Stopping SoftDevice is not supported (blenus_close returns E_NOSPT), so
 *          E_NOSPT is converted to E_OK and only opened is cleared.
 *          Advertising and the connection are kept after close.
 */
LOCAL ER dev_blenus_closefn(ID devid, UINT option, T_MSDI *p_msdi)
{
	T_BLENUS_DCB	*p_dcb;
	ER		err;

	(void)devid;
	(void)option;
	p_dcb = (T_BLENUS_DCB *)p_msdi->dmsdi.exinf;

	err = blenus_close();
	if (err == E_NOSPT) {
		err = E_OK;
	}
	p_dcb->opened = FALSE;
	return err;
}

/*----------------------------------------------------------------------
 * Read Device
 */

/**
 * @brief Read from the device (mSDI readfn).
 * @param req    Request. start >= 0 selects data, start < 0 selects an attribute (TDN_*)
 * @param p_msdi mSDI management information. exinf holds the control block of the device driver
 * @return Result of read_data / read_atr, or E_OACV if data is read without TD_READ in the open mode
 */
LOCAL ER dev_blenus_readfn(T_DEVREQ *req, T_MSDI *p_msdi)
{
	T_BLENUS_DCB	*p_dcb;
	ER		err;

	p_dcb = (T_BLENUS_DCB *)p_msdi->dmsdi.exinf;

	if (req->start >= 0) {
		if (p_dcb->omode & TD_READ) {
			err = read_data(p_dcb, req);
		} else {
			err = E_OACV;
		}
	} else {
		err = read_atr(p_dcb, req);
	}

	return err;
}

/*----------------------------------------------------------------------
 * Write Device
 */

/**
 * @brief Write to the device (mSDI writefn).
 * @param req    Request. start >= 0 selects data, start < 0 selects an attribute (TDN_*)
 * @param p_msdi mSDI management information. exinf holds the control block of the device driver
 * @return Result of write_data / write_atr, or E_OACV if data is written without TD_WRITE in the open mode
 */
LOCAL ER dev_blenus_writefn(T_DEVREQ *req, T_MSDI *p_msdi)
{
	T_BLENUS_DCB	*p_dcb;
	ER		err;

	p_dcb = (T_BLENUS_DCB *)p_msdi->dmsdi.exinf;

	if (req->start >= 0) {
		if (p_dcb->omode & TD_WRITE) {
			err = write_data(p_dcb, req);
		} else {
			err = E_OACV;
		}
	} else {
		err = write_atr(p_dcb, req);
	}

	return err;
}

/*----------------------------------------------------------------------
 * Event Device
 */

/**
 * @brief Handle a device event (mSDI eventfn). Not supported.
 * @param evttyp Event type (unused)
 * @param evtinf Event information (unused)
 * @param p_msdi mSDI management information (unused)
 * @return E_NOSPT always
 */
LOCAL ER dev_blenus_eventfn(INT evttyp, void *evtinf, T_MSDI *p_msdi)
{
	(void)evttyp;
	(void)evtinf;
	(void)p_msdi;
	return E_NOSPT;
}

/*----------------------------------------------------------------------
 * BLE NUS Device initialization and registration
 */

/**
 * @brief Initialize the BLE NUS device and register it with mSDI.
 * @param unit Unit number (0 to DEV_BLENUS_UNITNM - 1)
 * @return E_OK / E_PAR (unit out of range) / E_NOMEM (cannot allocate the control block) /
 *         error of msdi_def_dev
 * @details Called from the device initialization in devinit.c (unit 0). Allocates the control
 *          block and registers the device name DEVCNF_BLENUS_DEVNAME + ('a' + unit)
 *          ("blua") with msdi_def_dev.
 *          SoftDevice is not started here (blenus_open does it on the first open).
 * @note Until drv_ready is set at the end, notify_* are ignored (get_dcb returns NULL).
 */
EXPORT ER dev_init_blenus(UW unit)
{
	T_BLENUS_DCB	*p_dcb;
	T_IDEV		idev;
	T_MSDI		*p_msdi;
	T_DMSDI		dmsdi;
	ER		err;
	INT		i;

	if (unit >= DEV_BLENUS_UNITNM) {
		return E_PAR;
	}

#if TK_SUPPORT_MEMLIB
	p_dcb = (T_BLENUS_DCB *)Kmalloc(sizeof(T_BLENUS_DCB));
	if (p_dcb == NULL) {
		return E_NOMEM;
	}
	dev_blenus_cb[unit] = p_dcb;
#else
	p_dcb = &dev_blenus_cb[unit];
#endif
	knl_memset(p_dcb, 0, sizeof(T_BLENUS_DCB));

	dmsdi.exinf	= p_dcb;
	dmsdi.drvatr	= 0;
	dmsdi.devatr	= TDK_UNDEF;
	dmsdi.nsub	= 0;
	dmsdi.blksz	= 1;
	dmsdi.openfn	= dev_blenus_openfn;
	dmsdi.closefn	= dev_blenus_closefn;
	dmsdi.readfn	= dev_blenus_readfn;
	dmsdi.writefn	= dev_blenus_writefn;
	dmsdi.eventfn	= dev_blenus_eventfn;

	knl_strcpy((char *)dmsdi.devnm, DEVCNF_BLENUS_DEVNAME);
	i = knl_strlen(DEVCNF_BLENUS_DEVNAME);
	dmsdi.devnm[i] = (UB)('a' + unit);
	dmsdi.devnm[i + 1] = 0;

	err = msdi_def_dev(&dmsdi, &idev, &p_msdi);
	if (err != E_OK) {
		goto err_2;
	}

	p_dcb->unit = unit;
	p_dcb->evtmbfid = idev.evtmbfid;
	p_dcb->opened = FALSE;
	p_dcb->snd_tmo = DEVCNF_BLENUS_SND_TMO;
	p_dcb->rcv_tmo = DEVCNF_BLENUS_RCV_TMO;
	blenus_buff_init(&p_dcb->snd_buff);
	blenus_buff_init(&p_dcb->rcv_buff);
	p_dcb->connected = blenus_is_connected() ? TRUE : FALSE;

	drv_ready = TRUE;
	return E_OK;

err_2:
#if TK_SUPPORT_MEMLIB
	Kfree(p_dcb);
#endif
	return err;
}

#endif	/* DEV_BLENUS_ENABLE */
