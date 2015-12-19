
#ifdef  C_ALWAYS_ON_POINTER_H
#else 


//#define AO_Wr(addr, data)   *(volatile unsigned long *)((0xc8100000)|((addr)<<2))=(data)
//#define AO_Rd(addr)         *(volatile unsigned long *)((0xc8100000)|((addr)<<2))

// -------------------------------------------------------------------
// BASE #0
// -------------------------------------------------------------------

#define P_AO_RTI_STATUS_REG0        (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x00 << 2))
#define P_AO_RTI_STATUS_REG1        (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x01 << 2))
#define P_AO_RTI_STATUS_REG2        (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x02 << 2))

#define P_AO_RTI_PWR_CNTL_REG1      (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x03 << 2))
#define P_AO_RTI_PWR_CNTL_REG0      (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x04 << 2))
#define P_AO_RTI_PIN_MUX_REG        (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x05 << 2))

#define P_AO_WD_GPIO_REG            (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x06 << 2))

#define P_AO_REMAP_REG0             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x07 << 2))
#define P_AO_REMAP_REG1             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x08 << 2))
#define P_AO_GPIO_O_EN_N            (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x09 << 2))
#define P_AO_GPIO_I                 (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x0A << 2))

#define P_AO_RTI_PULL_UP_REG        (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x0B << 2))
// replaced with secure register
// #define P_AO_RTI_JTAG_CODNFIG_REG   (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x0C << 2))
#define P_AO_RTI_WD_MARK            (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x0D << 2))

#define P_AO_CPU_CNTL               (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x0E << 2))

#define P_AO_CPU_STAT               (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x0F << 2))


#define P_AO_RTI_GEN_CNTL_REG0      (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x10 << 2))
#define P_AO_WATCHDOG_REG           (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x11 << 2))
#define P_AO_WATCHDOG_RESET         (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x12 << 2))

#define P_AO_TIMER_REG              (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x13 << 2))
#define P_AO_TIMERA_REG             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x14 << 2))
#define P_AO_TIMERE_REG             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x15 << 2))

#define P_AO_AHB2DDR_CNTL           (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x18 << 2))
#define P_AO_TIMEBASE_CNTL          (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x19 << 2)) 

#define P_AO_CRT_CLK_CNTL1          (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x1a << 2)) 

#define P_AO_IRQ_MASK_FIQ_SEL       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x20 << 2))
#define P_AO_IRQ_GPIO_REG           (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x21 << 2))
#define P_AO_IRQ_STAT               (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x22 << 2))
#define P_AO_IRQ_STAT_CLR           (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x23 << 2))

#define P_AO_SAR_CLK                (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x24 << 2))

#define P_AO_DEBUG_REG0             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x28 << 2))
#define P_AO_DEBUG_REG1             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x29 << 2))
#define P_AO_DEBUG_REG2             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x2a << 2))
#define P_AO_DEBUG_REG3             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x2b << 2))

#define P_AO_IR_BLASTER_ADDR0       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x30 << 2))
#define P_AO_IR_BLASTER_ADDR1       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x31 << 2))
#define P_AO_IR_BLASTER_ADDR2       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x32 << 2))

// general Power control 
#define P_AO_RTI_PWR_A9_CNTL0       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x38 << 2))
#define P_AO_RTI_PWR_A9_CNTL1       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x39 << 2))
#define P_AO_RTI_GEN_PWR_SLEEP0     (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x3a << 2))
#define P_AO_RTI_GEN_PWR_ISO0       (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x3b << 2))

#define P_AO_CEC_GEN_CNTL           (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x40 << 2))
#define P_AO_CEC_RW_REG             (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x41 << 2))
#define P_AO_CEC_INTR_MASKN         (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x42 << 2))
#define P_AO_CEC_INTR_CLR           (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x43 << 2))
#define P_AO_CEC_INTR_STAT          (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x44 << 2))

#define P_AO_CRT_CLK_CNTL1          (volatile unsigned long *)(0xc8100000 | (0x00 << 10) | (0x1a << 2))
// -------------------------------------------------------------------
// BASE #1
// -------------------------------------------------------------------
#define P_AO_IR_DEC_LDR_ACTIVE      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x20 << 2))
#define P_AO_IR_DEC_LDR_IDLE        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x21 << 2))
#define P_AO_IR_DEC_LDR_REPEAT      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x22 << 2))
#define P_AO_IR_DEC_BIT_0           (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x23 << 2))
#define P_AO_IR_DEC_REG0            (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x24 << 2))
#define P_AO_IR_DEC_FRAME           (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x25 << 2))
#define P_AO_IR_DEC_STATUS          (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x26 << 2))
#define P_AO_IR_DEC_REG1            (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x27 << 2))

// ----------------------------
// UART
// ----------------------------
#define P_AO_UART_WFIFO             (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x30 << 2))
#define P_AO_UART_RFIFO             (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x31 << 2))
#define P_AO_UART_CONTROL           (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x32 << 2))
#define P_AO_UART_STATUS            (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x33 << 2))
#define P_AO_UART_MISC              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x34 << 2))
#define P_AO_UART_REG5              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x35 << 2))

// ----------------------------
// UART2
// ----------------------------
#define P_AO_UART2_WFIFO             (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x38 << 2))
#define P_AO_UART2_RFIFO             (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x39 << 2))
#define P_AO_UART2_CONTROL           (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x3a << 2))
#define P_AO_UART2_STATUS            (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x3b << 2))
#define P_AO_UART2_MISC              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x3c << 2))
#define P_AO_UART2_REG5              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x3d << 2))

