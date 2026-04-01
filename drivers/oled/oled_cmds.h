#ifndef HAL_OLED_CMDS_H
#define HAL_OLED_CMDS_H

#include <stdint.h>

#define OLED_SCREEN_W 128
#define OLED_SCREEN_H 64
#define OLED_PAGE_COUNT OLED_SCREEN_H / 8
#define OLED_CMD_RING_SZ 100
#define OLED_CMD_BUF_MAX_SZ 16   
#define OLED_FB_MAX_SZ 1040
#define OLED_MAX_RETRIES 4

typedef enum
{
    OLED_STATE_I2C_ERR,
    OLED_STATE_UNINITIALIZED,
    OLED_STATE_BUSY,
    OLED_STATE_IDLE,
} oled_state_t;

typedef enum
{
    OLED_CTRL_CMD_ONLY = 0x00,
    OLED_CTRL_DATA_ONLY = 0x40,
    OLED_CTRL_CMD = 0x80,
    OLED_CTRL_DATA = 0xC0
} oled_ctrl_t;

typedef enum
{
    OLED_CMD_SET_CONTRAST = 0x81,
    OLED_CMD_DISPLAY_GDDRAM = 0xA4,
    OLED_CMD_ENTIRE_DISPLAY_ON = 0xA5,
    OLED_CMD_NORMAL_DISPLAY = 0xA6,
    OLED_CMD_INVERSE_DISPLAY = 0xA7,
    OLED_CMD_DISPLAY_OFF = 0xAE,
    OLED_CMD_DISPLAY_ON = 0xAF,

    OLED_CMD_HRSCROLL = 0x26,  // 0x00, PG_START, SCROLL_STEP, PG_END, 0xFF
    OLED_CMD_HLSCROLL = 0x27,  // ...
    OLED_CMD_DEACTIVATE_SCROOL = 0x2E,
    OLED_CMD_ACTIVATE_SCROOL = 0x2F,

    OLED_CMD_ADDR_MODE = 0x20,     // 0x00 for horizontal
    OLED_CMD_SET_COL_ADDR = 0x21,  // start, end
    OLED_CMD_SET_PG_ADDR = 0x22,   // start, end

    OLED_CMD_CHARGE_PUMP = 0x8D,
    OLED_CMD_DIS_PUMP = 0x10,
    OLED_CMD_EN_PUMP = 0x14,

    OLED_CMD_CLOCK_DIVIDE = 0xD5,
    OLED_CMD_START_LINE = 0x40,
    OLED_CMD_SEG_REMAP = 0xA0,
    OLED_CMD_SET_MUX_RATIO = 0xA8,
    OLED_CMD_SCAN_DIR = 0xC0,
    OLED_CMD_SET_OFFSET = 0xD3,
    OLED_CMD_SET_COM_PINS = 0xDA,
    OLED_CMD_SET_PRECHARGE_PERIOD = 0xD9,
    OLED_CMD_SET_COMH_DESELECT = 0xDB
} oled_cmd_t;

typedef struct 
{
    uint8_t fb[OLED_FB_MAX_SZ];
    uint8_t end_page;
    uint8_t cmd_len;
} oled_fb_t;

typedef struct
{
    uint8_t buf[OLED_CMD_BUF_MAX_SZ];
    uint32_t len;
} oled_cmd_buf_t;

// Ring buffer of commands that needs to transmitted
typedef struct 
{
    oled_cmd_buf_t queue[OLED_CMD_RING_SZ];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} oled_cmd_ring_t;

#endif
