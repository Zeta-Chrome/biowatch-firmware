#ifndef DRIVER_OXIM_REGS_H
#define DRIVER_OXIM_REGS_H

// Interrupt registers
#include "lib/utils.h"
#include <stdint.h>

#define OXIM_INT_ST1 0x0
#define OXIM_INT_ST1_PWR_RDY BIT(0)
#define OXIM_INT_ST1_ALC_OVF BIT(5)
#define OXIM_INT_ST1_PPG_RDY BIT(6)
#define OXIM_INT_ST1_A_FULL BIT(7)

#define OXIM_INT_ST2 0x01
#define OXIM_INT_ST2_DIE_TEMP_RDY BIT(1)

#define OXIM_INT_EN1 0x02
#define OXIM_INT_EN1_ALC_OVF BIT(5)
#define OXIM_INT_EN1_PPG_RDY BIT(6)
#define OXIM_INT_EN1_A_FULL BIT(7)

#define OXIM_INT_EN2 0x03
#define OXIM_INT_EN2_DIE_TEMP_RDY BIT(1)

// FIFO registers
#define OXIM_FIFO_WR_PTR 0x04
#define OXIM_FIFO_OVF_CTR 0x05
#define OXIM_FIFO_RD_PTR 0x06
#define OXIM_FIFO_DATA 0x07

// Configuration registers
#define OXIM_FIFO_CONF 0x08
#define OXIM_FIFO_CONF_A_FULL_Pos 0
#define OXIM_FIFO_CONF_SMP_AVE_Pos 5

#define OXIM_MODE_CONF 0x09
#define OXIM_MODE_CONF_SHDN_Msk BIT(7)
#define OXIM_MODE_CONF_RST_Msk BIT(6)
#define OXIM_MODE_CONF_MODE_Pos 0

#define OXIM_SPO2_CONF 0x0A
#define OXIM_SPO2_CONF_ADC_RGE_Pos 5
#define OXIM_SPO2_CONF_SR_Pos 2
#define OXIM_SPO2_CONF_LED_PW_Pos 0

#define OXIM_LED1_PA 0x0C
#define OXIM_LED2_PA 0x0D

// Temperature registers
#define OXIM_DIE_TEMP_INT 0x1F
#define OXIM_DIE_TEMP_FRAC 0x20
#define OXIM_DIE_TEMP_EN 0x21

#endif
