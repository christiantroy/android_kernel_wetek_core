#ifndef IRRECEIVER_H
#define IRRECEIVER_H

#define MAX_PLUSE 1024
#define AM_IR_DEC_LDR_ACTIVE 0x0
#define AM_IR_DEC_LDR_IDLE 0x4
#define AM_IR_DEC_LDR_REPEAT 0x8
#define AM_IR_DEC_BIT_0     0xc 
#define AM_IR_DEC_REG0 0x10       
#define AM_IR_DEC_FRAME 0x14
#define AM_IR_DEC_STATUS 0x18
#define AM_IR_DEC_REG1 0x1c
#define am_remote_write_reg(x,val) aml_write_reg32(g_remote_base +x ,val)

#define am_remote_read_reg(x) aml_read_reg32(g_remote_base +x)

#define am_remote_set_mask(x,val) aml_set_reg32_mask(g_remote_base +x,val)

#define am_remote_clear_mask(x,val) aml_clr_reg32_mask(g_remote_base +x,val)
struct ir_window {
    unsigned int winNum;
    unsigned int winArray[MAX_PLUSE];
};

#define IRRECEIVER_IOC_SEND     0x5500
#define IRRECEIVER_IOC_RECV     0x5501
#define IRRECEIVER_IOC_STUDY_S  0x5502
#define IRRECEIVER_IOC_STUDY_E  0x5503


#endif //IRRECEIVER_H

