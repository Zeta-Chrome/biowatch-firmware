#include "drivers/oled/oled.h"
#include "drivers/oled/oled_cmds.h"
#include "core/utils/status.h"
#include "bsp/platform.h"
#include "core/hal/i2c/i2c.h"
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define OLED_ADDR 0x3C

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

static oled_cmd_ring_t g_oled_cmd_ring;
static bool g_oled_i2c_initialized = false;
static oled_state_t g_oled_state = OLED_STATE_UNINITIALIZED;
static bool g_oled_fb_flush_pending = false;
static uint8_t g_oled_retry_counter = 0;
static i2c_handle_t g_oled_i2c_h;

static void flush_cmd();

static void on_initialized(bw_status_t status)
{
    if (status == STATUS_I2C_ERR || status == STATUS_I2C_NACKF)
    {
        hal_i2c_reset(PL_OLED_I2C);

        if (g_oled_retry_counter < OLED_MAX_RETRIES)
        {
            oled_init(); // reinit on failure
            g_oled_retry_counter++;
        }
        else 
        {
            g_oled_state = OLED_STATE_I2C_ERR;
        }
    }
    else if (status == STATUS_OK)
    {
        g_oled_state = OLED_STATE_IDLE;
        g_oled_retry_counter = 0;
    }
}

static void on_cmd_flushed(bw_status_t status)
{
    if (status == STATUS_I2C_ERR || status == STATUS_I2C_NACKF)
    {
        hal_i2c_reset(PL_OLED_I2C);
        if (g_oled_retry_counter < OLED_MAX_RETRIES)
        {
            flush_cmd();
            g_oled_retry_counter++;
        }
        else 
        {
            g_oled_state = OLED_STATE_I2C_ERR;
        }
    }
    else if (status == STATUS_OK)
    {
        g_oled_state = OLED_STATE_IDLE; 
        g_oled_retry_counter = 0;

        // Drain on completion
        g_oled_cmd_ring.count--;
        g_oled_cmd_ring.tail = (g_oled_cmd_ring.tail + 1) % OLED_CMD_RING_SZ;
    
        flush_cmd(); // flush next command if available
        
        if(g_oled_fb_flush_pending)
        {
            oled_flush();
        }
    }
}

static void on_fb_flushed(bw_status_t status)
{
    g_oled_state = OLED_STATE_IDLE;
    if (status == STATUS_I2C_ERR || status == STATUS_I2C_NACKF)
    {
        hal_i2c_reset(PL_OLED_I2C);
        g_oled_fb_flush_pending = true;
    }
    else
    {
        flush_cmd(); // flush next command if available

        if(g_oled_fb_flush_pending)
        {
            oled_flush();
        }
    }
}

void oled_init()
{
    g_oled_state = OLED_STATE_UNINITIALIZED;
    
    if (!g_oled_i2c_initialized)
    {
        // I2C initialization
        i2c_conf_t conf = {
        .sda = PL_OLED_SDA, .scl = PL_OLED_SCL, .af = GPIO_AF4, .i2c = PL_OLED_I2C, .speed = I2C_SPEED_FAST};
        hal_i2c_init(&conf);
        g_oled_i2c_initialized = true;
    }

    // OLED initialization
    g_oled_i2c_h.i2c = PL_OLED_I2C;
    g_oled_i2c_h.addr = OLED_ADDR;
    g_oled_i2c_h.buf = g_oled_fb.fb;
    g_oled_i2c_h.len = 26; // 26 Init commands
    g_oled_i2c_h.on_complete = on_initialized;
    hal_i2c_transmit(&g_oled_i2c_h); 
}

bool oled_is_initialized()
{
    return g_oled_state > OLED_STATE_UNINITIALIZED;
}

bool oled_is_flushed()
{
    return g_oled_state >= OLED_STATE_IDLE;
}

bool oled_bus_error()
{
    return g_oled_state == OLED_STATE_I2C_ERR;
}

