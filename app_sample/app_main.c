/**
 * @file app_main.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief A simple usage example of the Nordic UART Service (NUS) over BLE.
 * @date 2026-08-16
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @details
 * `tk_opn_dev("blua")` starts SoftDevice / NUS / advertising.
 * Once connected and Notify (CCCD) is enabled, received bytes are echoed back over NUS
 * and also printed on the serial console. It does not depend on I2C / IMU.
 * As a liveness check, one LED of the LED matrix (ROW1/COL2) blinks.
 */
#include <tk/tkernel.h>
#include <tk/device.h>
#include <tm/tmonitor.h>
#include <tstdlib.h>
#include <sys/sysdef.h>

#define BLUA_DEVNM	((const UB *)"blua")

/** Polling interval (ms) while waiting for a connection / Notify. The unit of tk_dly_tsk is ms,
 *  and the timer period (CNF_TIMER_PERIOD = 10 ms) is added, so the actual wait is about 20 ms.
 *  Do not use tk_dly_tsk(1). */
#define BLUA_WAIT_TICK	(10)

/** Task stack sized to allow for nested SoftDevice calls */
#define BLUA_STKSZ	(2048)

/** micro:bit v2 matrix. ROW is active high, COL is active low. */
#define LED_ROW_PIN	(21)	/* P0.21 = ROW1 */
#define LED_COL_PIN	(11)	/* P0.11 = COL2 */

LOCAL ID	ddBlua;
LOCAL ID	tidEcho;
LOCAL ID	tidHello;
LOCAL ID	tidLed;

LOCAL void task_echo(INT stacd, void *exinf);
LOCAL void task_hello(INT stacd, void *exinf);
LOCAL void task_led(INT stacd, void *exinf);

/**
 * @brief Configure ROW/COL as outputs so that a single LED can be lit.
 */
LOCAL void led_init(void)
{
	out_w(GPIO(P0, PIN_CNF(LED_ROW_PIN)), 1);
	out_w(GPIO(P0, PIN_CNF(LED_COL_PIN)), 1);
	out_w(GPIO(P0, OUTSET), (1u << LED_ROW_PIN));
	out_w(GPIO(P0, OUTSET), (1u << LED_COL_PIN));	/* COL high = off */
}

/**
 * @brief Turn the single LED on / off.
 */
LOCAL void led_set(BOOL on)
{
	if (on) {
		out_w(GPIO(P0, OUTCLR), (1u << LED_COL_PIN));
	} else {
		out_w(GPIO(P0, OUTSET), (1u << LED_COL_PIN));
	}
}

/**
 * @brief Read a blua attribute as a BOOL.
 * @param dd Device descriptor
 * @param atr TDN_BLENUS_CONN or TDN_BLENUS_COMM
 * @return TRUE if the attribute is true. FALSE if the read fails.
 */
LOCAL BOOL blua_get_bool(ID dd, INT atr)
{
	BOOL	val = FALSE;
	SZ	asize;
	ER	err;

	err = tk_srea_dev(dd, atr, &val, sizeof(val), &asize);
	if (err < E_OK) {
		return FALSE;
	}
	return val ? TRUE : FALSE;
}

/**
 * @brief Whether the device is connected and NUS Notify is enabled.
 */
LOCAL BOOL blua_ready(ID dd)
{
	return blua_get_bool(dd, TDN_BLENUS_CONN)
	    && blua_get_bool(dd, TDN_BLENUS_COMM);
}

/**
 * @brief Write a byte sequence to NUS (tk_swri_dev).
 */
LOCAL ER blua_write(ID dd, const void *buf, UINT len)
{
	SZ	asize;

	return tk_swri_dev(dd, 0, buf, (SZ)len, &asize);
}

LOCAL T_CTSK ctsk_echo = {
	.itskpri = 20,
	.stksz   = BLUA_STKSZ,
	.task    = task_echo,
	.tskatr  = TA_HLNG | TA_RNG0,
};

LOCAL T_CTSK ctsk_hello = {
	.itskpri = 15,
	.stksz   = BLUA_STKSZ,
	.task    = task_hello,
	.tskatr  = TA_HLNG | TA_RNG0,
};

