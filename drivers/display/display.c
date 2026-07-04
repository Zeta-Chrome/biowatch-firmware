#include "display.h"
#include "display_cmds.h"
#include "hal/i2c/i2c.h"
#include "rtos/sync/event.h"
#include "utils/containers/queue.h"
#include "utils/status.h"
#include "utils/utils.h"
#include <string.h>

#define DISPLAY_ADDR 0x3C
#define MAX_RETRY_COUNTER 2
#define EVENT_INIT_SUCCESS BIT(0)
#define EVENT_INIT_FAILURE BIT(1)
#define EVENT_FLUSH_SUCCESS BIT(2)
#define EVENT_FLUSH_FAILURE BIT(3)
#define EVENT_CMD_FLUSH_SUCCESS BIT(4)
#define EVENT_CMD_FLUSH_FAILURE BIT(5)

// clang-format off
static display_fb_t g_fb = {
.buf = {
DISPLAY_CTRL_CMD_ONLY,      
DISPLAY_CMD_DISPLAY_OFF, 
DISPLAY_CMD_CLOCK_DIVIDE, 0x80,
DISPLAY_CMD_SET_MUX_RATIO, 0x3F,
DISPLAY_CMD_SET_OFFSET, 0x0,
DISPLAY_CMD_START_LINE,
DISPLAY_CMD_SEG_REMAP_START_0,
DISPLAY_CMD_SCAN_DIR_NORMAL,
DISPLAY_CMD_SET_COM_PINS, 0x12,
DISPLAY_CMD_SET_CONTRAST, 0x7F,
DISPLAY_CMD_DISPLAY_GDDRAM,
DISPLAY_CMD_NORMAL_DISPLAY, 
DISPLAY_CMD_SET_PRECHARGE_PERIOD, 0xF1,
DISPLAY_CMD_SET_COMH_DESELECT, 0x40,
DISPLAY_CMD_CHARGE_PUMP, DISPLAY_CMD_EN_PUMP,
DISPLAY_CMD_ADDR_MODE, 0x0,
DISPLAY_CMD_DISPLAY_ON 
},
.cmd_len = 26, // 26 Init commands
.num_pages = 0
};
// clang-format off
static event_t g_event;
static i2c_handle_t g_i2c_h;
static display_cmd_buf_t g_cmd_queue_buf[DISPLAY_CMD_RING_SZ]; 
static queue_t g_cmd_queue;
static uint8_t g_retry_counter = 0;
static uint8_t g_flush_pending = false;
static display_state_t g_state = DISPLAY_STATE_UNINITIALIZED;

static void on_initialized(bw_status_t status, void* user_data)
{
    (void)user_data;
    if (status != STATUS_OK)
    {
        rtos_event_set_from_isr(&g_event, EVENT_INIT_FAILURE);  
    }
    else 
    {
        rtos_event_set_from_isr(&g_event, EVENT_INIT_SUCCESS); 
    }
}

static void init_fb_cmds()
{
    // Initialize fb commands
    g_fb.cmd_len = 0;

    // clang-format off
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_CMD; g_fb.buf[g_fb.cmd_len++] = DISPLAY_CMD_SET_COL_ADDR;
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_CMD; g_fb.buf[g_fb.cmd_len++] = 0;
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_CMD; g_fb.buf[g_fb.cmd_len++] = 127;
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_CMD; g_fb.buf[g_fb.cmd_len++] = DISPLAY_CMD_SET_PG_ADDR;
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_CMD; g_fb.buf[g_fb.cmd_len++] = 0;
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_CMD; g_fb.buf[g_fb.cmd_len++] = 0; 
    g_fb.buf[g_fb.cmd_len++] = DISPLAY_CTRL_DATA_ONLY;
    // clang-format on 
}

