#ifndef BLE_TL_DEFS_H
#define BLE_TL_DEFS_H

#include "utils/utils.h"

typedef PACKED_STRUCT
{
    uint32_t version;
}
tl_safe_boot_info_t;

typedef PACKED_STRUCT
{
    uint32_t version;
    uint32_t memory_size;
    uint32_t fus_info;
}
tl_fus_info_t;

typedef PACKED_STRUCT
{
    uint32_t version;
    uint32_t memory_size;
    uint32_t info_stack;
    uint32_t reserved;
}
tl_wireless_info_t;

typedef struct
{
    tl_safe_boot_info_t safe_boot_info_table;
    tl_fus_info_t fus_info_table;
    tl_wireless_info_t wireless_fw_info_table;
} tl_device_info_t;

typedef struct
{
    uint8_t *pcmd_buffer;
    uint8_t *pcs_buffer;
    uint8_t *pevt_queue;
    uint8_t *phci_acl_data_buffer;
} tl_ble_t;

typedef struct
{
    uint8_t *pcmd_buffer;
    uint8_t *sys_queue;
} tl_sys_t;

typedef struct
{
    uint8_t *spare_ble_buffer;
    uint8_t *spare_sys_buffer;
    uint8_t *ble_pool;
    uint32_t ble_pool_size;
    uint8_t *pevt_free_buffer_queue;
    uint8_t *traces_evt_pool;
    uint32_t traces_pool_size;
} tl_mem_manager_t;

typedef struct
{
    uint8_t *traces_queue;
} tl_traces_t;

typedef struct
{
    tl_device_info_t *p_device_info_table;
    tl_ble_t *p_ble_table;
    void *p_thread_table;
    tl_sys_t *p_sys_table;
    tl_mem_manager_t *p_mem_manager_table;
    tl_traces_t *p_traces_table;
    void *p_mac_802_15_4_table;
    void *p_zigbee_table;
    void *p_lld_tests_table;
    void *p_ble_lld_table;
} tl_ref_t;

typedef struct
{
    uint32_t device_info_table_state;
    uint8_t reserved1;
    uint8_t last_fus_active_state;
    uint8_t last_wireless_stack_state;
    uint8_t current_wireless_stack_type;
    uint32_t safe_boot_version;
    uint32_t fus_version;
    uint32_t fus_memory_size;
    uint32_t wireless_stack_version;
    uint32_t wireless_stack_memory_size;
    uint32_t wireless_firmware_ble_info;
    uint32_t wireless_firmware_thread_info;
    uint32_t reserved2;
    uint64_t uid64;
    uint16_t device_id;
} tl_fus_device_info_t;

typedef struct
{
    /**
     * wireless info
     */
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_sub;
    uint8_t version_branch;
    uint8_t version_release_type;
    uint8_t memory_size_sram2b; /*< multiple of 1k */
    uint8_t memory_size_sram2a; /*< multiple of 1k */
    uint8_t memory_size_sram1;  /*< multiple of 1k */
    uint8_t memory_size_flash;  /*< multiple of 4k */
    uint8_t stack_type;
    // fus info
    uint8_t fus_version_major;
    uint8_t fus_version_minor;
    uint8_t fus_version_sub;
    uint8_t fus_memory_size_sram2b; /*< multiple of 1k */
    uint8_t fus_memory_size_sram2a; /*< multiple of 1k */
    uint8_t fus_memory_size_flash;  /*< multiple of 4k */
} tl_wireless_fw_info_t;

typedef struct
{
    uint8_t *p_ble_spare_evt_buffer;
    uint8_t *p_sys_spare_evt_buffer;
    uint8_t *p_async_evt_pool;
    uint32_t async_evt_pool_size;
    uint8_t *p_traces_evt_pool;
    uint32_t traces_evt_pool_size;
} tl_mm_conf_t;

/** CPU1 */
#define IPCC_BLE_CMD_CHANNEL 1
#define IPCC_SYSTEM_CMD_RSP_CHANNEL 2
#define IPCC_MM_RELEASE_BUFFER_CHANNEL 4
#define IPCC_HCI_ACL_DATA_CHANNEL 6

/** CPU2 */
#define IPCC_BLE_EVENT_CHANNEL 1
#define IPCC_SYSTEM_EVENT_CHANNEL 2
#define IPCC_TRACES_CHANNEL 4

#endif
