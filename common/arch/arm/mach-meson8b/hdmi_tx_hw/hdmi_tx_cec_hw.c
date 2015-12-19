#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <asm/delay.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>
#include <linux/amlogic/tvin/tvin.h>

#include <mach/gpio.h>
#include <linux/amlogic/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_cec.h>
#include <mach/hdmi_tx_reg.h>
#include <mach/hdmi_parameter.h>

static DEFINE_MUTEX(cec_mutex);

void cec_arbit_bit_time_set(unsigned bit_set, unsigned time_set, unsigned flag);
unsigned int cec_int_disable_flag = 0;
static unsigned char msg_log_buf[128] = { 0 };
extern int cec_msg_dbg_en;

void cec_disable_irq(void)
{
    // disable all AO_CEC interrupt sources
    aml_set_reg32_bits(P_AO_CEC_INTR_MASKN, 0x0, 0, 3);
    cec_int_disable_flag = 1;
    hdmi_print(INF, CEC "disable:int mask:0x%x\n", aml_read_reg32(P_AO_CEC_INTR_MASKN));
}
void cec_enable_irq(void)
{
    aml_set_reg32_bits(P_AO_CEC_INTR_MASKN, 0x6, 0, 3);
    cec_int_disable_flag = 0;
    hdmi_print(INF, CEC "enable:int mask:0x%x\n", aml_read_reg32(P_AO_CEC_INTR_MASKN));
}

void gpiox_10_pin_mux_mask(void){
    //GPIOX_10 pin mux masked expect PWM_E
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_3, 0x0, 22, 1); //0x202f bit[22]: XTAL_32K_OUT 
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_3, 0x0,  8, 1); //0x202f bit[ 8]: Tsin_D0_B
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_4, 0x0, 23, 1); //0x2030 bit[23]: SPI_MOSI
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_6, 0x0, 17, 1); //0x2032 bit[17]: UART_CTS_B
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_7, 0x0, 31, 1); //0x2033 bit[31]: PWM_VS
    aml_set_reg32_bits(P_PERIPHS_PIN_MUX_9, 0x1, 19, 1); //0x2035 bit[19]: PWM_E
}

void pwm_e_config(void){
    //PWM E config
    aml_set_reg32_bits(P_PWM_PWM_E,    0x25ff, 16, 16); //0x21b0 bit[31:16]: PWM_E_HIGH counter
    aml_set_reg32_bits(P_PWM_PWM_E,    0x25fe,  0, 16); //0x21b0 bit[15: 0]: PWM_E_LOW counter
    aml_set_reg32_bits(P_PWM_MISC_REG_EF, 0x1, 15,  1); //0x21b2 bit[15]   : PWM_E_CLK_EN
    aml_set_reg32_bits(P_PWM_MISC_REG_EF, 0x0,  8,  7); //0x21b2 bit[14: 8]: PWM_E_CLK_DIV
    aml_set_reg32_bits(P_PWM_MISC_REG_EF, 0x2,  4,  2); //0x21b2 bit[5 : 4]: PWM_E_CLK_SEL :0x2 sleect fclk_div4:637.5M
    aml_set_reg32_bits(P_PWM_MISC_REG_EF, 0x1,  0,  1); //0x21b2 bit[0]    : PWM_E_EN
}

void pwm_out_rtc_32k(void){
    gpiox_10_pin_mux_mask(); //enable PWM_E pin mux
    pwm_e_config();  //PWM E config   
    hdmi_print(INF, CEC "Set PWM_E out put RTC 32K!\n");
}

