/**
 * @file blenus_nrf5.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief nRF5 (SoftDevice) dependent part of the BLE NUS device driver.
 * @date 2026-08-10
 *
 * @copyright Copyright (c) 2025-2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define PROHIBIT_DEF_SIZE_T	1	/* Avoid double definition of size_t (Nordic SDK vs uT-Kernel) */
#include <stdint.h>
#include <string.h>
#include <tk/tkernel.h>
#include <tstdlib.h>
#include <tm/tmonitor.h>
#include <config_device.h>
#include "../../blenus.h"
#include "ble.h"
#include "ble_advdata.h"
#include "ble_nus.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_sdh_soc.h"
#include "nrf_nvic.h"
#include "app_util_platform.h"
#if NRF_MODULE_ENABLED(NRF_BLE_CONN_PARAMS_ENABLED)
#include "app_timer.h"
#include "app_error.h"
#endif

#define MICROBIT_V2                     1

#define APP_BLE_CONN_CFG_TAG            1                                           /**< A tag identifying the SoftDevice BLE configuration. */

#define DEVICE_NAME                     "micro:bit2_UART"                           /**< Name of device. Will be included in the advertising data. */
#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */

#define APP_BLE_OBSERVER_PRIO           3                                           /**< Application's BLE observer priority. You shouldn't need to modify this value. */

#define APP_ADV_INTERVAL                64                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */

#define APP_ADV_DURATION                0                                           /**< Advertising duration (10 ms units). 0 = no timeout (keep discoverable). */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(20, UNIT_1_25_MS)             /**< Minimum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(75, UNIT_1_25_MS)             /**< Maximum acceptable connection interval (75 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   0                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */

#if NRF_MODULE_ENABLED(NRF_BLE_CONN_PARAMS_ENABLED)
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000)                       /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000)                      /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */
#endif

#define DEAD_BEEF                       0xDEADBEEF                                  /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define UART_TX_BUF_SIZE                256                                         /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE                256                                         /**< UART RX buffer size. */

#define TX_PIN_NUMBER               6                               // P0.06
#define RX_PIN_NUMBER               40                              // P1.08
#define RTS_PIN_NUMBER              UART_PIN_DISCONNECTED
#define CTS_PIN_NUMBER              UART_PIN_DISCONNECTED

#define E_OK		(0)	/* Completed successfully */
#if 0
NRF_BLE_QWR_DEF(m_qwr);                                                             /**< Context for the Queued Write module.*/
#endif

BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);                                   /**< BLE NUS service instance. */
NRF_BLE_GATT_DEF(m_gatt);                                                          /**< GATT module instance. */
BLE_ADVERTISING_DEF(m_advertising);                                                 /**< Advertising module instance. */
 
static uint16_t   m_conn_handle          = BLE_CONN_HANDLE_INVALID;                 /**< Handle of the current connection. */
static uint16_t   m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;            /**< Maximum length of data (in bytes) that can be transmitted to the peer by the Nordic UART service module. */
static ble_uuid_t m_adv_uuids[]          =                                          /**< Universally unique service identifier. */
{
    {BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}
};

#define NRF_LOG_INFO(...) do { } while (0)  // Disable logging for this example
#define NRF_LOG_DEBUG(...) do { } while (0) // Disable debug logging for this example
#define NRF_LOG_ERROR(...) do { } while (0) // Disable error logging for this example
#define NRF_LOG_HEXDUMP_DEBUG(...) do { } while (0) // Disable hexdump debug logging for this example

#if NRF_MODULE_ENABLED(NRF_BLE_CONN_PARAMS_ENABLED)
/**@brief Function for handling an event from the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module
 *          which are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by simply setting
 *       the disconnect_on_fail config parameter, but instead we use the event handler
 *       mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}

/**@brief Function for handling errors from the Connection Parameters module.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}
#endif // NRF_MODULE_ENABLED(NRF_BLE_CONN_PARAMS_ENABLED)

/**@brief Function for starting advertising. */
static void advertising_start(void);

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
            //err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
            //APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_IDLE:
            /* Nordic UART sample enters system-off here; that stops discovery
             * after the advertising timeout. Restart so Web Bluetooth can find us. */
            advertising_start();
            break;
        default:
            break;
    }
}

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    uint32_t err_code;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_INFO("Connected");
#if INCLDUE_INDICATION
            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
            APP_ERROR_CHECK(err_code);
#endif
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
#if DEVCNF_USE_BLENUS
            dev_blenus_notify_conn(0, TRUE);
#endif
#if NRF_MODULE_ENABLED(NRF_BLE_QWR)            
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);
#endif // NRF_MODULE_ENABLED(NRF_BLE_QWR)
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected");
            // LED indication will be changed when advertising starts.
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            /* The next peer may not negotiate the MTU: start from the default. */
            m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - OPCODE_LENGTH - HANDLE_LENGTH;
