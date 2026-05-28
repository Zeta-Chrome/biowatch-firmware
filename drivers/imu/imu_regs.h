#ifndef DRIVER_IMU_REGS_H
#define DRIVER_IMU_REGS_H

#include "utils/utils.h"

#define IMU_WRITE 0x00
#define IMU_READ 0x80
#define IMU_CHIPID 0x00

#define IMU_ERR 0x02
#define IMU_ERR_DROP_CMD_Msk BIT(6)
#define IMU_ERR_CODE_Pos 1
#define IMU_ERR_FATAL_Msk BIT(0)

#define IMU_PMU_ST 0x03
#define IMU_PMU_ST_ACC_Pos 4
#define IMU_PMU_ST_GYR_Pos 2

#define IMU_GYR_DATA 0x0C // 6 bytes
#define IMU_ACC_DATA 0x12 // 6 bytes

#define IMU_SENSORTIME 0x18 // 3 bytes

#define IMU_ST 0x1B
#define IMU_ST_DRDY_ACC_Pos 7
#define IMU_ST_DRDY_GYR_Pos 6
#define IMU_ST_NVM_RDY_Pos 4

#define IMU_INT_ST0 0x1C
#define IMU_INT_ST0_FLAT_Msk BIT(7)
#define IMU_INT_ST0_STAP_Msk BIT(5)
#define IMU_INT_ST0_DTAP_Msk BIT(4)
#define IMU_INT_ST0_PMU_Msk BIT(3)
#define IMU_INT_ST0_ANYM_Msk BIT(2)
#define IMU_INT_ST0_SIGMOT_Msk BIT(1)
#define IMU_INT_ST0_STEP_Msk BIT(0)

#define IMU_INT_ST1 0x1D
#define IMU_INT_ST1_NOMO_Msk BIT(7)
#define IMU_INT_ST1_FFULL_Msk BIT(5)
#define IMU_INT_ST1_DRDY_Msk BIT(4)

#define IMU_FIFO_LEN 0x22 // 2 bytes
#define IMU_FIFO_DATA 0x24

#define IMU_ACC_CONF 0x40
#define IMU_ACC_CONF_US_Msk BIT(7)
#define IMU_ACC_CONF_BWP_Pos 4
#define IMU_ACC_CONF_ODR_Pos 0

#define IMU_ACC_RANGE 0x41
#define IMU_ACC_RANGE_Pos 0

#define IMU_GYR_CONF 0x42
#define IMU_GYR_CONF_BWP_Pos 4
#define IMU_GYR_CONF_ODR_Pos 0

#define IMU_GYR_RANGE 0x43

#define IMU_FIFO_FWM 0x46

#define IMU_FIFO_CONF 0x47
#define IMU_FIFO_CONF_ACC_EN_Pos 7
#define IMU_FIFO_CONF_GYR_EN_Pos 6

#define IMU_INT_EN0 0x50
#define IMU_INT_EN0_FLAT_Msk BIT(7)
#define IMU_INT_EN0_STAP_Msk BIT(5)
#define IMU_INT_EN0_DTAP_Msk BIT(4)
#define IMU_INT_EN0_ANYMX_Msk BIT(2)
#define IMU_INT_EN0_ANYMY_Msk BIT(1)
#define IMU_INT_EN0_ANYMZ_Msk BIT(0)

#define IMU_INT_EN1 0x51
#define IMU_INT_EN1_FWM_Msk BIT(6)
#define IMU_INT_EN1_FFUL_Msk BIT(5)
#define IMU_INT_EN1_DRDY_Msk BIT(4)

#define IMU_INT_EN2 0x52
#define IMU_INT_EN2_STEP_Msk BIT(3)
#define IMU_INT_EN2_NOMOX_Msk BIT(2)
#define IMU_INT_EN2_NOMOY_Msk BIT(1)
#define IMU_INT_EN2_NOMOZ_Msk BIT(0)

#define IMU_INT_OUT 0x53
#define IMU_INT2_OUT_EN_Msk BIT(7) 
#define IMU_INT2_OD_Pos 6 
#define IMU_INT2_LVL_Pos 5 
#define IMU_INT2_EDGE_Pos 4 
#define IMU_INT1_OUT_EN_Msk BIT(3) 
#define IMU_INT1_OD_Pos 2
#define IMU_INT1_LVL_Pos 1 
#define IMU_INT1_EDGE_Pos 0

#define IMU_INT1_MAP0 0x55
#define IMU_INT1_MAP0_FLAT_Msk BIT(7)
#define IMU_INT1_MAP0_ORIENT_Msk BIT(6)
#define IMU_INT1_MAP0_STAP_Msk BIT(5)
#define IMU_INT1_MAP0_DTAP_Msk BIT(4)
#define IMU_INT1_MAP0_NOMO_Msk BIT(3)
#define IMU_INT1_MAP0_ANYMO_Msk BIT(2)

#define IMU_INT1_MAP1 0x56
#define IMU_INT1_MAP1_DRDY_Msk BIT(3)
#define IMU_INT1_MAP1_FWM_Msk BIT(2)
#define IMU_INT1_MAP1_FFULL_Msk BIT(1)

#define IMU_INT2_MAP2 0x55
#define IMU_INT2_MAP2_FLAT_Msk BIT(7)
#define IMU_INT2_MAP2_ORIENT_Msk BIT(6)
#define IMU_INT2_MAP2_STAP_Msk BIT(5)
#define IMU_INT2_MAP2_DTAP_Msk BIT(4)
#define IMU_INT2_MAP2_NOMO_Msk BIT(3)
#define IMU_INT2_MAP2_ANYMO_Msk BIT(2)

#define IMU_INT_MO0 0x5F
#define IMU_INT_MO0_SN_DUR_Pos 2
#define IMU_INT_MO0_ANYMO_DUR_Pos 0
#define IMU_INT_MO1 0x60 // Anymotion threshold
#define IMU_INT_MO2 0x61 // slowmo or nomo threshold
#define IMU_INT_MO2_SN_TH_Pos 0
#define IMU_INT_MO3 0x62
#define IMU_INT_MO3_PROOF_Pos 4
#define IMU_INT_MO3_SKIP_Pos 2
#define IMU_INT_MO3_ANYMO_SEL_Msk BIT(1)
#define IMU_INT_MO3_NOMO_SEL_Msk BIT(0) 

#define IMU_INT_TAP 0x63 
#define IMU_INT_TAP_QUITE_Pos 7 
#define IMU_INT_TAP_SHOCK_Pos 6
#define IMU_INT_TAP_DUR_Pos 0
#define IMU_INT_TAP_TH_Pos 0

#define IMU_STEP_CNT 0x78 // 2 bytes

#define IMU_STEP_CONF 0x7A
#define IMU_STEP_CNT_EN_Msk BIT(3)

#define IMU_CMD 0x7E
#define IMU_CMD_INT_RST 0xB1
#define IMU_CMD_SOFTRST 0xB6
#define IMU_CMD_ACC_SUSPEND 0x10
#define IMU_CMD_ACC_NORMAL 0x11
#define IMU_CMD_ACC_LOW_PWR 0x12
#define IMU_CMD_GYR_SUSPEND 0x14
#define IMU_CMD_GYR_NORMAL 0x15
#define IMU_CMD_GYR_FAST_STRT 0x17
#define IMU_CMD_STEP_CNT_CLR 0xB2

#endif
