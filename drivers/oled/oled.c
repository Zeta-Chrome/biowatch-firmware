#include "oled.h"
#include "oled_cmds.h"
#include "platform.h"
#include "utils/containers/queue.h"
#include "utils/utils.h"
#include "utils/status.h"
#include "hal/i2c/i2c.h"
#include "rtos/sync/event.h"
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define OLED_ADDR 0x3C
#define EVENT_INIT_SUCCESS BIT(0)
#define EVENT_INIT_FAILURE BIT(1)
#define EVENT_FLUSH_SUCCESS BIT(2)
#define EVENT_FLUSH_FAILURE BIT(3)
#define EVENT_CMD_FLUSH_SUCCESS BIT(4)
#define EVENT_CMD_FLUSH_FAILURE BIT(5)

// clang-format off
static oled_fb_t g_oled_fb = {
.fb = {
OLED_CTRL_CMD_ONLY,      
OLED_CMD_DISPLAY_OFF, 
OLED_CMD_CLOCK_DIVIDE, 0x80,
OLED_CMD_SET_MUX_RATIO, 0x3F,
OLED_CMD_SET_OFFSET, 0x0,
OLED_CMD_START_LINE,
OLED_CMD_SEG_REMAP,
OLED_CMD_SCAN_DIR,
OLED_CMD_SET_COM_PINS, 0x12,
OLED_CMD_SET_CONTRAST, 0x7F,
OLED_CMD_DISPLAY_GDDRAM,
OLED_CMD_NORMAL_DISPLAY, 
OLED_CMD_SET_PRECHARGE_PERIOD, 0xF1,
OLED_CMD_SET_COMH_DESELECT, 0x40,
OLED_CMD_CHARGE_PUMP, OLED_CMD_EN_PUMP,
OLED_CMD_ADDR_MODE, 0x0,
OLED_CMD_DISPLAY_ON 
}, 
.end_page=0
};
// clang-format off

static event_t g_event;
static i2c_handle_t g_i2c_h;
static oled_cmd_buf_t g_cmd_queue_buf[OLED_CMD_RING_SZ]; 
static queue_t g_cmd_queue;
static bool g_i2c_initialized = false;
static oled_state_t g_state = OLED_STATE_UNINITIALIZED;
static bool g_fb_flush_pending = false;
static uint8_t g_retry_counter = 0;

static void on_initialized(bw_status_t status, void* user_data)
{
    (void)user_data;
    if (status == STATUS_I2C_ERR || status == STATUS_I2C_NACKF)
    {
        rtos_event_set_from_isr(&g_event, EVENT_INIT_FAILURE);  
    }
    else 
    {
        rtos_event_set_from_isr(&g_event, EVENT_INIT_SUCCESS); 
    }
}

void oled_init()
{
    g_state = OLED_STATE_UNINITIALIZED;
    
    if (!g_i2c_initialized)
    {
        // I2C initialization
        i2c_conf_t conf = {.sda = PL_OLED_SDA, .scl = PL_OLED_SCL, .af = GPIO_AF4, .i2c = PL_OLED_I2C, .speed = I2C_SPEED_FAST, .dnf = 0, .irq_priority = 4};
        hal_i2c_init(&conf, &g_i2c_h);
        g_i2c_initialized = true;
    }

    queue_init(&g_cmd_queue, g_cmd_queue_buf, sizeof(g_cmd_queue_buf[0]), OLED_CMD_RING_SZ);
    rtos_event_init(&g_event);

    // OLED initialization
    g_i2c_h.i2c = PL_OLED_I2C;
    g_i2c_h.addr = OLED_ADDR;
    g_i2c_h.buf = g_oled_fb.fb;
    g_i2c_h.len = 26; // 26 Init commands
    g_i2c_h.callback = on_initialized;
    hal_i2c_transmit(&g_i2c_h); 
    
    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_INIT_SUCCESS | EVENT_INIT_FAILURE, &event_bit, true, false, 1000);
    if (status != STATUS_OK)
    {
        BW_LOG("Exited with status: %d\n", status);
        event_bit = EVENT_INIT_FAILURE;
    }

    if (event_bit & EVENT_INIT_SUCCESS)
    {
        g_state = OLED_STATE_IDLE;
        g_retry_counter = 0;
    }
    else if (event_bit & EVENT_INIT_FAILURE)
    {
        hal_i2c_reset(&g_i2c_h);

        if (g_retry_counter < OLED_MAX_RETRIES)
        {
            BW_LOG("Init failed\n");
            g_retry_counter++;
            oled_init(); // reinit on failure
        }
        else 
        {
            g_state = OLED_STATE_I2C_ERR;
        }
    }
}

