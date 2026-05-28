#ifndef BLE_TL_H
#define BLE_TL_H

#include "ble/tl/tl_defs.h"
#include "hal/ipcc/ipcc.h"
#include "utils/utils.h"

#define FUS_DEVICE_INFO_TABLE_VALIDITY_KEYWORD    (0xA94656B9)

/* Field Version */
#define INFO_VERSION_MAJOR_OFFSET                   24
#define INFO_VERSION_MAJOR_MASK                     0xff000000
#define INFO_VERSION_MINOR_OFFSET                   16
#define INFO_VERSION_MINOR_MASK                     0x00ff0000
#define INFO_VERSION_SUB_OFFSET                     8
#define INFO_VERSION_SUB_MASK                       0x0000ff00
#define INFO_VERSION_BRANCH_OFFSET                  4
#define INFO_VERSION_BRANCH_MASK                    0x0000000f0
#define INFO_VERSION_TYPE_OFFSET                    0
#define INFO_VERSION_TYPE_MASK                      0x00000000f
#define INFO_VERSION_TYPE_RELEASE                   1

/* Field Memory */
#define INFO_SIZE_SRAM2B_OFFSET                     24
#define INFO_SIZE_SRAM2B_MASK                       0xff000000
#define INFO_SIZE_SRAM2A_OFFSET                     16
#define INFO_SIZE_SRAM2A_MASK                       0x00ff0000
#define INFO_SIZE_SRAM1_OFFSET                      8
#define INFO_SIZE_SRAM1_MASK                        0x0000ff00
#define INFO_SIZE_FLASH_OFFSET                      0
#define INFO_SIZE_FLASH_MASK                        0x000000ff

/* Field stack information */
#define INFO_STACK_TYPE_OFFSET                      0
#define INFO_STACK_TYPE_MASK                        0x000000ff
#define INFO_STACK_TYPE_NONE                        0
#define INFO_STACK_TYPE_BLE_FULL                    0x01
#define INFO_STACK_TYPE_BLE_HCI                     0x02
#define INFO_STACK_TYPE_BLE_LIGHT                   0x03
#define INFO_STACK_TYPE_BLE_BEACON                  0x04
#define INFO_STACK_TYPE_BLE_BASIC                   0x05
#define INFO_STACK_TYPE_BLE_FULL_EXT_ADV            0x06
#define INFO_STACK_TYPE_BLE_HCI_EXT_ADV             0x07
#define INFO_STACK_TYPE_THREAD_FTD                  0x10
#define INFO_STACK_TYPE_THREAD_MTD                  0x11
#define INFO_STACK_TYPE_ZIGBEE_FFD                  0x30
#define INFO_STACK_TYPE_ZIGBEE_RFD                  0x31
#define INFO_STACK_TYPE_MAC                         0x40
#define INFO_STACK_TYPE_BLE_THREAD_FTD_STATIC       0x50
#define INFO_STACK_TYPE_BLE_THREAD_FTD_DYNAMIC      0x51
#define INFO_STACK_TYPE_BLE_THREAD_LIGHT_DYNAMIC    0x52
#define INFO_STACK_TYPE_802154_LLD_TESTS            0x60
#define INFO_STACK_TYPE_802154_PHY_VALID            0x61
#define INFO_STACK_TYPE_BLE_PHY_VALID               0x62
#define INFO_STACK_TYPE_BLE_LLD_TESTS               0x63
#define INFO_STACK_TYPE_BLE_RLV                     0x64
#define INFO_STACK_TYPE_802154_RLV                  0x65
#define INFO_STACK_TYPE_BLE_ZIGBEE_FFD_STATIC       0x70
#define INFO_STACK_TYPE_BLE_ZIGBEE_RFD_STATIC       0x71
#define INFO_STACK_TYPE_BLE_ZIGBEE_FFD_DYNAMIC      0x78
#define INFO_STACK_TYPE_BLE_ZIGBEE_RFD_DYNAMIC      0x79
#define INFO_STACK_TYPE_RLV                         0x80
#define INFO_STACK_TYPE_BLE_MAC_STATIC              0x90
#define INFO_STACK_TYPE_NVM_BACKUP                  0xF0
#define INFO_STACK_TYPE_NVM_RESTORE                 0xF1
#define TL_BLECMD_PKT_TYPE (0x01)
#define TL_ACL_DATA_PKT_TYPE (0x02)
#define TL_BLEEVT_PKT_TYPE (0x04)
#define TL_SYSCMD_PKT_TYPE (0x10)
#define TL_SYSRSP_PKT_TYPE (0x11)
#define TL_SYSEVT_PKT_TYPE (0x12)
#define TL_TRACES_APP_PKT_TYPE (0x40)
#define TL_TRACES_WL_PKT_TYPE (0x41)