void display_init(bool invert_x, bool invert_y)
{
    g_state = DISPLAY_STATE_UNINITIALIZED;
    
    queue_init(&g_cmd_queue, g_cmd_queue_buf, sizeof(g_cmd_queue_buf[0]), DISPLAY_CMD_RING_SZ);
    rtos_event_init(&g_event);

    g_fb.buf[9] = invert_x ? DISPLAY_CMD_SEG_REMAP_START_127 : DISPLAY_CMD_SEG_REMAP_START_0;
    g_fb.buf[10] = invert_y ? DISPLAY_CMD_SCAN_DIR_REMAPPED : DISPLAY_CMD_SCAN_DIR_NORMAL;
    
    while (g_state != DISPLAY_STATE_READY)
    {
        // DISPLAY initialization
        g_i2c_h.addr = DISPLAY_ADDR;
        g_i2c_h.buf = g_fb.buf;
        g_i2c_h.len = g_fb.cmd_len;
        g_i2c_h.callback = on_initialized;
        hal_i2c_transmit_dma(&g_i2c_h); 
    
        uint32_t event_bit;
        bw_status_t status = rtos_event_wait(&g_event, EVENT_INIT_SUCCESS | EVENT_INIT_FAILURE, &event_bit, true, false, 1000);
        if (status != STATUS_OK)
        {
            BW_LOG("display exited with status: %d\n", status);
            event_bit = EVENT_INIT_FAILURE;
        }

        if (event_bit & EVENT_INIT_SUCCESS)
        {
            g_state = DISPLAY_STATE_READY;
            g_retry_counter = 0;
            init_fb_cmds();
        }
        else if (event_bit & EVENT_INIT_FAILURE)
        {
            if (g_retry_counter++ < MAX_RETRY_COUNTER)
            {
                hal_i2c_reset_dma(&g_i2c_h);
                continue;
            }
            g_state = DISPLAY_STATE_I2C_ERR;
        }
    }
}

display_state_t display_get_state()
{
    return g_state;
}

i2c_handle_t *display_get_i2c_handle()
{
    return &g_i2c_h;
}

static void on_cmd_flushed(bw_status_t status, void *user_data)
{
    (void)user_data;
    if (status != STATUS_OK)
    {
        rtos_event_set_from_isr(&g_event, EVENT_CMD_FLUSH_FAILURE); 
    }
    else
    {
        rtos_event_set_from_isr(&g_event, EVENT_CMD_FLUSH_SUCCESS); 
    } 
}

static void on_fb_flushed(bw_status_t status, void* user_data)
{
    (void)user_data;
    if (status != STATUS_OK)
    {
        rtos_event_set_from_isr(&g_event, EVENT_FLUSH_FAILURE); 
    }
    else
    {
        rtos_event_set_from_isr(&g_event, EVENT_FLUSH_SUCCESS); 
    }
}

static void flush_cmd()
{
    display_cmd_buf_t *cmd_buf;
    uint32_t event_bit;
    uint32_t wait_evts = EVENT_CMD_FLUSH_SUCCESS | EVENT_CMD_FLUSH_FAILURE;

    while (!is_queue_empty(&g_cmd_queue) && g_state == DISPLAY_STATE_READY)
    {
        queue_peek(&g_cmd_queue, (void**)&cmd_buf);

        g_i2c_h.addr = DISPLAY_ADDR;
        g_i2c_h.buf = cmd_buf->buf;
        g_i2c_h.len = cmd_buf->len;
        g_i2c_h.callback = on_cmd_flushed;
        hal_i2c_transmit_dma(&g_i2c_h);

        bw_status_t status = rtos_event_wait(&g_event, wait_evts, &event_bit, true, false, 1000);
        if (status != STATUS_OK)
        {
            BW_LOG("display exited with status: %d\n", status);
            event_bit = EVENT_CMD_FLUSH_FAILURE;
        }

        if (event_bit & EVENT_CMD_FLUSH_SUCCESS)
        {
            g_state = DISPLAY_STATE_READY; 
            queue_pop(&g_cmd_queue, NULL);
            g_retry_counter = 0;
         }
        else if (event_bit & EVENT_CMD_FLUSH_FAILURE)
        {
            if (g_retry_counter++ < MAX_RETRY_COUNTER)
            {
                hal_i2c_reset_dma(&g_i2c_h);
                continue;
            }
            g_state = DISPLAY_STATE_I2C_ERR;
        }
    }
}