#if DEVCNF_USE_BLENUS
            dev_blenus_notify_conn(0, FALSE);
#endif
            /* Leave re-advertising to the DISCONNECTED handling of ble_advertising (do not start it twice). */
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // Pairing not supported
            err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            break;
    }
}

/**@brief Function for the SoftDevice initialization.
 *
 * @details This function initializes the SoftDevice and the BLE event interrupt.
 */
static ret_code_t ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    tm_printf((UB *)"blenus sdh_enable err=%d\n", err_code);
    if (err_code != NRF_SUCCESS) {
        return err_code;
    }

    /* Overwritten from APP_RAM_START (nrf_sdh_ble.c). */
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    tm_printf((UB *)"blenus cfg ram=0x%x err=%d\n", ram_start, err_code);
    if (err_code != NRF_SUCCESS) {
        return err_code;
    }

    err_code = nrf_sdh_ble_enable(&ram_start);
    tm_printf((UB *)"blenus enable ram=0x%x err=%d\n", ram_start, err_code);
    if (err_code != NRF_SUCCESS) {
        return err_code;
    }

    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
    return NRF_SUCCESS;
}

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{
    uint32_t               err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));

    init.advdata.name_type          = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance = false;
    /* General discoverable: matches APP_ADV_DURATION=0 (no advertising timeout). */
    init.advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

    init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids  = m_adv_uuids;

    init.config.ble_adv_fast_enabled  = true;
    init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;
    init.evt_handler = on_adv_evt;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/**@brief Function for starting advertising. */
static void advertising_start(void)
{
    uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for the GAP initialization.
 *
 * @details This function will set up all the necessary GAP (Generic Access Profile) parameters of
 *          the device. It also sets the permissions and appearance.
 */
static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *) DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling events from the GATT library. */
static void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt)
{
    if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED))
    {
        m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
        NRF_LOG_INFO("Data len is set to 0x%X(%d)", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
    }
    NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x",
                  p_gatt->att_mtu_desired_central,
                  p_gatt->att_mtu_desired_periph);
}

/**@brief Function for initializing the GATT library. */
static void gatt_init(void)
{
    ret_code_t err_code;

    err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
#if ORG    
    APP_ERROR_HANDLER(nrf_error);
#else
   NRF_LOG_ERROR(nrf_error); // Disable error handling for this example
#endif
}

/**@brief Function for handling the data from the Nordic UART Service.
 *
 * @details RX bytes go to device/blenus RX ring (task context reads via tk_srea_dev).
 *          SoftDevice observer context: ring buffer only. No tk_wup_tsk.
 *
 * @param[in] p_evt       Nordic UART Service event.
 */
/**@snippet [Handling the data received over BLE] */
static void nus_data_handler(ble_nus_evt_t * p_evt)
{
#if DEVCNF_USE_BLENUS
    if (p_evt->type == BLE_NUS_EVT_RX_DATA)
    {
        NRF_LOG_DEBUG("Received data from BLE NUS.");
        NRF_LOG_HEXDUMP_DEBUG(p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);

        for (uint32_t i = 0; i < p_evt->params.rx_data.length; i++)
        {
            UB byte = p_evt->params.rx_data.p_data[i];

            /* Driver RX buffer (non-blocking). Wake is SWI0, not SWI2. */
            dev_blenus_notify_rcv(0, byte);
        }
        /* Match previous console behavior: append LF after CR-terminated packets. */
        if (p_evt->params.rx_data.length > 0 &&
            p_evt->params.rx_data.p_data[p_evt->params.rx_data.length - 1] == '\r')
        {
            dev_blenus_notify_rcv(0, '\n');
        }
    }
    else if (p_evt->type == BLE_NUS_EVT_TX_RDY)
    {
        dev_blenus_notify_tx_rdy(0);
    }
    else if (p_evt->type == BLE_NUS_EVT_COMM_STARTED)
    {
        dev_blenus_notify_comm(0, TRUE);
    }
    else if (p_evt->type == BLE_NUS_EVT_COMM_STOPPED)
    {
        dev_blenus_notify_comm(0, FALSE);
    }
#else
    (void)p_evt;
#endif
}
/**@snippet [Handling the data received over BLE] */

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
    uint32_t           err_code;
    ble_nus_init_t     nus_init;
    nrf_ble_qwr_init_t qwr_init = {0};

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;

#if NRF_MODULE_ENABLED(NRF_BLE_QWR) 
    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);
#endif // NRF_MODULE_ENABLED(NRF_BLE_QWR)

    // Initialize NUS.
    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;

    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
}

#include "nrf_sdm.h"

/*----------------------------------------------------------------------
 * SoftDevice bring-up / low-level TX (called from device/blenus)
 */