void oled_flush()
{
    // prevent race conditions
    __disable_irq();
    if (!oled_is_flushed())
    {
        __enable_irq();
        g_oled_fb_flush_pending = true; 
        return;
    }

    g_oled_state = OLED_STATE_BUSY;
    __enable_irq();

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

    g_oled_i2c_h.i2c = PL_OLED_I2C;
    g_oled_i2c_h.addr = OLED_ADDR;
    g_oled_i2c_h.buf = g_oled_fb.fb;
    g_oled_i2c_h.len = g_oled_fb.cmd_len + (g_oled_fb.end_page + 1) * OLED_SCREEN_W;
    g_oled_i2c_h.on_complete = on_fb_flushed;

    hal_i2c_transmit(&g_oled_i2c_h);

    // Now reset the end_page
    g_oled_fb.end_page = 0;
    g_oled_fb_flush_pending = false;
}

static void flush_cmd()
{
    // prevent race conditions
    __disable_irq();
    if (g_oled_cmd_ring.count == 0 || !oled_is_flushed())
    {
        __enable_irq();
        return;
    }

    g_oled_state = OLED_STATE_BUSY;
    __enable_irq();

    oled_cmd_buf_t *cmd_buf = &g_oled_cmd_ring.queue[g_oled_cmd_ring.tail];

    g_oled_i2c_h.i2c = PL_OLED_I2C;
    g_oled_i2c_h.addr = OLED_ADDR;
    g_oled_i2c_h.buf = cmd_buf->buf;
    g_oled_i2c_h.len = cmd_buf->len;
    g_oled_i2c_h.on_complete = on_cmd_flushed;

    hal_i2c_transmit(&g_oled_i2c_h);
}

static void push_cmd()
{
    g_oled_cmd_ring.count += (g_oled_cmd_ring.count < OLED_CMD_RING_SZ);
    g_oled_cmd_ring.head = (g_oled_cmd_ring.head + 1) % OLED_CMD_RING_SZ;
}

void oled_power_on()
{
    // prevent overwrites
    if (g_oled_cmd_ring.count >= OLED_CMD_RING_SZ|| g_oled_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf = &g_oled_cmd_ring.queue[g_oled_cmd_ring.head];

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_DISPLAY_ON;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_CHARGE_PUMP;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_EN_PUMP;

    push_cmd();
    flush_cmd();
}

void oled_power_off()
{
    // prevent overwrites
    if (g_oled_cmd_ring.count >= OLED_CMD_RING_SZ || g_oled_state <= OLED_STATE_I2C_ERR)
    {
        return;
    } 

    oled_cmd_buf_t *cmd_buf = &g_oled_cmd_ring.queue[g_oled_cmd_ring.head];

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_DISPLAY_OFF;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_CHARGE_PUMP;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_DIS_PUMP;

    push_cmd();
    flush_cmd();
}

void oled_display_normal()
{
    // prevent overwrites
    if (g_oled_cmd_ring.count >= OLED_CMD_RING_SZ|| g_oled_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf = &g_oled_cmd_ring.queue[g_oled_cmd_ring.head];

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_NORMAL_DISPLAY;

    push_cmd();
    flush_cmd();
}

void oled_display_inverse()
{
    // prevent overwrites
    if (g_oled_cmd_ring.count >= OLED_CMD_RING_SZ|| g_oled_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf = &g_oled_cmd_ring.queue[g_oled_cmd_ring.head];

    cmd_buf->len = 0;
    cmd_buf->buf[cmd_buf->len++] = OLED_CTRL_CMD_ONLY;
    cmd_buf->buf[cmd_buf->len++] = OLED_CMD_INVERSE_DISPLAY;

    push_cmd();
    flush_cmd();
}

void oled_set_brightness(uint8_t value)
{
    // prevent overwrites
    if (g_oled_cmd_ring.count >= OLED_CMD_RING_SZ|| g_oled_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    oled_cmd_buf_t *cmd_buf = &g_oled_cmd_ring.queue[g_oled_cmd_ring.head];
    
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

    push_cmd();
    flush_cmd();
}

void oled_clear_screen()
{
    // Don't waste time if the state is errored
    if (g_oled_state <= OLED_STATE_I2C_ERR)
    {
        return;
    }

    g_oled_fb.end_page = 7;
    memset(g_oled_fb.fb + g_oled_fb.cmd_len, 0x0, OLED_SCREEN_W * OLED_PAGE_COUNT);
}

void oled_fill_rect(oled_coord_t top_left, oled_coord_t bottom_right, uint8_t value)
{
    // Don't waste time if the state is errored
    if (g_oled_state <= OLED_STATE_I2C_ERR)
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
    if (g_oled_state <= OLED_STATE_I2C_ERR)
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