#define TL_CMD_HDR_SIZE                (4)
#define TL_EVT_HDR_SIZE                (3)
#define TL_EVT_CS_PAYLOAD_SIZE         (4)

#define TL_BLEEVT_CC_OPCODE (0x0E)
#define TL_BLEEVT_CS_OPCODE (0x0F)
#define TL_BLEEVT_VS_OPCODE (0xFF)

typedef PACKED_STRUCT
{
    uint32_t *next;
    uint32_t *prev;
}
tl_packet_header_t;

typedef PACKED_STRUCT
{
    uint8_t status;
    uint8_t num_cmd;
    uint16_t cmd_code;
}
tl_cs_evt_t;

typedef PACKED_STRUCT
{
    uint8_t num_cmd;
    uint16_t cmd_code;
    uint8_t payload[2];
}
tl_cc_evt_t;

typedef PACKED_STRUCT
{
    uint16_t sub_evt_code;
    uint8_t payload[2];
}
tl_async_evt_t;

typedef PACKED_STRUCT
{
    uint8_t evt_code;
    uint8_t plen;
    uint8_t payload[2];
}
tl_evt_t;

typedef PACKED_STRUCT
{
    uint8_t type;
    tl_evt_t evt;
}
tl_evt_serial_t;

typedef PACKED_STRUCT
{
    tl_packet_header_t header;
    tl_evt_serial_t evt_serial;
}
tl_evt_packet_t;

typedef PACKED_STRUCT
{
    uint16_t cmd_code;
    uint8_t plen;
    uint8_t payload[255];
}
tl_cmd_t;

typedef PACKED_STRUCT
{
    uint8_t type;
    tl_cmd_t cmd;
}
tl_cmd_serial_t;

typedef PACKED_STRUCT
{
    tl_packet_header_t header;
    tl_cmd_serial_t cmd_serial;
}
tl_cmd_packet_t;

typedef PACKED_STRUCT
{
    uint8_t type;
    uint16_t handle;
    uint16_t length;
    uint8_t acl_data[1];
}
tl_acl_data_serial_t;

typedef PACKED_STRUCT
{
    tl_packet_header_t header;
    tl_acl_data_serial_t AclDataSerial;
}
tl_acl_data_packet_t;

typedef void (*sys_evt_callback_t)(tl_evt_packet_t *);
typedef void (*sys_cmd_callback_t)();

typedef struct
{
    sys_cmd_callback_t sys_cmd_callback;
    sys_evt_callback_t sys_evt_callback;
    uint8_t *pcmd_buffer;
} tl_sys_conf_t;

typedef struct
{
    ipcc_callback_t ble_evt_callback;
    ipcc_callback_t acl_ack_callback;
    uint8_t *pcmd_buffer;
    uint8_t *pacl_data_buffer;
} tl_ble_conf_t;

void tl_init();
void tl_sys_init(tl_cmd_packet_t *pcmd_buffer, sys_cmd_callback_t sys_cmd_callback,
                 sys_evt_callback_t sys_evt_callback);
void tl_sys_send_cmd();
void tl_mm_init(tl_mm_conf_t *conf);
void tl_mm_evt_done(tl_evt_packet_t *p_hci_evt);

#endif
