/**
 * @file blenus.h
 * @author koh aiaida (koh@aiaida.jp)
 * @brief Internal definitions of the BLE NUS device driver.
 * @date 2026-08-10
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef	__DEV_BLENUS_H__
#define	__DEV_BLENUS_H__

#include "../common/drvif/msdrvif.h"
#include "../include/dev_blenus.h"
#include "blenus_cnf.h"

/*----------------------------------------------------------------------
 * Hardware dependent definition
 */
#ifdef CPU_NRF5
#include "sysdepend/nrf5/blenus_nrf5.h"
#endif	/* CPU_NRF5 */

/*----------------------------------------------------------------------*/
/* SoftDevice / NUS event bridge (BLE observer context)
 *
 * Do not call tk_wup_tsk from the SoftDevice observer (SWI2) or from SysTick (priority 1).
 * DI()/BASEPRI masks SoftDevice, and the link dies from CONN_SUP_TIMEOUT.
 * Waiting tasks notice a change by rechecking the state on each tk_slp_tsk timeout
 * (read_data / wait_tx_rdy). Waking via SWI0 (APP_LOW) (blenus_wake_waiters) is an
 * optional extra means; waiting tasks work without it.
 * Declared here (driver-internal), not in the user-facing header.
 */
IMPORT void dev_blenus_notify_rcv( UW unit, UW data );
IMPORT void dev_blenus_notify_tx_rdy( UW unit );
IMPORT void dev_blenus_notify_conn( UW unit, BOOL connected );
IMPORT void dev_blenus_notify_comm( UW unit, BOOL started );
IMPORT void dev_blenus_notify_err( UW unit, UW err );
IMPORT void blenus_wake_waiters(void);

/*----------------------------------------------------------------------*/
/* Communication data buffer (same layout as ser)
 */
typedef struct {
	UW	top;
	UW	tail;
	ID	wait_tskid;
	UB	data[DEVCONF_BLENUS_BUFFSIZE];
} T_BLENUS_BUFF;

Inline void blenus_buff_init(T_BLENUS_BUFF *buff)
{
	buff->top = buff->tail = 0;
	buff->wait_tskid = 0;
}

/*----------------------------------------------------------------------*/
/* Device driver Control block
 */
typedef struct {
	UW	unit;		/* Unit No. */
	UINT	omode;		/* Open mode */
	ID	evtmbfid;	/* MBF ID for event notification */
	BOOL	opened;		/* Device open state */

	BOOL	connected;	/* GAP connected */
	BOOL	comm_started;	/* NUS CCCD / notification enabled */
	BOOL	tx_busy;	/* Waiting SoftDevice TX_RDY */
	UW	com_error;	/* Communication Error flags */
	TMO	snd_tmo;	/* Send timeout (TDN_BLENUS_SNDTMO / wait_tx_rdy) */
	TMO	rcv_tmo;	/* Receive timeout */

	T_BLENUS_BUFF	snd_buff;
	T_BLENUS_BUFF	rcv_buff;
} T_BLENUS_DCB;

#endif	/* __DEV_BLENUS_H__ */
