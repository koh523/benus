/**
 * @file blenus_cnf.h
 * @author koh aiaida (koh@aiaida.jp)
 * @brief Configuration definitions of the BLE NUS device driver.
 * @date 2026-08-10
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef	__DEV_BLENUS_CNF_H__
#define	__DEV_BLENUS_CNF_H__

#define	DEVCNF_BLENUS_DEVNAME	"blu"	/* Device name ("blu" + unit -> "blua") */

#define	DEV_BLENUS_UNITNM	(1)	/* Number of device units */

/* RX/TX ring buffer size (bytes).
 * Must exceed the largest single write (app MSG_SIZE = 128) so that a whole
 * message can be queued before it is handed to SoftDevice.
 */
#define	DEVCONF_BLENUS_BUFFSIZE	256

#define	DEVCNF_BLENUS_SND_TMO	TMO_FEVR	/* Default; also used as TX_RDY wait */
#define	DEVCNF_BLENUS_RCV_TMO	TMO_FEVR

/* Max bytes handed to one ble_nus_data_send() call.
 * The effective size is further limited by the negotiated ATT MTU.
 */
#define	DEVCNF_BLENUS_MAXCHUNK	64

/* Max TX_RDY wait attempts while SoftDevice reports RESOURCES/BUSY.
 * Each wait uses TDN_BLENUS_SNDTMO (default DEVCNF_BLENUS_SND_TMO).
 */
#define	DEVCNF_BLENUS_TXRDY_RETRY	10

#endif	/* __DEV_BLENUS_CNF_H__ */