void cec_hw_reset(void)
{
    //unsigned long data32;
    // Assert SW reset AO_CEC
    //data32  = 0;
    //data32 |= 0 << 1;   // [2:1]    cntl_clk: 0=Disable clk (Power-off mode); 1=Enable gated clock (Normal mode); 2=Enable free-run clk (Debug mode).
    //data32 |= 1 << 0;   // [0]      sw_reset: 1=Reset
    aml_write_reg32(P_AO_CEC_GEN_CNTL, 0x1);
    // Enable gated clock (Normal mode).
    aml_set_reg32_bits(P_AO_CEC_GEN_CNTL, 1, 1, 1);
    // Release SW reset
    aml_set_reg32_bits(P_AO_CEC_GEN_CNTL, 0, 0, 1);

    // Enable all AO_CEC interrupt sources
    if (!cec_int_disable_flag)
        aml_set_reg32_bits(P_AO_CEC_INTR_MASKN, 0x6, 0, 3);

    aocec_wr_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | cec_global_info.my_node_index);

    //Cec arbitration 3/5/7 bit time set.
    cec_arbit_bit_time_set(3, 0x118, 0);
    cec_arbit_bit_time_set(5, 0x000, 0);
    cec_arbit_bit_time_set(7, 0x2aa, 0);

    hdmi_print(INF, CEC "hw reset :logical addr:0x%x\n", aocec_rd_reg(CEC_LOGICAL_ADDR0));

}

void cec_rx_buf_clear(void)
{
    aocec_wr_reg(CEC_RX_CLEAR_BUF, 0x1);
    aocec_wr_reg(CEC_RX_CLEAR_BUF, 0x0);
    hdmi_print(INF, CEC "rx buf clean\n");
}

int cec_rx_buf_check(void)
{
    unsigned long rx_num_msg = aocec_rd_reg(CEC_RX_NUM_MSG);

    if (rx_num_msg)
        hdmi_print(INF, CEC "rx msg num:0x%02x\n", rx_num_msg);

    return rx_num_msg;
}

int cec_ll_rx( unsigned char *msg, unsigned char *len)
{
    int i;
    int ret = -1;
    int pos;
    int rx_stat;

    rx_stat = aocec_rd_reg(CEC_RX_MSG_STATUS);
    if ((RX_DONE != rx_stat) || (1 != aocec_rd_reg(CEC_RX_NUM_MSG)))
    {
        hdmi_print(INF, CEC, "rx status:%x\n", rx_stat);
        aml_write_reg32(P_AO_CEC_INTR_CLR, aml_read_reg32(P_AO_CEC_INTR_CLR) | (1 << 2));
        aocec_wr_reg(CEC_RX_MSG_CMD,  RX_ACK_CURRENT);
        aocec_wr_reg(CEC_RX_MSG_CMD,  RX_NO_OP);
        return ret;
    }

    *len = aocec_rd_reg(CEC_RX_MSG_LENGTH) + 1;
    for (i = 0; i < (*len) && i < MAX_MSG; i++)
    {
        msg[i]= aocec_rd_reg(CEC_RX_MSG_0_HEADER +i);
    }

    ret = rx_stat;

    if (cec_msg_dbg_en  == 1)
    {
        pos = 0;
        pos += sprintf(msg_log_buf + pos, "CEC[%d]: rx msg len: %d   dat: ",
                       cec_global_info.my_node_index, *len);
        for (i = 0; i < (*len); i++)
        {
            pos += sprintf(msg_log_buf + pos, "%02x ", msg[i]);
        }
        pos += sprintf(msg_log_buf + pos, "\n");
        msg_log_buf[pos] = '\0';
        hdmi_print(INF, CEC "%s", msg_log_buf);
    }

    //cec_rx_buf_check();
    aml_write_reg32(P_AO_CEC_INTR_CLR, aml_read_reg32(P_AO_CEC_INTR_CLR) | (1 << 2));
    aocec_wr_reg(CEC_RX_MSG_CMD, RX_ACK_NEXT);
    aocec_wr_reg(CEC_RX_MSG_CMD, RX_NO_OP);

    return ret;
}


/*************************** cec arbitration cts code ******************************/
// using the cec pin as fiq gpi to assist the bus arbitration

