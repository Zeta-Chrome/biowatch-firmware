#include "shci.h"
#include "ble/tl/tl.h"
#include "ble/tl/tl_defs.h"
#include "hal/reg.h"
#include "rtos/task/task.h"
#include "utils/containers/stm_clist.h"

static sys_evt_callback_t g_shci_evt_callback;
static stm_list_node_t shci_async_evt_queue;
static task_handle_t g_shci_evt_task_h;

static void tl_sys_evt_received(tl_evt_packet_t *shcievt);
static void shci_evt_handler_task(void *user_data);

void shci_init(tl_cmd_packet_t *pcmd_buffer, sys_cmd_callback_t shci_cmd_callback,
               sys_evt_callback_t shci_evt_callback)
{
    g_shci_evt_callback = shci_evt_callback;
    tl_sys_init(pcmd_buffer, shci_cmd_callback, tl_sys_evt_received);
    rtos_task_create(shci_evt_handler_task, "_SHCI_EVT_HANDLER", 3, 256, NULL, &g_shci_evt_task_h);
}

static void tl_sys_evt_received(tl_evt_packet_t *shcievt)
{
    stm_list_insert_tail(&shci_async_evt_queue, (stm_list_node_t *)shcievt);

    rtos_task_notify_from_isr(g_shci_evt_task_h, 0, NOTIFY_ACTION_NONE);
}

static void shci_evt_handler_task(void *user_data)
{
    tl_evt_packet_t *p_hci_evt_buf;

    while (true)
    {
        rtos_task_notify_wait(0, 0, NULL, MAX_TIMEOUT);

        while (!stm_list_is_empty(&shci_async_evt_queue))
        {
            stm_list_remove_head(&shci_async_evt_queue, (stm_list_node_t **)&p_hci_evt_buf);
            g_shci_evt_callback(p_hci_evt_buf);
            tl_mm_evt_done(p_hci_evt_buf);
        }
    }
}

void shci_get_wireless_info(tl_wireless_fw_info_t *p_wireless_info)
{
    uint32_t ipccdba = 0;
    tl_ref_t *p_RefTable = NULL;
    uint32_t wireless_firmware_version = 0;
    uint32_t wireless_firmware_memory_size = 0;
    uint32_t wireless_firmware_info_stack = 0;
    tl_fus_device_info_t *p_fus_device_info_table = NULL;
    uint32_t fus_version = 0;
    uint32_t fus_memorySize = 0;

    ipccdba = reg_get_bit(&FLASH->IPCCBR, FLASH_IPCCBR_IPCCDBA_Pos);

    p_fus_device_info_table = (tl_fus_device_info_t *)(*(uint32_t *)((ipccdba << 2) + SRAM2A_BASE));

    if (p_fus_device_info_table->device_info_table_state == FUS_DEVICE_INFO_TABLE_VALIDITY_KEYWORD)
    {
        wireless_firmware_version = p_fus_device_info_table->wireless_stack_version;
        wireless_firmware_memory_size = p_fus_device_info_table->wireless_stack_memory_size;
        wireless_firmware_info_stack = p_fus_device_info_table->wireless_firmware_ble_info;

        fus_version = p_fus_device_info_table->fus_version;
        fus_memorySize = p_fus_device_info_table->fus_memory_size;
    }
    else
    {
        p_RefTable = (tl_ref_t *)((ipccdba << 2) + SRAM2A_BASE);

        wireless_firmware_version = p_RefTable->p_device_info_table->wireless_fw_info_table.version;
        wireless_firmware_memory_size = p_RefTable->p_device_info_table->wireless_fw_info_table
                                        .memory_size;
        wireless_firmware_info_stack = p_RefTable->p_device_info_table->wireless_fw_info_table
                                       .info_stack;

        fus_version = p_RefTable->p_device_info_table->fus_info_table.version;
        fus_memorySize = p_RefTable->p_device_info_table->fus_info_table.memory_size;
    }

    p_wireless_info->version_major = ((wireless_firmware_version & INFO_VERSION_MAJOR_MASK) >>
                                      INFO_VERSION_MAJOR_OFFSET);
    p_wireless_info->version_minor = ((wireless_firmware_version & INFO_VERSION_MINOR_MASK) >>
                                      INFO_VERSION_MINOR_OFFSET);
    p_wireless_info->version_sub = ((wireless_firmware_version & INFO_VERSION_SUB_MASK) >>
                                    INFO_VERSION_SUB_OFFSET);
    p_wireless_info->version_branch = ((wireless_firmware_version & INFO_VERSION_BRANCH_MASK) >>
                                       INFO_VERSION_BRANCH_OFFSET);
    p_wireless_info->version_release_type = ((wireless_firmware_version & INFO_VERSION_TYPE_MASK) >>
                                             INFO_VERSION_TYPE_OFFSET);

    p_wireless_info->memory_size_sram2b = ((wireless_firmware_memory_size & INFO_SIZE_SRAM2B_MASK) >>
                                           INFO_SIZE_SRAM2B_OFFSET);
    p_wireless_info->memory_size_sram2a = ((wireless_firmware_memory_size & INFO_SIZE_SRAM2A_MASK) >>
                                           INFO_SIZE_SRAM2A_OFFSET);
    p_wireless_info->memory_size_sram1 = ((wireless_firmware_memory_size & INFO_SIZE_SRAM1_MASK) >>
                                          INFO_SIZE_SRAM1_OFFSET);
    p_wireless_info->memory_size_flash = ((wireless_firmware_memory_size & INFO_SIZE_FLASH_MASK) >>
                                          INFO_SIZE_FLASH_OFFSET);

    p_wireless_info->stack_type = ((wireless_firmware_info_stack & INFO_STACK_TYPE_MASK) >>
                                   INFO_STACK_TYPE_OFFSET);

    p_wireless_info->fus_version_major = ((fus_version & INFO_VERSION_MAJOR_MASK) >>
                                          INFO_VERSION_MAJOR_OFFSET);
    p_wireless_info->fus_version_minor = ((fus_version & INFO_VERSION_MINOR_MASK) >>
                                          INFO_VERSION_MINOR_OFFSET);
    p_wireless_info->fus_version_sub = ((fus_version & INFO_VERSION_SUB_MASK) >>
                                        INFO_VERSION_SUB_OFFSET);

    p_wireless_info->fus_memory_size_sram2b = ((fus_memorySize & INFO_SIZE_SRAM2B_MASK) >>
                                               INFO_SIZE_SRAM2B_OFFSET);
    p_wireless_info->fus_memory_size_sram2a = ((fus_memorySize & INFO_SIZE_SRAM2A_MASK) >>
                                               INFO_SIZE_SRAM2A_OFFSET);
    p_wireless_info->fus_memory_size_flash = ((fus_memorySize & INFO_SIZE_FLASH_MASK) >>
                                              INFO_SIZE_FLASH_OFFSET);
}

void shci_send(uint16_t cmd_code, uint8_t len_cmd_payload, uint8_t *p_cmd_payload,
               tl_evt_packet_t *p_rsp)
{
}
