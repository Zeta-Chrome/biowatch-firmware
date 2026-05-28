#include "ble.h"
#include "ble/shci/shci.h"
#include "ble/tl/tl.h"
#include "ble/tl/tl_defs.h"
#include "hal/pwr/pwr.h"

#define BLE_EVT_QUEUE_LEN 5
#define TL_BLE_EVT_FRAME_SIZE 255
#define POOL_SIZE (BLE_EVT_QUEUE_LEN * 4 * DIVC((sizeof(tl_packet_header_t) + TL_BLE_EVT_FRAME_SIZE), 4))

PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t evt_pool[POOL_SIZE];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static tl_cmd_packet_t sys_cmd_buffer;
PLACE_IN_SECTION("MB_MEM2")
ALIGN(4) static uint8_t sys_spare_evt_buf[sizeof(tl_packet_header_t) + TL_EVT_HDR_SIZE + 255];
PLACE_IN_SECTION("MB_MEM2")
ALIGN(4) static uint8_t ble_spare_evt_buf[sizeof(tl_packet_header_t) + TL_EVT_HDR_SIZE + 255];

static void shci_cmd_callback();
static void shci_evt_callback(tl_evt_packet_t *evt_packet);

void ble_init()
{
    // Init TL
    tl_init();

    // Init SHCI
    shci_init(&sys_cmd_buffer, shci_cmd_callback, shci_evt_callback);

    tl_mm_conf_t mm_conf = {.p_ble_spare_evt_buffer = ble_spare_evt_buf,
                            .p_sys_spare_evt_buffer = sys_spare_evt_buf,
                            .p_async_evt_pool = evt_pool,
                            .async_evt_pool_size = POOL_SIZE,
                            .p_traces_evt_pool = NULL,
                            .traces_evt_pool_size = 0};
    tl_mm_init(&mm_conf);

    // Boot CPU2
    hal_pwr_boot_cpu2();
}

static void shci_cmd_callback()
{
    BW_LOG("Command Callback received");
}

static void sys_evt_err(tl_evt_packet_t *evt_packet)
{
    tl_async_evt_t *p_sys_event;
    shci_sys_err_code_t *p_sys_error_code;

    p_sys_event = (tl_async_evt_t *)(evt_packet->evt_serial.evt.payload);
    p_sys_error_code = (shci_sys_err_code_t *)p_sys_event->payload;

    BW_LOG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON %x \n\r", (*p_sys_error_code));

    if ((*p_sys_error_code) == ERR_BLE_INIT)
    {
        /* Error during BLE stack initialization */
        BW_LOG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON - ERR_BLE_INIT \n");
    }
    else
    {
        BW_LOG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON - BLE ERROR \n");
    }
    return;
}

static void sys_evt_ready(tl_evt_packet_t *evt_packet)
{
    tl_async_evt_t *p_sys_event;
    shci_c2_ready_evt_t *p_sys_ready_event;

    shci_c2_config_cmd_param_t config_param = {0};
    uint32_t revision_id = 0;
    uint32_t device_id = 0;

    p_sys_event = (tl_async_evt_t *)(evt_packet->evt_serial.evt.payload);
    p_sys_ready_event = (shci_c2_ready_evt_t *)p_sys_event->payload;

    if (p_sys_ready_event->sys_evt_ready_rsp == WIRELESS_FW_RUNNING)
    {
        BW_LOG(">>== WIRELESS_FW_RUNNING \n");

        // Debug enable 

        /* Enable all events Notification */
        config_param.payload_cmd_size = SHCI_C2_CONFIG_PAYLOAD_CMD_SIZE;
        config_param.evt_mask1 = SHCI_C2_CONFIG_EVTMASK1_BIT0_ERROR_NOTIF_ENABLE +
                                SHCI_C2_CONFIG_EVTMASK1_BIT1_BLE_NVM_RAM_UPDATE_ENABLE +
                                SHCI_C2_CONFIG_EVTMASK1_BIT2_THREAD_NVM_RAM_UPDATE_ENABLE +
                                SHCI_C2_CONFIG_EVTMASK1_BIT3_NVM_START_WRITE_ENABLE +
                                SHCI_C2_CONFIG_EVTMASK1_BIT4_NVM_END_WRITE_ENABLE +
                                SHCI_C2_CONFIG_EVTMASK1_BIT5_NVM_START_ERASE_ENABLE +
                                SHCI_C2_CONFIG_EVTMASK1_BIT6_NVM_END_ERASE_ENABLE;

        revision_id = dbg_mcu_get_revision_id();
        config_param.revision_id= (uint16_t)revision_id;
        BW_LOG(">>== DBGMCU_GetRevisionID= %x \n", revision_id);

        device_id = dbg_mcu_get_device_id();
        config_param.device_id = (uint16_t)device_id;
        BW_LOG(">>== DBGMCU_GetDeviceID= %x \n", device_id);

        SHCI_C2_Config(&config_param);

        APP_BLE_Init();
    }
    else if (p_sys_ready_event->sys_evt_ready_rsp == FUS_FW_RUNNING)
    {
        BW_LOG(">>== SHCI_SUB_EVT_CODE_READY - FUS_FW_RUNNING \n");
    }
    else
    {
        BW_LOG(">>== SHCI_SUB_EVT_CODE_READY - UNEXPECTED CASE \n");
    }

    return;
}