void display_power_on()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    display_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void**)&cmd_buf);

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_DISPLAY_ON;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_CHARGE_PUMP;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_EN_PUMP;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void display_power_off()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    display_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void**)&cmd_buf); 

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_DISPLAY_OFF;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_CHARGE_PUMP;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_DIS_PUMP;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void display_normal()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    display_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void**)&cmd_buf); 

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_NORMAL_DISPLAY;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void display_inverse()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    display_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void**)&cmd_buf); 

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_INVERSE_DISPLAY;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void display_set_brightness(uint8_t value)
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    display_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void**)&cmd_buf); 
    
    uint8_t phase2    = 1 + ((uint16_t)value * 14) / 255;
    uint8_t precharge = (phase2 << 4) | 0x01; 

    uint8_t vcomh;
    if (value < 85)
    {
        vcomh = 0x00;
    }   
    else if (value < 170)
    {
        vcomh = 0x20;
    }   
    else       
    {
        vcomh = 0x30;
    }   

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_SET_PRECHARGE_PERIOD;
    cmd_buf->buf[cmd_buf->len++] = precharge;
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_SET_COMH_DESELECT;
    cmd_buf->buf[cmd_buf->len++] = vcomh; 
    cmd_buf->buf[cmd_buf->len++] = DISPLAY_CMD_SET_CONTRAST;
    cmd_buf->buf[cmd_buf->len++] = value;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void display_clear_screen()
{
    if (g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    g_flush_pending = true;
    g_fb.num_pages = 8;
    memset(g_fb.buf + g_fb.cmd_len, 0x0, DISPLAY_FB_SZ);
}

static void orr_page_region_fill(uint8_t page, uint8_t scol, uint8_t ecol, uint8_t mask, uint8_t value)
{
    size_t idx;
    for (size_t col = scol; col <= ecol; col++)
    {
        idx = g_fb.cmd_len + page * DISPLAY_SCREEN_W + col;
        g_fb.buf[idx] = (g_fb.buf[idx] & ~mask) | (value & mask);
    }
}

void display_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t value)
{
    BW_ASSERT(x < 128 && y < 64 && w <= 128 && w > 0 && h <= 64 && h > 0, "Invalid x:%d, y:%d, w:%d, h:%d ", x, y, w, h);

    if (g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    g_flush_pending = true;

    uint8_t spage = y / 8;
    uint8_t epage = (y + h - 1) / 8;
    g_fb.num_pages = MAX(g_fb.num_pages, epage + 1);

    if (epage == spage)
    {
        uint8_t mask = MASK(h, y % 8);
        orr_page_region_fill(spage, x, x + w - 1, mask, value);
        return;
    }

    if (y % 8 != 0)
    {
        uint8_t mask = 0xFF << (y % 8);
        orr_page_region_fill(spage, x, x + w - 1, mask, value);
    }

    uint8_t *buf = g_fb.buf + g_fb.cmd_len + ((y + 7) / 8) * DISPLAY_SCREEN_W + x;
    for (int i = ((y + 7)/ 8); i <= ((y + h) / 8 - 1); i++)
    {
        memset(buf, value, w);
        buf += DISPLAY_SCREEN_W;
    }

    if ((y + h) % 8 != 0)
    {
        uint8_t mask = 0xFF >> (8 - (y + h) % 8);
        orr_page_region_fill(epage, x, x + w - 1, mask, value);
    }
}

void display_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *data)
{
    BW_ASSERT(x < 128 && y < 64 && w <= 128 && w > 0 && h <= 64 && h > 0, "Invalid x:%d, y:%d, w:%d, h:%d ", x, y, w, h);

    if (g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    size_t idx, i = 0;
    g_flush_pending = true;

    uint8_t offset = y % 8;
    uint8_t spage = y / 8;
    uint8_t epage = (y + h - 1) / 8;
    g_fb.num_pages = MAX(g_fb.num_pages, epage + 1);

    if (epage == spage)
    {
        uint8_t mask = MASK(h, offset);
        for (size_t col = x; col <= x + w - 1; col++)
        {
            idx = g_fb.cmd_len + spage * DISPLAY_SCREEN_W + col;
            g_fb.buf[idx] = (g_fb.buf[idx] & ~mask) | ((data[i] << offset) & mask);
            i++;
        }

        return;
    }

    if (y % 8 != 0)
    {
        uint8_t mask = 0xFF << offset;
        for (size_t col = x; col <= x + w - 1; col++)
        {
            idx = g_fb.cmd_len + spage * DISPLAY_SCREEN_W + col;
            g_fb.buf[idx] = (g_fb.buf[idx] & ~mask) | (data[i] << offset);
            i++;
        }
    }

    for (int page = ((y + 7)/ 8); page <= ((y + h) / 8 - 1); page++)
    {
        for (size_t col = x; col <= x + w - 1; col++)
        {
            idx = g_fb.cmd_len + page * DISPLAY_SCREEN_W + col;
            g_fb.buf[idx] = (data[i - w] >> (8 - offset)) | (data[i] << offset);
            i++;
        }
    }

    if ((y + h) % 8 != 0)
    {
        uint8_t mask = 0xFF >> (8 - (y + h) % 8);
        for (size_t col = x; col <= x + w - 1; col++)
        {
            idx = g_fb.cmd_len + epage * DISPLAY_SCREEN_W + col;
            g_fb.buf[idx] = (((data[i - w] >> (8 - offset)) | (data[i] << offset)) & mask) | (g_fb.buf[idx] & ~mask);
            i++;
        }
    }
}

static void orr_page_region_invert(uint8_t page, uint8_t scol, uint8_t ecol, uint8_t mask)
{
    size_t idx;
    for (size_t col = scol; col <= ecol; col++)
    {
        idx = g_fb.cmd_len + page * DISPLAY_SCREEN_W + col;
        g_fb.buf[idx] = (g_fb.buf[idx] & ~mask) | (~g_fb.buf[idx] & mask);
    }
}

void display_region_invert(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    BW_ASSERT(x < 128 && y < 64 && w <= 128 && w > 0 && h <= 64 && h > 0, "Invalid x:%d, y:%d, w:%d, h:%d ", x, y, w, h);

    if (g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    g_flush_pending = true;

    uint8_t spage = y / 8;
    uint8_t epage = (y + h - 1) / 8;
    g_fb.num_pages = MAX(g_fb.num_pages, epage + 1);

    if (epage == spage)
    {
        uint8_t mask = MASK(h, y % 8);
        orr_page_region_invert(spage, x, x + w - 1, mask);
        return;
    }

    if (y % 8 != 0)
    {
        uint8_t mask = 0xFF << (y % 8);
        orr_page_region_invert(spage, x, x + w - 1, mask);
    }

    for (int page = ((y + 7)/ 8); page <= ((y + h) / 8 - 1); page++)
    {
        orr_page_region_invert(page, x, x + w - 1, 0xFF);
    }

    if ((y + h) % 8 != 0)
    {
        uint8_t mask = 0xFF >> (8 - (y + h) % 8);
        orr_page_region_invert(epage, x, x + w - 1, mask);
    }

}

void display_flush()
{
    if (!g_flush_pending || g_fb.num_pages == 0 || g_state <= DISPLAY_STATE_I2C_ERR)
    {
        return;
    }

    g_fb.buf[11] = g_fb.num_pages - 1;

    g_i2c_h.addr = DISPLAY_ADDR;
    g_i2c_h.buf = g_fb.buf;
    g_i2c_h.len = g_fb.cmd_len + g_fb.num_pages * DISPLAY_SCREEN_W;
    g_i2c_h.callback = on_fb_flushed;
    hal_i2c_transmit_dma(&g_i2c_h);
    
    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_FLUSH_SUCCESS | EVENT_FLUSH_FAILURE, &event_bit, true, false, 1000);
    if (status != STATUS_OK)
    {
        BW_LOG("display exited with status: %d\n", status);
        event_bit = EVENT_FLUSH_FAILURE;
    }

    if (event_bit & EVENT_FLUSH_SUCCESS)
    {
        g_state = DISPLAY_STATE_READY;
        g_flush_pending = false;
        g_fb.num_pages = 0;
        flush_cmd(); // flush next command if available
    }
    else if (event_bit & EVENT_FLUSH_FAILURE)
    {
        g_flush_pending = true;
        if (g_retry_counter++ < MAX_RETRY_COUNTER)
        {
            BW_LOG("Display flush failed");
            hal_i2c_reset_dma(&g_i2c_h);
        }
        else
        {
            g_state = DISPLAY_STATE_I2C_ERR;
        }
    }
}