bool oled_is_initialized()
{
    return g_state > OLED_STATE_UNINITIALIZED;
}

bool oled_is_flushed()
{
    return g_state >= OLED_STATE_IDLE;
}

bool oled_bus_error()
{
    return g_state == OLED_STATE_I2C_ERR;
}

static void on_cmd_flushed(bw_status_t status, void *user_data)
{
    (void)user_data;
    if (status == STATUS_I2C_ERR || status == STATUS_I2C_NACKF)
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
    if (status == STATUS_I2C_ERR || status == STATUS_I2C_NACKF)
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
    // prevent race conditions
    if (is_queue_empty(&g_cmd_queue) || !oled_is_flushed())
    {
        return;
    }

    g_state = OLED_STATE_BUSY;

    oled_cmd_buf_t *cmd_buf;
    queue_peek(&g_cmd_queue, (void**)&cmd_buf);

    g_i2c_h.i2c = PL_OLED_I2C;
    g_i2c_h.addr = OLED_ADDR;
    g_i2c_h.buf = cmd_buf->buf;
    g_i2c_h.len = cmd_buf->len;
    g_i2c_h.callback = on_cmd_flushed;

    hal_i2c_transmit(&g_i2c_h);

    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_CMD_FLUSH_SUCCESS | EVENT_CMD_FLUSH_FAILURE, &event_bit, true, false, 1000);
    if (status != STATUS_OK)
    {
        BW_LOG("Exited with status: %d\n", status);
        event_bit = EVENT_CMD_FLUSH_FAILURE;
    }

    g_state = OLED_STATE_IDLE;
    if (event_bit & EVENT_CMD_FLUSH_SUCCESS)
    {
        g_state = OLED_STATE_IDLE; 
        g_retry_counter = 0;

        // Drain on completion
        queue_pop(&g_cmd_queue, NULL);
        flush_cmd(); // flush next command if available
        
        if(g_fb_flush_pending)
        {
            oled_flush();
        }
    }
    else if (event_bit & EVENT_CMD_FLUSH_FAILURE)
    {
        hal_i2c_reset(&g_i2c_h);
        if (g_retry_counter < OLED_MAX_RETRIES)
        {
            BW_LOG("Cmd Flush failed\n");
            g_retry_counter++;
            flush_cmd();
        }
        else 
        {
            g_state = OLED_STATE_I2C_ERR;
        }
    }
}

void oled_flush()
{
    // prevent race conditions
    if (!oled_is_flushed())
    {
        g_fb_flush_pending = true; 
        return;
    }

    g_state = OLED_STATE_BUSY;

    // Initialize fb commands
    g_oled_fb.cmd_len = 0;

    // clang-format off
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_CMD; g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CMD_SET_COL_ADDR;
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_CMD; g_oled_fb.fb[g_oled_fb.cmd_len++] = 0;
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_CMD; g_oled_fb.fb[g_oled_fb.cmd_len++] = 127;
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_CMD; g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CMD_SET_PG_ADDR;
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_CMD; g_oled_fb.fb[g_oled_fb.cmd_len++] = 0;
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_CMD; g_oled_fb.fb[g_oled_fb.cmd_len++] = g_oled_fb.end_page; 
    g_oled_fb.fb[g_oled_fb.cmd_len++] = OLED_CTRL_DATA_ONLY;
    // clang-format on 

    g_i2c_h.i2c = PL_OLED_I2C;
    g_i2c_h.addr = OLED_ADDR;
    g_i2c_h.buf = g_oled_fb.fb;
    g_i2c_h.len = g_oled_fb.cmd_len + (g_oled_fb.end_page + 1) * OLED_SCREEN_W;
    g_i2c_h.callback = on_fb_flushed;

    hal_i2c_transmit(&g_i2c_h);
    
    uint32_t event_bit;
    bw_status_t status = rtos_event_wait(&g_event, EVENT_FLUSH_SUCCESS | EVENT_FLUSH_FAILURE, &event_bit, true, false, 1000);
    if (status != STATUS_OK)
    {
        BW_LOG("Exited with status: %d\n", status);
        event_bit = EVENT_FLUSH_FAILURE;
    }

    g_state = OLED_STATE_IDLE;
    if (event_bit & EVENT_FLUSH_SUCCESS)
    {
        flush_cmd(); // flush next command if available

        if(g_fb_flush_pending)
        {
            oled_flush();
        }
    }
    else if (event_bit & EVENT_FLUSH_FAILURE)
    {
        hal_i2c_reset(&g_i2c_h);
        g_fb_flush_pending = true;

        BW_LOG("Flush failed");
    }

    // Now reset the end_page
    g_oled_fb.end_page = 0;
    g_fb_flush_pending = false;
}