static void shci_evt_callback(tl_evt_packet_t *evt_packet)
{
    tl_async_evt_t *p_sys_evt;
    tl_wireless_fw_info_t wireless_info;

    p_sys_evt = (tl_async_evt_t *)evt_packet->evt_serial.evt.payload;

    switch (p_sys_evt->sub_evt_code)
    {
    case SHCI_SUB_EVT_CODE_READY:
        /* Read the firmware version of both the wireless firmware and the FUS */
        shci_get_wireless_info(&wireless_info);
        BW_LOG("Wireless Firmware version %d.%d.%d\n", wireless_info.version_major,
               wireless_info.version_minor, wireless_info.version_sub);
        BW_LOG("Wireless Firmware build %d\n", wireless_info.version_release_type);
        BW_LOG("FUS version %d.%d.%d\n", wireless_info.fus_version_major,
               wireless_info.fus_version_minor, wireless_info.fus_version_sub);

        BW_LOG(">>== SHCI_SUB_EVT_CODE_READY\n\r");
        sys_evt_ready(evt_packet);
        break;

    case SHCI_SUB_EVT_ERROR_NOTIF:
        BW_LOG(">>== SHCI_SUB_EVT_ERROR_NOTIF \n\r");
        sys_evt_err(evt_packet);
        break;

    case SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE:
        BW_LOG(">>== SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE -- BLE NVM RAM HAS BEEN UPDATED BY CPU2 \n");
        BW_LOG("     - StartAddress = %lx , Size = %ld\n",
               ((shci_c2_ble_nvm_ram_update_evt_t *)p_sys_evt->payload)->start_address,
               ((shci_c2_ble_nvm_ram_update_evt_t *)p_sys_evt->payload)->size);
        break;

    case SHCI_SUB_EVT_NVM_START_WRITE:
        BW_LOG("==>> SHCI_SUB_EVT_NVM_START_WRITE : NumberOfWords = %ld\n",
               ((shci_c2_nvm_start_write_evt_t *)p_sys_evt->payload)->number_of_words);
        break;

    case SHCI_SUB_EVT_NVM_END_WRITE:
        BW_LOG(">>== SHCI_SUB_EVT_NVM_END_WRITE\n\r");
        break;

    case SHCI_SUB_EVT_NVM_START_ERASE:
        BW_LOG("==>>SHCI_SUB_EVT_NVM_START_ERASE : NumberOfSectors = %ld\n",
               ((shci_c2_nvm_start_erase_evt_t *)p_sys_evt->payload)->number_of_sectors);
        break;

    case SHCI_SUB_EVT_NVM_END_ERASE:
        BW_LOG(">>== SHCI_SUB_EVT_NVM_END_ERASE\n\r");
        break;

    default:
        break;
    }
}