// return value: 1: successful      0: error
static int cec_ll_tx_once(const unsigned char *msg, unsigned char len)
{
    int i;
    unsigned int ret = 0xf;
    unsigned int n;
    unsigned int cnt = 30;
    int pos;

    while (aocec_rd_reg(CEC_TX_MSG_STATUS) || aocec_rd_reg(CEC_RX_MSG_STATUS))
    {
        msleep(5);
        if (TX_ERROR == aocec_rd_reg(CEC_TX_MSG_STATUS))
        {
            if (cec_msg_dbg_en  == 1)
                hdmi_print(INF, CEC "tx once:tx error!\n");
            //aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            //cec_hw_reset();
            break;
        }
        if (!(cnt--))
        {
            if (cec_msg_dbg_en  == 1)
                hdmi_print(INF, CEC "tx busy time out.\n");
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            break;
        }
    }
    for (i = 0; i < len; i++)
    {
        aocec_wr_reg(CEC_TX_MSG_0_HEADER + i, msg[i]);
    }
    aocec_wr_reg(CEC_TX_MSG_LENGTH, len-1);
    aocec_wr_reg(CEC_TX_MSG_CMD, TX_REQ_CURRENT);

    if (cec_msg_dbg_en  == 1)
    {
        pos = 0;
        pos += sprintf(msg_log_buf + pos, "CEC: tx msg len: %d   dat: ", len);
        for (n = 0; n < len; n++)
        {
            pos += sprintf(msg_log_buf + pos, "%02x ", msg[n]);
        }
        pos += sprintf(msg_log_buf + pos, "\n");

        msg_log_buf[pos] = '\0';
        printk("%s", msg_log_buf);
    }
    return ret;
}

int cec_ll_tx_polling(const unsigned char *msg, unsigned char len)
{
    int i;
    unsigned int ret = 0xf;
    unsigned int n;
    unsigned int j = 50;
    int pos;
    unsigned tx_stat;
    int flag = 0;

    /*
     * wait until tx is free
     */
    while (1) {
        tx_stat = aocec_rd_reg(CEC_TX_MSG_STATUS);
        if (!flag && tx_stat == TX_BUSY) {
            hdmi_print(INF, CEC "tx_stat is busy, waiting free...\n");
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
            flag = 1;
        }
        if (tx_stat != TX_BUSY) {
            break;
        }
        if (!(j--))
        {
            if (cec_msg_dbg_en  == 1)
                hdmi_print(INF, CEC "tx busy time out.\n");
            //aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            break;
        }
        msleep(5);
    }

    hdmi_print(INF, CEC "now tx_stat:%d\n", tx_stat);
    if (TX_ERROR == tx_stat) {
        hdmi_print(INF, CEC "tx polling:tx error!.\n");
        //aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
        aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
        //cec_hw_reset();
    } else if (TX_BUSY == tx_stat) {
        return TX_BUSY;
    }
    aml_set_reg32_bits(P_AO_CEC_INTR_MASKN, 0x0, 1, 1);
    for (i = 0; i < len; i++)
    {
        aocec_wr_reg(CEC_TX_MSG_0_HEADER + i, msg[i]);
    }
    aocec_wr_reg(CEC_TX_MSG_LENGTH, len-1);
    aocec_wr_reg(CEC_TX_MSG_CMD, RX_ACK_CURRENT);

    j = 50;
    hdmi_print(INF, CEC "start poll\n");
    while (j--) {
        ret = aocec_rd_reg(CEC_TX_MSG_STATUS);
        if (ret != TX_BUSY) {
            break;
        }
        msleep(5);
    }

    ret = aocec_rd_reg(CEC_TX_MSG_STATUS);
    hdmi_print(INF, CEC "end poll, tx_stat:%x\n", ret);
    if (ret == TX_BUSY) {
        hdmi_print(INF, CEC "tx busy timeout\n");
        aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
    }

    aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
    aml_set_reg32_bits(P_AO_CEC_INTR_MASKN, 1, 1, 1);

    if (cec_msg_dbg_en  == 1)
    {
        pos = 0;
        pos += sprintf(msg_log_buf + pos, "CEC: tx msg len: %d   dat: ", len);
        for (n = 0; n < len; n++)
        {
            pos += sprintf(msg_log_buf + pos, "%02x ", msg[n]);
        }
        msg_log_buf[pos] = '\0';
        printk("%s\n", msg_log_buf);
    }
    return ret;
}