void oled_power_on()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void**)&cmd_buf);

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_DISPLAY_ON;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_CHARGE_PUMP;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_EN_PUMP;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void oled_power_off()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void*)&cmd_buf); 

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_DISPLAY_OFF;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_CHARGE_PUMP;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_DIS_PUMP;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void oled_display_normal()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void*)&cmd_buf); 

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_NORMAL_DISPLAY;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void oled_display_inverse()
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void*)&cmd_buf); 

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_INVERSE_DISPLAY;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void oled_set_brightness(uint8_t value)
{
    // prevent overwrites
    if (is_queue_full(&g_cmd_queue) || g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf;
    queue_back(&g_cmd_queue, (void*)&cmd_buf); 
    
    uint8_t phase2    = 1 + ((uint16_t)value * 14) / 255;
    uint8_t precharge = (phase2 << 4) | 0x01; 

    uint8_t vcomh;
    if (value < 85)
    {
        vcomh = 0x00;
    }   
    else if (value < 170)
    {
        vcomh = 0x20;}   
    else       
    {
        vcomh = 0x30;
    }   

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_SET_PRECHARGE_PERIOD;
    cmd_buf->buf[cmd_buf->len++] = precharge;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_SET_COMH_DESELECT;
    cmd_buf->buf[cmd_buf->len++] = vcomh; 
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_SET_CONTRAST;
    cmd_buf->buf[cmd_buf->len++] = value;

    queue_push(&g_cmd_queue, NULL);
    flush_cmd();
}

void oled_clear_screen()
{
    // Don't waste time if the state is errored
    if (g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    g_oled_fb.end_page = 7;
    memset(g_oled_fb.fb + g_oled_fb.cmd_len, 0x0, OLED_SCREEN_W * OLED_PAGE_COUNT);
}

void oled_fill_rect(oled_coord_t top_left, oled_coord_t bottom_right, uint8_t value)
{
    // Don't waste time if the state is errored
    if (g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    g_oled_fb.end_page = MAX(g_oled_fb.end_page, bottom_right.page);

    uint16_t spix = g_oled_fb.cmd_len + top_left.page * OLED_SCREEN_W + top_left.col;
    for (uint8_t pg = top_left.page; pg <= bottom_right.page; pg++)
    {
        memset(g_oled_fb.fb + spix, value, bottom_right.col - top_left.col + 1);
        spix += OLED_SCREEN_W; 
    }
}

void oled_draw_bitmap(oled_coord_t top_left, uint8_t cols, uint8_t pages, const uint8_t *data)
{
    // Don't waste time if the state is errored
    if (g_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    g_oled_fb.end_page = MAX(g_oled_fb.end_page, top_left.page + pages - 1);

    uint16_t spix = g_oled_fb.cmd_len + top_left.page * OLED_SCREEN_W + top_left.col;
    for (uint8_t pg = top_left.page; pg <= top_left.page + pages - 1; pg++)
    {
        memcpy(g_oled_fb.fb + spix, data + pg * cols, cols);
        spix += OLED_SCREEN_W; 
    }
}
