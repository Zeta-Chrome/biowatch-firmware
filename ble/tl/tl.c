#include "tl.h"
#include "hal/ipcc/ipcc.h"
#include "tl_defs.h"
#include "utils/containers/stm_clist.h"
#include "utils/utils.h"

PLACE_IN_SECTION("MAPPING_TABLE") static volatile tl_ref_t tl_ref_table;
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static tl_device_info_t tl_device_info_table;
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static tl_ble_t tl_ble_table;
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static tl_sys_t tl_sys_table;
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static tl_mem_manager_t tl_mem_manager_table;
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static tl_traces_t tl_traces_table;

PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static stm_list_node_t sys_evt_queue;
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static stm_list_node_t free_buf_queue;

static stm_list_node_t g_local_free_buf_queue;
static ipcc_handle_t g_ipcc_sys_evt_h;
static ipcc_handle_t g_ipcc_sys_cmd_h;
static ipcc_handle_t g_ipcc_mm_release_h;
static tl_sys_conf_t g_sys_conf;

static void tl_sys_evt_callback(void *user_data);
static void tl_sys_cmd_callback(void *user_data);
static void tl_mm_release_callback(void *user_data);
static void send_free_buffer();

void tl_init()
{
    tl_ref_table.p_device_info_table = &tl_device_info_table;
    tl_ref_table.p_ble_table = &tl_ble_table;
    tl_ref_table.p_sys_table = &tl_sys_table;
    tl_ref_table.p_mem_manager_table = &tl_mem_manager_table;
    tl_ref_table.p_traces_table = &tl_traces_table;

    hal_ipcc_init(5, 5);
}

void tl_sys_init(tl_cmd_packet_t *pcmd_buffer, sys_cmd_callback_t sys_cmd_callback,
                 sys_evt_callback_t sys_evt_callback)
{
    stm_list_init(&sys_evt_queue);

    g_sys_conf.pcmd_buffer = (uint8_t *)pcmd_buffer;
    g_sys_conf.sys_cmd_callback = sys_cmd_callback;
    g_sys_conf.sys_evt_callback = sys_evt_callback;

    tl_sys_table.pcmd_buffer = (uint8_t *)pcmd_buffer;
    tl_sys_table.sys_queue = (uint8_t *)&sys_evt_queue;

    g_ipcc_sys_evt_h.callback = tl_sys_evt_callback;
    hal_ipcc_rx(IPCC_SYSTEM_EVENT_CHANNEL, &g_ipcc_sys_evt_h);
}

static void tl_sys_evt_callback(void *user_data)
{
    tl_evt_packet_t *pevt;

    while (!stm_list_is_empty(&sys_evt_queue))
    {
        stm_list_remove_head(&sys_evt_queue, (stm_list_node_t **)&pevt);
        // Debug trace
        g_sys_conf.sys_evt_callback(pevt);
    }
}

void tl_sys_send_cmd()
{
    ((tl_cmd_packet_t *)(tl_ref_table.p_sys_table->pcmd_buffer))->cmd_serial.type = TL_SYSCMD_PKT_TYPE;

    g_ipcc_sys_cmd_h.callback = tl_sys_cmd_callback;
    hal_ipcc_tx(IPCC_SYSTEM_CMD_RSP_CHANNEL, &g_ipcc_sys_cmd_h);
}

static void tl_sys_cmd_callback(void *user_data)
{
    (void)user_data;

    // Debug Trace
    g_sys_conf.sys_cmd_callback();
}

void tl_mm_init(tl_mm_conf_t *conf)
{
    stm_list_init(&free_buf_queue);
    stm_list_init(&g_local_free_buf_queue);
    g_ipcc_mm_release_h.callback = tl_mm_release_callback;

    tl_mem_manager_table.ble_pool = conf->p_async_evt_pool;
    tl_mem_manager_table.ble_pool_size = conf->async_evt_pool_size;
    tl_mem_manager_table.pevt_free_buffer_queue = (uint8_t *)&free_buf_queue;
    tl_mem_manager_table.spare_ble_buffer = conf->p_ble_spare_evt_buffer;
    tl_mem_manager_table.spare_sys_buffer = conf->p_sys_spare_evt_buffer;
    tl_mem_manager_table.traces_evt_pool = conf->p_traces_evt_pool;
    tl_mem_manager_table.traces_pool_size = conf->traces_evt_pool_size;
}

void tl_mm_evt_done(tl_evt_packet_t *p_hci_evt)
{
    stm_list_insert_tail(&g_local_free_buf_queue, (stm_list_node_t *)p_hci_evt);

    // Debug trace

    if (!hal_ipcc_is_tx_channel_occupied(IPCC_MM_RELEASE_BUFFER_CHANNEL))
    {
        send_free_buffer();
        hal_ipcc_tx(IPCC_MM_RELEASE_BUFFER_CHANNEL, &g_ipcc_mm_release_h);
    }
}

void tl_mm_release_callback(void *user_data)
{
    (void)user_data;

    if (!stm_list_is_empty(&g_local_free_buf_queue))
    {
        send_free_buffer();
        hal_ipcc_tx(IPCC_MM_RELEASE_BUFFER_CHANNEL, &g_ipcc_mm_release_h);
    }
}

static void send_free_buffer()
{
    stm_list_node_t *p_node;

    while (!stm_list_is_empty(&g_local_free_buf_queue))
    {
        stm_list_remove_head(&g_local_free_buf_queue, &p_node);
        stm_list_insert_tail(&free_buf_queue, p_node);
    }
}