void tx_irq_handle(void)
{
    unsigned tx_status = aocec_rd_reg(CEC_TX_MSG_STATUS);
    switch (tx_status)
    {
        case TX_DONE:
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            break;
        case TX_BUSY:
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_ABORT);
            aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            break;
        case TX_ERROR:
            if (cec_msg_dbg_en  == 1)
                hdmi_print(INF, CEC "TX ERROR!!!\n");
            if (RX_ERROR == aocec_rd_reg(CEC_RX_MSG_STATUS))
            {
                cec_hw_reset();
            }
            else
            {
                aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            }
            //aocec_wr_reg(CEC_TX_MSG_CMD, TX_NO_OP);
            break;
        default:
        break;
    }
    aml_write_reg32(P_AO_CEC_INTR_CLR, aml_read_reg32(P_AO_CEC_INTR_CLR) | (1 << 1));
    //aml_write_reg32(P_AO_CEC_INTR_MASKN, aml_read_reg32(P_AO_CEC_INTR_MASKN) | (1 << 2));
}

// Return value: 0: fail    1: success
int cec_ll_tx(const unsigned char *msg, unsigned char len)
{
    int ret = 0;
    if (cec_int_disable_flag)
        return 2;

    mutex_lock(&cec_mutex);
    //aml_write_reg32(P_AO_CEC_INTR_MASKN, aml_read_reg32(P_AO_CEC_INTR_MASKN) & ~(1 << 2));
    cec_ll_tx_once(msg, len);

    mutex_unlock(&cec_mutex);

    return ret;
}

void cec_polling_online_dev(int log_addr, int *bool)
{
    unsigned int r;
    unsigned char msg[1];
    int retry = 5;

    cec_global_info.my_node_index = log_addr;
    msg[0] = (log_addr<<4) | log_addr;

    aocec_wr_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | 0xf);
    if (cec_msg_dbg_en  == 1)
        hdmi_print(INF, CEC "CEC_LOGICAL_ADDR0:0x%lx\n",aocec_rd_reg(CEC_LOGICAL_ADDR0));
    while (retry) {
        r = cec_ll_tx_polling(msg, 1);
        if (r == TX_BUSY) {
            retry--;
            hdmi_print(INF, CEC "try log addr %x busy, retry:%d\n", log_addr, retry);
            /*
             * try to reset CEC if tx busy is found
             */
            cec_hw_reset();
        } else {
            break;
        }
    }
//    cec_hw_reset();

    if (r == TX_ERROR) {
        *bool = 0;
    } else if (r == TX_DONE) {
        memset(&(cec_global_info.cec_node_info[log_addr]), 0, sizeof(cec_node_info_t));
        cec_global_info.cec_node_info[log_addr].dev_type = cec_log_addr_to_dev_type(log_addr);
        *bool = 1;
    }
    if (*bool == 0)
    {
        aocec_wr_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | log_addr);
    }
    hdmi_print(INF, CEC "CEC: poll online logic device: 0x%x BOOL: %d\n", log_addr, *bool);

}


//--------------------------------------------------------------------------
// AO CEC0 config
//--------------------------------------------------------------------------
void ao_cec_init(void)
{
    unsigned long data32;
    pwm_out_rtc_32k();  //enable RTC 32k
    // Assert SW reset AO_CEC
    data32  = 0;
    data32 |= 0 << 1;   // [2:1]    cntl_clk: 0=Disable clk (Power-off mode); 1=Enable gated clock (Normal mode); 2=Enable free-run clk (Debug mode).
    data32 |= 1 << 0;   // [0]      sw_reset: 1=Reset
    aml_write_reg32(P_AO_CEC_GEN_CNTL, data32);
    // Enable gated clock (Normal mode).
    aml_set_reg32_bits(P_AO_CEC_GEN_CNTL, 1, 1, 1);
    // Release SW reset
    aml_set_reg32_bits(P_AO_CEC_GEN_CNTL, 0, 0, 1);

    // Enable all AO_CEC interrupt sources
    cec_enable_irq();

    // Device 0 config
    aocec_wr_reg(CEC_LOGICAL_ADDR0, (0x1 << 4) | 0x4);
}


