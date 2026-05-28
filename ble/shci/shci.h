#ifndef BLE_SHCI_H
#define BLE_SHCI_H

#include "ble/tl/tl.h"

#define SHCI_EVT_CODE (0xFF)
#define SHCI_SUB_EVT_CODE_BASE (0x9200)

typedef enum
{
    WIRELESS_FW_RUNNING = 0x00,
    FUS_FW_RUNNING = 0x01,
    NVM_BACKUP_RUNNING = 0x10,
    NVM_RESTORE_RUNNING = 0x11
} shci_sys_evt_ready_rsp_t;

typedef enum
{
    ERR_BLE_INIT = 0, /* This event is currently not reported by the CPU2                    */
    ERR_THREAD_LLD_FATAL_ERROR = 125,
    ERR_THREAD_UNKNOWN_CMD = 126,
    ERR_ZIGBEE_UNKNOWN_CMD = 200,
} shci_sys_err_code_t;

typedef PACKED_STRUCT
{
    shci_sys_evt_ready_rsp_t sys_evt_ready_rsp;
}
shci_c2_ready_evt_t;

typedef PACKED_STRUCT
{
    uint32_t start_address;
    uint32_t size;
}
shci_c2_ble_nvm_ram_update_evt_t;

typedef PACKED_STRUCT
{
    uint32_t number_of_words;
}
shci_c2_nvm_start_write_evt_t;

typedef PACKED_STRUCT
{
    uint32_t number_of_sectors;
}
shci_c2_nvm_start_erase_evt_t;

typedef PACKED_STRUCT
{
    uint8_t payload_cmd_size;
    uint8_t config1;
    uint8_t evt_mask1;
    uint8_t spare1;
    uint32_t ble_nvm_ram_address;
    uint32_t thread_nvm_ram_address;
    uint16_t revision_id;
    uint16_t device_id;
}
shci_c2_config_cmd_param_t;

#define SHCI_C2_CONFIG_PAYLOAD_CMD_SIZE (sizeof(shci_c2_config_cmd_param_t) - 1)

#define SHCI_C2_CONFIG_EVTMASK1_BIT0_ERROR_NOTIF_ENABLE (1 << 0)
#define SHCI_C2_CONFIG_EVTMASK1_BIT1_BLE_NVM_RAM_UPDATE_ENABLE (1 << 1)
#define SHCI_C2_CONFIG_EVTMASK1_BIT2_THREAD_NVM_RAM_UPDATE_ENABLE (1 << 2)
#define SHCI_C2_CONFIG_EVTMASK1_BIT3_NVM_START_WRITE_ENABLE (1 << 3)
#define SHCI_C2_CONFIG_EVTMASK1_BIT4_NVM_END_WRITE_ENABLE (1 << 4)
#define SHCI_C2_CONFIG_EVTMASK1_BIT5_NVM_START_ERASE_ENABLE (1 << 5)
#define SHCI_C2_CONFIG_EVTMASK1_BIT6_NVM_END_ERASE_ENABLE (1 << 6)

typedef enum
{
    SHCI_SUB_EVT_CODE_READY = SHCI_SUB_EVT_CODE_BASE,
    SHCI_SUB_EVT_ERROR_NOTIF,
    SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE,
    SHCI_SUB_EVT_THREAD_NVM_RAM_UPDATE,
    SHCI_SUB_EVT_NVM_START_WRITE,
    SHCI_SUB_EVT_NVM_END_WRITE,
    SHCI_SUB_EVT_NVM_START_ERASE,
    SHCI_SUB_EVT_NVM_END_ERASE,
    SHCI_SUB_EVT_CODE_CONCURRENT_802154_EVT,
} shci_sub_evt_code_t;

typedef void (*shci_callback_t)(void);

void shci_init(tl_cmd_packet_t *pcmd_buffer, sys_cmd_callback_t sys_cmd_callback,
               sys_evt_callback_t sys_evt_callback);
void shci_get_wireless_info(tl_wireless_fw_info_t *p_wireless_info);

#endif
