/**
 * @file blenus_nrf5.h
 * @author koh aiaida (koh@aiaida.jp)
 * @brief Interface of the nRF5 (SoftDevice) dependent part of the BLE NUS device driver.
 * @date 2026-08-10
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef	__DEV_BLENUS_NRF5_H__
#define	__DEV_BLENUS_NRF5_H__

#include <tk/tkernel.h>

/**
 * @brief Initialize SoftDevice / GAP / GATT / NUS and start advertising.
 * @return E_OK / E_SYS (SoftDevice could not be enabled or configured).
 *         Does not return if setting the vector table (sd_softdevice_vector_table_base_set) fails.
 * @note Called from the device driver openfn. Idempotent (returns E_OK without doing anything if already initialized).
 */
IMPORT ER blenus_open(void);

/**
 * @brief Shut down NUS / SoftDevice.
 * @return E_NOSPT (teardown is not implemented; closefn normalizes it to E_OK)
 */
IMPORT ER blenus_close(void);

/**
 * @brief Return the BLE connection state.
 * @return TRUE if connected, otherwise FALSE
 */
IMPORT B blenus_is_connected(void);

/**
 * @brief Low-level send to SoftDevice (used by kick_tx of the device driver).
 * @param buf  Send buffer
 * @param plen In/out: requested length / length actually handed over
 * @return E_OK (also E_OK when the length is 0) / E_PAR (NULL argument) /
 *         E_OBJ (not connected, or the peer has not subscribed to Notify) /
 *         E_BUSY (the SoftDevice transmit queue is full; retry after TX_RDY) /
 *         E_IO (other SoftDevice errors)
 */
IMPORT ER blenus_ll_send(UB *buf, UINT *plen);

/**
 * @brief Maximum payload length usable for one NUS transmission.
 * @return Length based on the negotiated ATT MTU. 20 bytes before the MTU exchange.
 */
IMPORT UINT blenus_ll_max_payload(void);

/**
 * @brief Register and enable the SWI0 (APP_LOW) interrupt handler. Call after SoftDevice is enabled.
 * @return E_OK / E_SYS
 * @note Even if this is not used, waiting for receive and send works by polling with
 *       tk_slp_tsk timeouts.
 */
IMPORT ER blenus_ll_wake_irq_start(void);

/**
 * @brief Only pends SWI0 (no SVC, no kernel call). Intended to be called from SoftDevice SWI2.
 */
IMPORT void blenus_ll_pend_wake(void);

#endif /* __DEV_BLENUS_NRF5_H__ */
