/**
 * @file dev_blenus.h
 * @author koh aiaida (koh@aiaida.jp)
 * @brief User-facing definitions of the BLE NUS device driver.
 * @date 2026-08-10
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef	__DEVINC_BLENUS_H__
#define	__DEVINC_BLENUS_H__

/*----------------------------------------------------------------------*/
/* Device driver initialization
 */
IMPORT ER dev_init_blenus( UW unit );

/*----------------------------------------------------------------------*/
/* Attribute data number
 */
typedef enum {
	TDN_BLENUS_CONN		= -100,	/* R-: BLE connection state (BOOL) */
	TDN_BLENUS_COMM		= -101,	/* R-: NUS notification enabled (BOOL) */
	TDN_BLENUS_COMERR	= -102,	/* R-: Communication Error (UW) */
	TDN_BLENUS_SNDTMO	= -103,	/* RW: Send timeout (TMO); used by TX_RDY wait */
	TDN_BLENUS_RCVTMO	= -104,	/* RW: Receive timeout (TMO) */
} T_DN_BLENUS_ATR;

/* Communication Error */
#define	DEV_BLENUS_ERR_ROVR	(1u << 7)	/* Receive buffer overflow */
#define	DEV_BLENUS_ERR_TOVR	(1u << 6)	/* Send gave up (no TX_RDY in time) */

#endif	/* __DEVINC_BLENUS_H__ */