// ----------------------------
// I2C Master (8)
// ----------------------------
#define P_AO_I2C_M_0_CONTROL_REG    (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x40 << 2))
#define P_AO_I2C_M_0_SLAVE_ADDR     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x41 << 2))
#define P_AO_I2C_M_0_TOKEN_LIST0    (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x42 << 2))
#define P_AO_I2C_M_0_TOKEN_LIST1    (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x43 << 2))
#define P_AO_I2C_M_0_WDATA_REG0     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x44 << 2))
#define P_AO_I2C_M_0_WDATA_REG1     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x45 << 2))
#define P_AO_I2C_M_0_RDATA_REG0     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x46 << 2))
#define P_AO_I2C_M_0_RDATA_REG1     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x47 << 2))
// ----------------------------
// I2C Slave (3)
// ----------------------------
#define P_AO_I2C_S_CONTROL_REG      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x50 << 2))
#define P_AO_I2C_S_SEND_REG         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x51 << 2))
#define P_AO_I2C_S_RECV_REG         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x52 << 2))
#define P_AO_I2C_S_CNTL1_REG        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x53 << 2))

// ---------------------------
// PWM 
// ---------------------------
#define P_AO_PWM_PWM_A              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x54 << 2))
#define P_AO_PWM_PWM_B              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x55 << 2))
#define P_AO_PWM_MISC_REG_AB        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x56 << 2))
#define P_AO_PWM_DELTA_SIGMA_AB     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x57 << 2))

// ---------------------------
// RTC (4)
// ---------------------------
// NOTE:  These are on the Secure APB3  bus
// 
// Not on the secure bus #define P_AO_RTC_ADDR0              (volatile unsigned long *)(0xDA004000 | (0xd0 << 2))
// Not on the secure bus #define P_AO_RTC_ADDR1              (volatile unsigned long *)(0xDA004000 | (0xd1 << 2))
// Not on the secure bus #define P_AO_RTC_ADDR2              (volatile unsigned long *)(0xDA004000 | (0xd2 << 2))
// Not on the secure bus #define P_AO_RTC_ADDR3              (volatile unsigned long *)(0xDA004000 | (0xd3 << 2))
// Not on the secure bus #define P_AO_RTC_ADDR4              (volatile unsigned long *)(0xDA004000 | (0xd4 << 2))
#define P_AO_RTC_ADDR0              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0xd0 << 2))
#define P_AO_RTC_ADDR1              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0xd1 << 2))
#define P_AO_RTC_ADDR2              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0xd2 << 2))
#define P_AO_RTC_ADDR3              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0xd3 << 2))
#define P_AO_RTC_ADDR4              (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0xd4 << 2))

#define P_AO_MF_IR_DEC_LDR_ACTIVE   (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x60 << 2))
#define P_AO_MF_IR_DEC_LDR_IDLE     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x61 << 2))
#define P_AO_MF_IR_DEC_LDR_REPEAT   (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x62 << 2))
#define P_AO_MF_IR_DEC_BIT_0        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x63 << 2))
#define P_AO_MF_IR_DEC_REG0         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x64 << 2))
#define P_AO_MF_IR_DEC_FRAME        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x65 << 2))
#define P_AO_MF_IR_DEC_STATUS       (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x66 << 2))
#define P_AO_MF_IR_DEC_REG1         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x67 << 2))
#define P_AO_MF_IR_DEC_REG2         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x68 << 2))
#define P_AO_MF_IR_DEC_DURATN2      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x69 << 2))
#define P_AO_MF_IR_DEC_DURATN3      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x6a << 2))
#define P_AO_MF_IR_DEC_FRAME1       (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x6b << 2))
#define P_AO_MF_IR_DEC_STATUS1      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x6c << 2))   
#define P_AO_MF_IR_DEC_STATUS2      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x6d << 2))   
#define P_AO_MF_IR_DEC_REG3         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x6e << 2))   
#define P_AO_MF_IR_DEC_FRAME_RSV0   (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x6f << 2))   
#define P_AO_MF_IR_DEC_FRAME_RSV1   (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x70 << 2))   

// ----------------------------
// SAR ADC (16)
// ----------------------------
#define P_AO_SAR_ADC_REG0           (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x80 << 2))
#define P_AO_SAR_ADC_CHAN_LIST      (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x81 << 2))
#define P_AO_SAR_ADC_AVG_CNTL       (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x82 << 2))
#define P_AO_SAR_ADC_REG3           (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x83 << 2))
#define P_AO_SAR_ADC_DELAY          (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x84 << 2))
#define P_AO_SAR_ADC_LAST_RD        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x85 << 2))
#define P_AO_SAR_ADC_FIFO_RD        (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x86 << 2))
#define P_AO_SAR_ADC_AUX_SW         (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x87 << 2))
#define P_AO_SAR_ADC_CHAN_10_SW     (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x88 << 2))
#define P_AO_SAR_ADC_DETECT_IDLE_SW (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x89 << 2))
#define P_AO_SAR_ADC_DELTA_10       (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x8a << 2))
#define P_AO_SAR_ADC_REG11          (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x8b << 2))
#define P_AO_SAR_ADC_REG12          (volatile unsigned long *)(0xc8100000 | (0x01 << 10) | (0x8c << 2))

#endif      // C_ALWAYS_ON_POINTER_H

