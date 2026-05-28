#ifndef DRIVER_BARO_REGS_H
#define DRIVER_BARO_REGS_H

#include "utils/utils.h"

#define BARO_CHIPID 0xD0

#define BARO_RESET 0xE0
#define BARO_RESET_WORD 0xB6

#define BARO_STATUS 0xF3
#define BARO_STATUS_MEAR_Msk BIT(3)
#define BARO_STATUS_IMUP_Msk BIT(0)

#define BARO_CTRL_MEAS 0xF4
#define BARO_CTRL_MEAS_OSRST_Pos 5
#define BARO_CTRL_MEAS_OSRSP_Pos 2
#define BARO_CTRL_MEAS_MODE_Pos 0

#define BARO_CONF 0xF5
#define BARO_CONF_TSB_Pos 5
#define BARO_CONF_FILTER_Pos 2

#define BARO_PRESS 0xF7 // 3 bytes

#define BARO_TEMP 0xFA // 3 bytes

#define BARO_CALIB 0x88 // 12 bytes

#endif