LOCAL T_CTSK ctsk_led = {
	.itskpri = 30,
	.stksz   = 512,
	.task    = task_led,
	.tskatr  = TA_HLNG | TA_RNG0,
};

/**
 * @brief Echo one received byte to NUS and also output it to tmonitor.
 *
 * The default receive timeout is TMO_FEVR, so a multi-byte request waits until all
 * bytes have arrived. The echo is therefore done one byte at a time.
 */
LOCAL void task_echo(INT stacd, void *exinf)
{
	UB	data;
	SZ	asize;
	ER	err;

	(void)stacd;
	(void)exinf;

	while (1) {
		if (!blua_ready(ddBlua)) {
			tk_dly_tsk(BLUA_WAIT_TICK);
			continue;
		}
		err = tk_srea_dev(ddBlua, 0, &data, 1, &asize);
		if (err >= E_OK && asize > 0) {
			(void)blua_write(ddBlua, &data, 1);
#if USE_TMONITOR
			tm_putchar(data);
#endif
		}
	}
}

/**
 * @brief Send a greeting once at the rising edge of Notify start.
 */
LOCAL void task_hello(INT stacd, void *exinf)
{
	BOOL		was_ready = FALSE;
	BOOL		was_conn = FALSE;
	static const UB	hello[] = "NUS ready\n";

	(void)stacd;
	(void)exinf;

	while (1) {
		BOOL conn = blua_get_bool(ddBlua, TDN_BLENUS_CONN);
		BOOL ready = conn && blua_get_bool(ddBlua, TDN_BLENUS_COMM);

#if USE_TMONITOR
		if (conn && !was_conn) {
			tm_putstring((UB *)"blenus connected\n");
		}
		if (!conn && was_conn) {
			tm_putstring((UB *)"blenus disconnected\n");
		}
#endif
		was_conn = conn;

		if (ready && !was_ready) {
			(void)blua_write(ddBlua, hello, (UINT)knl_strlen((char *)hello));
#if USE_TMONITOR
			tm_putstring((UB *)"NUS Notify started.\n");
#endif
		}
		was_ready = ready;
		tk_dly_tsk(BLUA_WAIT_TICK);
	}
}

/**
 * @brief Toggle one LED (ROW1/COL2) on and off about every 0.5 s, so that it blinks.
 */
LOCAL void task_led(INT stacd, void *exinf)
{
	BOOL	on = FALSE;

	(void)stacd;
	(void)exinf;

	led_init();
	while (1) {
		on = !on;
		led_set(on);
		tk_dly_tsk(500);
	}
}

/**
 * @brief User main. Opens blua and starts the echo / greeting / LED tasks.
 */
EXPORT INT usermain(void)
{
	T_RVER	rver;
	ER	err;

#if USE_TMONITOR
	tm_putstring((UB *)"Start NUS over BLE sample.\n");
#endif
	tk_ref_ver(&rver);
#if USE_TMONITOR
	tm_printf((UB *)"Make Code: %04x  Product ID: %04x\n", rver.maker, rver.prid);
	tm_printf((UB *)"Product Ver. %04x\n", rver.prver);
#endif

	ddBlua = tk_opn_dev(BLUA_DEVNM, TD_UPDATE);
	if (ddBlua < E_OK) {
		return ddBlua;
	}

	tidEcho = tk_cre_tsk(&ctsk_echo);
	if (tidEcho < E_OK) {
		err = tidEcho;
		goto err_echo;
	}
	tidHello = tk_cre_tsk(&ctsk_hello);
	if (tidHello < E_OK) {
		err = tidHello;
		goto err_hello;
	}
	tidLed = tk_cre_tsk(&ctsk_led);
	if (tidLed < E_OK) {
		err = tidLed;
		goto err_led;
	}

	tk_sta_tsk(tidEcho, 0);
	tk_sta_tsk(tidHello, 0);
	tk_sta_tsk(tidLed, 0);

	tk_slp_tsk(TMO_FEVR);
	return 0;

err_led:
	tk_del_tsk(tidHello);
err_hello:
	tk_del_tsk(tidEcho);
err_echo:
	(void)tk_cls_dev(ddBlua, 0);
	return err;
}