/**
 * @brief Bring up SoftDevice / GAP / GATT / NUS / advertising.
 * @return E_OK / E_SYS (SoftDevice could not be enabled or configured)
 * @details See blenus_nrf5.h. The initialization steps depend on their order.
 *          -# ble_stack_init (enable SoftDevice)
 *          -# initialize gap / gatt / services / advertising / conn_params
 *          -# sd_softdevice_vector_table_base_set (hand the application VTOR over)
 *          -# advertising_start
 * @note The VTOR must be set after SoftDevice is enabled and before advertising starts.
 * @note Only a failure to set the VTOR does not return; it halts.
 */
ER blenus_open(void)
{
    extern const void (*exchdr_tbl[])();
    uint32_t err_code;
    static BOOL opened = FALSE;

    /* Idempotent: reopen / multiple openfn calls are safe. */
    if (opened) {
        return E_OK;
    }

    err_code = ble_stack_init();
    if (err_code != NRF_SUCCESS) {
        return E_SYS;
    }
    gap_params_init();
    gatt_init();

    services_init();
    advertising_init();
#if NRF_MODULE_ENABLED(NRF_BLE_CONN_PARAMS_ENABLED)
    conn_params_init();
#endif

    /* Hand the application VTOR over first, once SoftDevice is enabled (before advertising starts). */
    err_code = sd_softdevice_vector_table_base_set((UW)exchdr_tbl);
    if (err_code != NRF_SUCCESS) {
        while (1) {}
    }

    advertising_start();

    /* SWI0 is not used: tk_wup_tsk from an IRQ right after a write drops the link. */
    opened = TRUE;
    return E_OK;
}

ER blenus_close(void)
{
    /* Teardown (stop advertising / SoftDevice disable) is deferred:
     * the app keeps "blua" open for the session, and reopen-after-disable
     * needs careful SoftDevice sequencing. Closefn maps E_NOSPT to E_OK.
     */
    return E_NOSPT;
}

UINT blenus_ll_max_payload(void)
{
    UINT maxlen = m_ble_nus_max_data_len;

    /* Before the ATT MTU exchange this is the default 20 bytes. */
    if (maxlen == 0 || maxlen > BLE_NUS_MAX_DATA_LEN) {
        maxlen = BLE_GATT_ATT_MTU_DEFAULT - OPCODE_LENGTH - HANDLE_LENGTH;
    }
    return maxlen;
}

ER blenus_ll_send(UB *buf, UINT *plen)
{
    uint32_t err_code;
    uint16_t length;

    if (buf == NULL || plen == NULL) {
        return E_PAR;
    }
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID) {
        return E_OBJ;
    }

    length = (uint16_t)*plen;
    if (length == 0) {
        return E_OK;
    }
    if (length > blenus_ll_max_payload()) {
        length = (uint16_t)blenus_ll_max_payload();
    }

    err_code = ble_nus_data_send(&m_nus, buf, &length, m_conn_handle);
    if (err_code == NRF_SUCCESS) {
        *plen = length;
        return E_OK;
    }
    if (err_code == NRF_ERROR_RESOURCES || err_code == NRF_ERROR_BUSY) {
        return E_BUSY;
    }
    if (err_code == NRF_ERROR_INVALID_STATE) {
        return E_OBJ;
    }
    return E_IO;
}

B blenus_is_connected(void)
{
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
        return TRUE;
    }
    return FALSE;
}

/**
 * @brief SWI0 interrupt handler. Only calls blenus_wake_waiters().
 * @param intno Interrupt number (unused)
 * @note Registered by blenus_ll_wake_irq_start().
 */
static void blenus_swi0_hdr(UINT intno)
{
    (void)intno;
    blenus_wake_waiters();
}

ER blenus_ll_wake_irq_start(void)
{
    T_DINT dint;
    uint32_t err_code;

    dint.intatr = TA_HLNG;
    dint.inthdr = (FP)blenus_swi0_hdr;
    if (tk_def_int((UINT)SWI0_EGU0_IRQn, &dint) < E_OK) {
        return E_SYS;
    }

    /* APP_LOW (6). Same priority as SWI2, so it does not run in the middle of SWI2. */
    err_code = sd_nvic_SetPriority(SWI0_EGU0_IRQn, APP_IRQ_PRIORITY_LOW);
    if (err_code != NRF_SUCCESS) {
        return E_SYS;
    }
    err_code = sd_nvic_ClearPendingIRQ(SWI0_EGU0_IRQn);
    if (err_code != NRF_SUCCESS) {
        return E_SYS;
    }
    err_code = sd_nvic_EnableIRQ(SWI0_EGU0_IRQn);
    if (err_code != NRF_SUCCESS) {
        return E_SYS;
    }
    return E_OK;
}

void blenus_ll_pend_wake(void)
{
    (void)sd_nvic_SetPendingIRQ(SWI0_EGU0_IRQn);
}