void cec_arbit_bit_time_read(void)
{   //11bit:bit[10:0]
    //3 bit
    hdmi_print(INF, CEC "read 3 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_4BIT_BIT10_8),aocec_rd_reg(AO_CEC_TXTIME_4BIT_BIT7_0));
    //5 bit
    hdmi_print(INF, CEC "read 5 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_2BIT_BIT10_8), aocec_rd_reg(AO_CEC_TXTIME_2BIT_BIT7_0));
    //7 bit
    hdmi_print(INF, CEC "read 7 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_17MS_BIT10_8), aocec_rd_reg(AO_CEC_TXTIME_17MS_BIT7_0));
}

void cec_arbit_bit_time_set(unsigned bit_set, unsigned time_set, unsigned flag)
{   //11bit:bit[10:0]
    if (flag)
        hdmi_print(INF, CEC "bit_set:0x%x;time_set:0x%x \n", bit_set, time_set);
    switch (bit_set)
    {
        case 3:
            //3 bit
            if (flag)
                hdmi_print(INF, CEC "read 3 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_4BIT_BIT10_8),aocec_rd_reg(AO_CEC_TXTIME_4BIT_BIT7_0));
            aocec_wr_reg(AO_CEC_TXTIME_4BIT_BIT7_0, time_set & 0xff);
            aocec_wr_reg(AO_CEC_TXTIME_4BIT_BIT10_8, (time_set >> 8) & 0x7);
            if (flag)
                hdmi_print(INF, CEC "write 3 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_4BIT_BIT10_8),aocec_rd_reg(AO_CEC_TXTIME_4BIT_BIT7_0));
            break;
            //5 bit
        case 5:
            if (flag)
                hdmi_print(INF, CEC "read 5 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_2BIT_BIT10_8), aocec_rd_reg(AO_CEC_TXTIME_2BIT_BIT7_0));
            aocec_wr_reg(AO_CEC_TXTIME_2BIT_BIT7_0, time_set & 0xff);
            aocec_wr_reg(AO_CEC_TXTIME_2BIT_BIT10_8, (time_set >> 8) & 0x7);
            if (flag)
                hdmi_print(INF, CEC "write 5 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_2BIT_BIT10_8), aocec_rd_reg(AO_CEC_TXTIME_2BIT_BIT7_0));
            break;
            //7 bit
        case 7:
            if (flag)
                hdmi_print(INF, CEC "read 7 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_17MS_BIT10_8), aocec_rd_reg(AO_CEC_TXTIME_17MS_BIT7_0));
            aocec_wr_reg(AO_CEC_TXTIME_17MS_BIT7_0, time_set & 0xff);
            aocec_wr_reg(AO_CEC_TXTIME_17MS_BIT10_8, (time_set >> 8) & 0x7);
            if (flag)
                hdmi_print(INF, CEC "write 7 bit:0x%x%x \n", aocec_rd_reg(AO_CEC_TXTIME_17MS_BIT10_8), aocec_rd_reg(AO_CEC_TXTIME_17MS_BIT7_0));
            break;
        default:
            break;
    }
}

void dumpaocecreg(void)
{
    int i;

    for (i = 0; i < 0x95; i ++)
        printk("aocecreg[0x%x]: 0x%x\n", i, (unsigned int)aocec_rd_reg(i));
    for (i = 0; i < 0x5; i ++)
        printk("aoreg[0x%x]: 0x%x\n", (0x104 + i*4), aml_read_reg32(P_AO_CEC_GEN_CNTL + i*4));
}

void raocec(unsigned int addr)
{
    printk("aocecreg[0x%x]: 0x%x\n", addr, (unsigned int)aocec_rd_reg(addr));
}

void waocec(unsigned int addr, unsigned int value)
{
    aocec_wr_reg(addr, value);
    printk("aocecreg[0x%x]: 0x%x\n", addr, (unsigned int)aocec_rd_reg(addr));
}

// DELETE LATER, TEST ONLY
void cec_test_(unsigned int cmd)
{
    ;
}

void cec_keep_reset(void)
{
    aml_write_reg32(P_AO_CEC_GEN_CNTL, 0x1);
}
