/*
 * Driver for the amlogic vpu controller
 *
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <plat/io.h>
#include <mach/am_regs.h>
#include <mach/vpu.h>
#ifdef CONFIG_SMP
#include <mach/smp.h>
#endif
#include <linux/amlogic/vout/vinfo.h>

#define VPU_VERION	"v03"

typedef enum {
	VPU_CHIP_M8 = 0,
	VPU_CHIP_M8M2,
	VPU_CHIP_MAX,
} VPU_Chip_t;

static const char* vpu_chip_name[] = {
	"m8",
	"m8m2",
	"invalid",
};

//#define LIMIT_VPU_CLK_LOW
static spinlock_t vpu_lock;
static spinlock_t vpu_mem_lock;
static DEFINE_MUTEX(vpu_mutex);

static const char* vpu_mod_table[]={
	"viu_osd1",
	"viu_osd2",
	"viu_vd1",
	"viu_vd2",
	"viu_chroma",
	"viu_ofifo",
	"viu_scale",
	"viu_osd_scale",
	"viu_vdin0",
	"viu_vdin1",
	"pic_rot1",
	"pic_rot2",
	"pic_rot3",
	"di_pre",
	"di_post",
	"viu2_osd1",
	"viu2_osd2",
	"viu2_vd1",
	"viu2_chroma",
	"viu2_ofifo",
	"viu2_scale",
	"viu2_osd_scale",
	"vencp",
	"vencl",
	"venci",
	"isp",
	"none",
};

typedef struct {
	VPU_Chip_t   chip_type;
	unsigned int clk_level_dft;
	unsigned int clk_level_max;
	unsigned int clk_level;
	unsigned mem_pd0;
	unsigned mem_pd1;
}VPU_Conf_t;

#define CLK_LEVEL_DFT		4
#define CLK_LEVEL_MAX		8	//limit max clk to 425M
static unsigned int vpu_clk_setting[][10][3] = {
    {   //frequency    clk_mux   div //M8
        {106250000,    1,        7}, //0
        {127500000,    2,        3}, //1
        {159375000,    0,        3}, //2
        {212500000,    1,        3}, //3
        {255000000,    2,        1}, //4
        {283333000,    1,        2}, //5
        {318750000,    0,        1}, //6
        {364300000,    3,        0}, //7
        {425000000,    1,        1}, //8
        {510000000,    2,        0}, //9
    },
    {   //frequency    clk_mux   div //M8M2
        {106250000,    1,        7}, //0
        {127500000,    2,        3}, //1
        {159375000,    0,        3}, //2
        {212500000,    1,        3}, //3
        {255000000,    2,        1}, //4
        {283333000,    1,        2}, //5
        {318750000,    0,        1}, //6
        {364000000,    3,        0}, //7 //use gp_pll
        {425000000,    1,        1}, //8
        {510000000,    2,        0}, //9
    },
};

static unsigned int vpu_clk_vmod[] = {
	0,	//VPU_VIU_OSD1,
	0,	//VPU_VIU_OSD2,
	0,	//VPU_VIU_VD1,
	0,	//VPU_VIU_VD2,
	0,	//VPU_VIU_CHROMA,
	0,	//VPU_VIU_OFIFO,
	0,	//VPU_VIU_SCALE,
	0,	//VPU_VIU_OSD_SCALE,
	0,	//VPU_VIU_VDIN0,
	0,	//VPU_VIU_VDIN1,
	0,	//VPU_PIC_ROT1,
	0,	//VPU_PIC_ROT2,
	0,	//VPU_PIC_ROT3,
	0,	//VPU_DI_PRE,
	0,	//VPU_DI_POST,
	0,	//VPU_VIU2_OSD1,
	0,	//VPU_VIU2_OSD2,
	0,	//VPU_VIU2_VD1,
	0,	//VPU_VIU2_CHROMA,
	0,	//VPU_VIU2_OFIFO,
	0,	//VPU_VIU2_SCALE,
	0,	//VPU_VIU2_OSD_SCALE,
	0,	//VPU_VENCP,
	0,	//VPU_VENCL,
	0,	//VPU_VENCI,
	0,	//VPU_ISP,
	0,	//VPU_MAX,
};

static VPU_Conf_t vpu_config = {
	.mem_pd0 = 0,
	.mem_pd1 = 0,
	.chip_type = VPU_CHIP_MAX,
	.clk_level_dft = CLK_LEVEL_DFT,
	.clk_level_max = CLK_LEVEL_MAX,
	.clk_level = CLK_LEVEL_DFT,
};

static vpu_mod_t get_vpu_mod(unsigned int vmod)
{
	unsigned int vpu_mod;

	if (vmod < VPU_MOD_START) {
		switch (vmod) {
			case VMODE_480P:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_480P_59HZ:
#endif
            case VMODE_480P_RPT:
			case VMODE_576P:
			case VMODE_576P_RPT:
			case VMODE_720P:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_720P_59HZ:
#endif
			case VMODE_1080I:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_1080I_59HZ:
#endif
			case VMODE_1080P:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_1080P_59HZ:
#endif
			case VMODE_720P_50HZ:
			case VMODE_1080I_50HZ:
			case VMODE_1080P_50HZ:
			case VMODE_1080P_24HZ:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_1080P_23HZ:
#endif
			case VMODE_4K2K_30HZ:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_4K2K_29HZ:
#endif
			case VMODE_4K2K_25HZ:
			case VMODE_4K2K_24HZ:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
			case VMODE_4K2K_23HZ:
#endif
			case VMODE_4K2K_SMPTE:
			case VMODE_VGA:
			case VMODE_SVGA:
			case VMODE_XGA:
			case VMODE_SXGA:
				vpu_mod = VPU_VENCP;
				break;
			case VMODE_480I:
			case VMODE_480I_RPT:
			case VMODE_576I:
			case VMODE_576I_RPT:
			case VMODE_480CVBS:
			case VMODE_576CVBS:
				vpu_mod = VPU_VENCI;
				break;
			case VMODE_LCD:
			case VMODE_LVDS_1080P:
			case VMODE_LVDS_1080P_50HZ:
				vpu_mod = VPU_VENCL;
				break;
			default:
				vpu_mod = VPU_MAX;
				break;
		}
	}
	else if ((vmod >= VPU_MOD_START) && (vmod < VPU_MAX)) {
		vpu_mod = vmod;
	}
	else {
		vpu_mod = VPU_MAX;
	}

	return vpu_mod;
} 

#ifdef CONFIG_VPU_DYNAMIC_ADJ
static unsigned int get_vpu_clk_level_max_vmod(void)
{
	unsigned int max_level;
	int i;
	
	max_level = 0;
	for (i=VPU_MOD_START; i<VPU_MAX; i++) {
		if (vpu_clk_vmod[i-VPU_MOD_START] > max_level)
			max_level = vpu_clk_vmod[i-VPU_MOD_START];
	}
	
	return max_level;
}
#endif

static unsigned int get_vpu_clk_level(unsigned int video_clk)
{
	unsigned int video_bw;
	unsigned clk_level;
	int i;
	
	video_bw = video_clk + 1000000;

	for (i=0; i<vpu_config.clk_level_max; i++) {
		if (video_bw <= vpu_clk_setting[vpu_config.chip_type][i][0])
			break;
	}
	clk_level = i;

	return clk_level;
}

unsigned int get_vpu_clk(void)
{
	unsigned int clk_freq;
	unsigned int clk_source, clk_div;
	
	switch ((aml_read_reg32(P_HHI_VPU_CLK_CNTL) >> 9) & 0x7) {
		case 0:
			clk_source = 637500000;
			break;
		case 1:
			clk_source = 850000000;
			break;
		case 2:
			clk_source = 510000000;
			break;
		case 3:
			if (vpu_config.chip_type == VPU_CHIP_M8M2)
				clk_source = 364000000;
			else
				clk_source = 364300000;
			break;
		default:
			clk_source = 0;
			break;
	}
	
	clk_div = ((aml_read_reg32(P_HHI_VPU_CLK_CNTL) >> 0) & 0x7f) + 1;
	clk_freq = clk_source / clk_div;
	
	return clk_freq;
}

static int switch_gp_pll(int flag)
{
	int cnt = 100;
	int ret = 0;
	
	if (flag) { //enable gp_pll
		//M=182, N=3, OD=2. gp_pll=24*M/N/2^OD=364M
		aml_write_reg32(P_HHI_GP_PLL_CNTL, 0x206b6); //set gp_pll frequency fixed to 364M
		aml_set_reg32_bits(P_HHI_GP_PLL_CNTL, 1, 30, 1); //enable
		do{
			udelay(10);
			aml_set_reg32_bits(P_HHI_GP_PLL_CNTL, 1, 29, 1); //reset
			udelay(50);
			aml_set_reg32_bits(P_HHI_GP_PLL_CNTL, 0, 29, 1); //release reset
			udelay(50);
		}while(((aml_read_reg32(P_HHI_GP_PLL_CNTL)&(1<<31)) == 0) && (cnt > 0));
		if (cnt == 0) {
			ret = 1;
			aml_set_reg32_bits(P_HHI_GP_PLL_CNTL, 0, 30, 1);
			printk("[error]: GP_PLL lock failed, can't use the clk source!\n");
		}
	}
	else { //disable gp_pll
		aml_set_reg32_bits(P_HHI_GP_PLL_CNTL, 0, 30, 1);
	}
	
	return ret;
}

static int _adjust_vpu_clk(void *vconf1)
{
	unsigned int clk_level;
	VPU_Conf_t *vconf=(VPU_Conf_t *)vconf1;	
	clk_level = vconf->clk_level;
	aml_write_reg32(P_HHI_VPU_CLK_CNTL, ((1 << 8) | (vpu_clk_setting[vpu_config.chip_type][clk_level][1] << 9) | (vpu_clk_setting[vpu_config.chip_type][clk_level][2] << 0)));
	
	return 0;
}

static int adjust_vpu_clk(unsigned int clk_level)
{
	unsigned long flags = 0;
	int ret = 0;
	
	spin_lock_irqsave(&vpu_lock, flags);
	
	vpu_config.clk_level = clk_level;
	switch (vpu_config.chip_type) {
		case VPU_CHIP_M8M2:
			if (clk_level == 7) {
				ret = switch_gp_pll(1);
				if (ret)
					clk_level = 6;
			}
			else
				ret = switch_gp_pll(0);
			
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, vpu_clk_setting[vpu_config.chip_type][0][1], 25, 3);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, vpu_clk_setting[vpu_config.chip_type][0][2], 16, 7);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 1, 24, 1);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 1, 31, 1);
			udelay(10);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 0, 8, 1);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, vpu_clk_setting[vpu_config.chip_type][clk_level][1], 9, 3);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, vpu_clk_setting[vpu_config.chip_type][clk_level][2], 0, 7);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 1, 8, 1);
			udelay(20);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 0, 31, 1);
			aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 0, 24, 1);
			break;
		case VPU_CHIP_M8:
#ifdef CONFIG_SMP
			try_exclu_cpu_exe(_adjust_vpu_clk, &vpu_config);
#else
			_adjust_vpu_clk(&vpu_config);
#endif
			break;
		default:
			break;
	}
	
	if (((aml_read_reg32(P_HHI_VPU_CLK_CNTL) >> 8) & 1) == 0)
		aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 1, 8, 1);
	
	printk("set vpu clk: %uHz, readback: %uHz(0x%x)\n", vpu_clk_setting[vpu_config.chip_type][clk_level][0], get_vpu_clk(), (aml_read_reg32(P_HHI_VPU_CLK_CNTL)));
	
	spin_unlock_irqrestore(&vpu_lock, flags);
	return ret;
}

static int set_vpu_clk(unsigned int vclk)
{
	int ret = 0;
	unsigned clk_level;
	mutex_lock(&vpu_mutex);
	
	if (vclk >= 100) {	//regard as vpu_clk
		clk_level = get_vpu_clk_level(vclk);
	}
	else {	//regard as clk_level
		clk_level = vclk;
	}

	if (clk_level >= vpu_config.clk_level_max) {
		ret = 1;
		printk("set vpu clk out of supported range\n");
		goto set_vpu_clk_limit;
	}
#ifdef LIMIT_VPU_CLK_LOW
	else if (clk_level < vpu_config.clk_level_dft) {
		ret = 3;
		printk("set vpu clk less than system default\n");
		goto set_vpu_clk_limit;
	}
#endif
	
	if ((((aml_read_reg32(P_HHI_VPU_CLK_CNTL) >> 9) & 0x7) != vpu_clk_setting[vpu_config.chip_type][clk_level][1]) || (((aml_read_reg32(P_HHI_VPU_CLK_CNTL) >> 0) & 0x7f) != vpu_clk_setting[vpu_config.chip_type][clk_level][2])) {
		adjust_vpu_clk(clk_level);
	}
	
set_vpu_clk_limit:
	mutex_unlock(&vpu_mutex);
	return ret;
}

//***********************************************//
//VPU_CLK control
//***********************************************//
/*
 *  Function: get_vpu_clk_vmod
 *      Get vpu clk holding frequency with specified vmod
 *
 *	Parameters:
 *      vmod - unsigned int, must be one of the following constants:
 *                 VMODE, VMODE is supported by VOUT
 *                 VPU_MOD, supported by vpu_mod_t
 *
 *  Returns:
 *      unsigned int, vpu clk frequency unit in Hz
 * 
 *	Example:
 *      video_clk = get_vpu_clk_vmod(VMODE_720P);
 *      video_clk = get_vpu_clk_vmod(VPU_VIU_OSD1);
 *
*/
unsigned int get_vpu_clk_vmod(unsigned int vmod)
{
	unsigned int vpu_mod;
	unsigned int vpu_clk;
	mutex_lock(&vpu_mutex);
	
	vpu_mod = get_vpu_mod(vmod);
	if ((vpu_mod >= VPU_MOD_START) && (vpu_mod < VPU_MAX)) {
		vpu_clk = vpu_clk_vmod[vpu_mod - VPU_MOD_START];
		vpu_clk = vpu_clk_setting[vpu_config.chip_type][vpu_clk][0];
	}
	else {
		vpu_clk = 0;
		printk("unsupport vmod\n");
	}
	
	mutex_unlock(&vpu_mutex);
	return vpu_clk;
}

/*
 *  Function: request_vpu_clk_vmod
 *      Request a new vpu clk holding frequency with specified vmod
 *      Will change vpu clk if the max level in all vmod vpu clk holdings is unequal to current vpu clk level
 *
 *	Parameters:
 *      vclk - unsigned int, vpu clk frequency unit in Hz
 *      vmod - unsigned int, must be one of the following constants:
 *                 VMODE, VMODE is supported by VOUT
 *                 VPU_MOD, supported by vpu_mod_t
 *
 *  Returns:
 *      int, 0 for success, 1 for failed
 * 
 *	Example:
 *      ret = request_vpu_clk_vmod(100000000, VMODE_720P);
 *      ret = request_vpu_clk_vmod(300000000, VPU_VIU_OSD1);
 *
*/
int request_vpu_clk_vmod(unsigned int vclk, unsigned int vmod)
{
	int ret = 0;
#ifdef CONFIG_VPU_DYNAMIC_ADJ
	unsigned clk_level;
	unsigned vpu_mod;
	
	mutex_lock(&vpu_mutex);
	
	if (vclk >= 100) {	//regard as vpu_clk
		clk_level = get_vpu_clk_level(vclk);
	}
	else {	//regard as clk_level
		clk_level = vclk;
	}

	if (clk_level >= vpu_config.clk_level_max) {
		ret = 1;
		printk("set vpu clk out of supported range\n");
		goto request_vpu_clk_limit;
	}
	
	vpu_mod = get_vpu_mod(vmod);
	if (vpu_mod == VPU_MAX) {
		ret = 2;
		printk("unsupport vmod\n");
		goto request_vpu_clk_limit;
	}
	
	vpu_clk_vmod[vpu_mod - VPU_MOD_START] = clk_level;
	printk("request vpu clk holdings: %s %uHz\n", vpu_mod_table[vpu_mod - VPU_MOD_START], vpu_clk_setting[vpu_config.chip_type][clk_level][0]);

	clk_level = get_vpu_clk_level_max_vmod();
	if (clk_level != vpu_config.clk_level) {
		adjust_vpu_clk(clk_level);
	}

request_vpu_clk_limit:
	mutex_unlock(&vpu_mutex);
#endif
	return ret;
}

/*
 *  Function: release_vpu_clk_vmod
 *      Release vpu clk holding frequency to 0 with specified vmod
 *      Will change vpu clk if the max level in all vmod vpu clk holdings is unequal to current vpu clk level
 *
 *	Parameters:
 *      vmod - unsigned int, must be one of the following constants:
 *                 VMODE, VMODE is supported by VOUT
 *                 VPU_MOD, supported by vpu_mod_t
 *
 *  Returns:
 *      int, 0 for success, 1 for failed
 * 
 *	Example:
 *      ret = release_vpu_clk_vmod(VMODE_720P);
 *      ret = release_vpu_clk_vmod(VPU_VIU_OSD1);
 *
*/
int release_vpu_clk_vmod(unsigned int vmod)
{
	int ret = 0;
#ifdef CONFIG_VPU_DYNAMIC_ADJ
	unsigned clk_level;
	unsigned vpu_mod;
	
	mutex_lock(&vpu_mutex);
	
	clk_level = 0;
	vpu_mod = get_vpu_mod(vmod);
	if (vpu_mod == VPU_MAX) {
		ret = 2;
		printk("unsupport vmod\n");
		goto release_vpu_clk_limit;
	}
	
	vpu_clk_vmod[vpu_mod - VPU_MOD_START] = clk_level;
	printk("release vpu clk holdings: %s\n", vpu_mod_table[vpu_mod - VPU_MOD_START]);

	clk_level = get_vpu_clk_level_max_vmod();
	if (clk_level != vpu_config.clk_level) {
		adjust_vpu_clk(clk_level);
	}

release_vpu_clk_limit:
	mutex_unlock(&vpu_mutex);
#endif
	return ret;
}

//***********************************************//
//VPU_MEM_PD control
//***********************************************//
#define VPU_MEM_PD_MASK		0x3

/*
 *  Function: switch_vpu_mem_pd_vmod
 *      switch vpu memory power down by specified vmod
 *
 *	Parameters:
 *      vmod - unsigned int, must be one of the following constants:
 *                 VMODE, VMODE is supported by VOUT
 *                 VPU_MOD, supported by vpu_mod_t
 *      flag - int, on/off switch flag, must be one of the following constants:
 *                 VPU_MEM_POWER_ON
 *                 VPU_MEM_POWER_DOWN
 * 
 *	Example:
 *      switch_vpu_mem_pd_vmod(VMODE_720P, VPU_MEM_POWER_ON);
 *      switch_vpu_mem_pd_vmod(VPU_VIU_OSD1, VPU_MEM_POWER_DOWN);
 *
*/
void switch_vpu_mem_pd_vmod(unsigned int vmod, int flag)
{
	unsigned vpu_mod;
	unsigned vpu_mem_bit = 0;
	unsigned long flags = 0;
	spin_lock_irqsave(&vpu_mem_lock, flags);
	
	flag = (flag > 0) ? 1 : 0;
	
	vpu_mod = get_vpu_mod(vmod);
	if ((vpu_mod >= VPU_MOD_START) && (vpu_mod <= VPU_DI_POST)) {
		vpu_mem_bit = (vpu_mod - VPU_MOD_START) * 2;
		if (flag)
			aml_set_reg32_bits(P_HHI_VPU_MEM_PD_REG0, VPU_MEM_PD_MASK, vpu_mem_bit, 2);
		else
			aml_set_reg32_bits(P_HHI_VPU_MEM_PD_REG0, 0, vpu_mem_bit, 2);
	}
	else if ((vpu_mod >= VPU_VIU2_OSD1) && (vpu_mod <= VPU_VIU2_OSD_SCALE)) {
		vpu_mem_bit = (vpu_mod - VPU_VIU2_OSD1) * 2;
		if (flag)
			aml_set_reg32_bits(P_HHI_VPU_MEM_PD_REG1, VPU_MEM_PD_MASK, vpu_mem_bit, 2);
		else
			aml_set_reg32_bits(P_HHI_VPU_MEM_PD_REG1, 0, vpu_mem_bit, 2);
	}
	else if ((vpu_mod >= VPU_VENCP) && (vpu_mod < VPU_MAX)) {
		vpu_mem_bit = (vpu_mod - VPU_VENCP + 10) * 2;
		if (flag) {
			aml_set_reg32_bits(P_HHI_VPU_MEM_PD_REG1, VPU_MEM_PD_MASK, vpu_mem_bit, 2);
		}
		else {
			aml_set_reg32_bits(P_HHI_VPU_MEM_PD_REG1, 0, vpu_mem_bit, 2);
		}
	}
	else {
		printk("switch_vpu_mem_pd: unsupport vpu mod\n");
	}
	//printk("switch_vpu_mem_pd: %s %s\n", vpu_mod_table[vpu_mod - VPU_MOD_START], ((flag > 0) ? "OFF" : "ON"));
	spin_unlock_irqrestore(&vpu_mem_lock, flags);
}
//***********************************************//

int get_vpu_mem_pd_vmod(unsigned int vmod)
{
	unsigned vpu_mod;
	unsigned vpu_mem_bit = 0;

	vpu_mod = get_vpu_mod(vmod);

	if ((vpu_mod >= VPU_MOD_START) && (vpu_mod <= VPU_DI_POST)) {
		vpu_mem_bit = (vpu_mod - VPU_MOD_START) * 2;
		return (aml_get_reg32_bits(P_HHI_VPU_MEM_PD_REG0, vpu_mem_bit, 2) == 0) ? VPU_MEM_POWER_ON : VPU_MEM_POWER_DOWN;
	}
	else if ((vpu_mod >= VPU_VIU2_OSD1) && (vpu_mod <= VPU_VIU2_OSD_SCALE)) {
		vpu_mem_bit = (vpu_mod - VPU_VIU2_OSD1) * 2;
		return (aml_get_reg32_bits(P_HHI_VPU_MEM_PD_REG1, vpu_mem_bit, 2) == 0) ? VPU_MEM_POWER_ON : VPU_MEM_POWER_DOWN;
	}
	else if ((vpu_mod >= VPU_VENCP) && (vpu_mod < VPU_MAX)) {
		vpu_mem_bit = (vpu_mod - VPU_VENCP + 10) * 2;
		return (aml_get_reg32_bits(P_HHI_VPU_MEM_PD_REG1, vpu_mem_bit, 2) == 0) ? VPU_MEM_POWER_ON : VPU_MEM_POWER_DOWN;
	}
	else {
		return -1;
	}
}

//***********************************************//
//VPU sysfs function
//***********************************************//
static const char * vpu_usage_str =
{"Usage:\n"
"	echo get > clk ; print current vpu clk\n"
"	echo set <vclk> > clk ; force to set vpu clk\n"
"	echo dump [vmod] > clk ; dump vpu clk by vmod, [vmod] is unnecessary\n"
"	echo request <vclk> <vmod> > clk ; request vpu clk holding by vmod\n"
"	echo release <vmod> > clk ; release vpu clk holding by vmod\n"
"\n"
"	request & release will change vpu clk if the max level in all vmod vpu clk holdings is unequal to current vpu clk level.\n" 
"	vclk both support level and frequency value unit in Hz.\n"    
"	vclk level & frequency:\n"
"		0: 106.25M		1: 127.5M		2: 159.375M\n"
"		3: 182.15M		4: 212.5M		5: 255M\n"
"		6: 318.75M		7: 364.3M\n"//		8: 425M\n"
//"		9: 510M			10: 637.5M\n"
};

static ssize_t vpu_debug_help(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", vpu_usage_str);
}

static ssize_t vpu_debug(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret;
	int i;
	unsigned tmp[2];
	
	switch (buf[0])	{
		case 'g':	//get
			printk("get current vpu clk: %uHz\n", get_vpu_clk());
			break;
		case 's':	//set
			tmp[0] = 4;
			ret = sscanf(buf, "set %u", &tmp[0]);
			if (tmp[0] > 100) {
				printk("set vpu clk frequency: %uHz\n", tmp[0]);
			}
			else {
				printk("set vpu clk level: %u\n", tmp[0]);
			}
			set_vpu_clk(tmp[0]);
			break;
		case 'r':
			if (buf[2] == 'q') {	//request
				tmp[0] = 0;
				tmp[1] = VPU_MAX;
				ret = sscanf(buf, "request %u %u", &tmp[0], &tmp[1]);
				request_vpu_clk_vmod(tmp[0], tmp[1]);
			}
			else if (buf[2] == 'l') {	//release
				tmp[0] = VPU_MAX;
				ret = sscanf(buf, "release %u", &tmp[0]);
				release_vpu_clk_vmod(tmp[0]);
			}
			break;
		case 'd':
			tmp[0] = VPU_MAX;
			ret = sscanf(buf, "dump %u", &tmp[0]);
			tmp[1] = get_vpu_mod(tmp[0]);
			printk("vpu clk holdings:\n");
			if (tmp[1] == VPU_MAX) {
				for (i=VPU_MOD_START; i<VPU_MAX; i++) {
					printk("%s:		%uHz(%u)\n", vpu_mod_table[i - VPU_MOD_START], vpu_clk_setting[vpu_config.chip_type][vpu_clk_vmod[i - VPU_MOD_START]][0], vpu_clk_vmod[i - VPU_MOD_START]);
				}
			}
			else {
				printk("%s:		%uHz(%u)\n", vpu_mod_table[tmp[1] - VPU_MOD_START], vpu_clk_setting[vpu_config.chip_type][vpu_clk_vmod[tmp[1] - VPU_MOD_START]][0], vpu_clk_vmod[tmp[1] - VPU_MOD_START]);
			}
			break;
		default:
			printk("wrong format of vpu debug command.\n");
			break;
	}	
	
	if (ret != 1 || ret !=2)
		return -EINVAL;
	
	return count;
	//return 0;
}

static struct class_attribute vpu_debug_class_attrs[] = {
	__ATTR(clk, S_IRUGO | S_IWUSR, vpu_debug_help, vpu_debug),
	__ATTR(help, S_IRUGO | S_IWUSR, vpu_debug_help, NULL),
	__ATTR_NULL
};

static struct class aml_vpu_debug_class = {
	.name = "vpu",
	.class_attrs = vpu_debug_class_attrs,
};
//*********************************************************//
#if 0
static void vpu_driver_init(void)
{
    set_vpu_clk(vpu_config.clk_level);

    aml_set_reg32_bits(P_AO_RTI_GEN_PWR_SLEEP0, 0, 8, 1); // [8] power on
    aml_write_reg32(P_HHI_VPU_MEM_PD_REG0, vpu_config.mem_pd0);
    aml_write_reg32(P_HHI_VPU_MEM_PD_REG1, vpu_config.mem_pd1);
    aml_set_reg32_bits(P_HHI_MEM_PD_REG0, 0, 8, 8); // MEM-PD
    udelay(2);

    //Reset VIU + VENC
    //Reset VENCI + VENCP + VADC + VENCL
    //Reset HDMI-APB + HDMI-SYS + HDMI-TX + HDMI-CEC
    aml_write_reg32(P_RESET0_MASK, aml_read_reg32(P_RESET0_MASK) & (~((0x1 << 5) | (0x1<<10))));
    aml_write_reg32(P_RESET4_MASK, aml_read_reg32(P_RESET4_MASK) & (~((0x1 << 6) | (0x1<<7) | (0x1<<9) | (0x1<<13))));
    aml_write_reg32(P_RESET2_MASK, aml_read_reg32(P_RESET2_MASK) & (~((0x1 << 2) | (0x1<<3) | (0x1<<11) | (0x1<<15))));
    aml_write_reg32(P_RESET2_REGISTER, ((0x1 << 2) | (0x1<<3) | (0x1<<11) | (0x1<<15)));
    aml_write_reg32(P_RESET4_REGISTER, ((0x1 << 6) | (0x1<<7) | (0x1<<9) | (0x1<<13)));    // reset this will cause VBUS reg to 0
    aml_write_reg32(P_RESET0_REGISTER, ((0x1 << 5) | (0x1<<10)));
    aml_write_reg32(P_RESET4_REGISTER, ((0x1 << 6) | (0x1<<7) | (0x1<<9) | (0x1<<13)));
    aml_write_reg32(P_RESET2_REGISTER, ((0x1 << 2) | (0x1<<3) | (0x1<<11) | (0x1<<15)));
    aml_write_reg32(P_RESET0_MASK, aml_read_reg32(P_RESET0_MASK) | ((0x1 << 5) | (0x1<<10)));
    aml_write_reg32(P_RESET4_MASK, aml_read_reg32(P_RESET4_MASK) | ((0x1 << 6) | (0x1<<7) | (0x1<<9) | (0x1<<13)));
    aml_write_reg32(P_RESET2_MASK, aml_read_reg32(P_RESET2_MASK) | ((0x1 << 2) | (0x1<<3) | (0x1<<11) | (0x1<<15)));

    //Remove VPU_HDMI ISO
    aml_set_reg32_bits(P_AO_RTI_GEN_PWR_SLEEP0, 0, 9, 1); // [9] VPU_HDMI
}
static void vpu_driver_disable(void)
{
    vpu_config.mem_pd0 = aml_read_reg32(P_HHI_VPU_MEM_PD_REG0);
    vpu_config.mem_pd1 = aml_read_reg32(P_HHI_VPU_MEM_PD_REG1);

    // Power down VPU_HDMI
    // Enable Isolation
    aml_set_reg32_bits(P_AO_RTI_GEN_PWR_SLEEP0, 1, 9, 1); // ISO
    //Power off memory
    aml_write_reg32(P_HHI_VPU_MEM_PD_REG0, 0xffffffff);
    aml_write_reg32(P_HHI_VPU_MEM_PD_REG1, 0xffffffff);
    aml_set_reg32_bits(P_HHI_MEM_PD_REG0, 0xff, 8, 8); // HDMI MEM-PD

    //Power down VPU domain
    aml_set_reg32_bits(P_AO_RTI_GEN_PWR_SLEEP0, 1, 8, 1); //PDN

    aml_set_reg32_bits(P_HHI_VPU_CLK_CNTL, 0, 8, 1);
}
#endif
#ifdef CONFIG_PM
static int vpu_suspend(struct platform_device *pdev, pm_message_t state)
{
	//vpu_driver_disable();
	return 0;
}

static int vpu_resume(struct platform_device *pdev)
{
	//vpu_driver_init();
	return 0;
}
#endif

static void detect_vpu_chip(void)
{
	if (IS_MESON_M8M2_CPU)
		vpu_config.chip_type = VPU_CHIP_M8M2;
	else if (IS_MESON_M8_CPU)
		vpu_config.chip_type = VPU_CHIP_M8;
	else
		vpu_config.chip_type = VPU_CHIP_MAX;
	
	vpu_config.clk_level_dft = CLK_LEVEL_DFT;
	vpu_config.clk_level_max = CLK_LEVEL_MAX;
	
	printk("vpu driver detect cpu type: %s\n", vpu_chip_name[vpu_config.chip_type]);
}

static int get_vpu_config(struct platform_device *pdev)
{
	int ret=0;
	unsigned int val;
	struct device_node *vpu_np;
	
	//mesonfb_np = of_find_node_by_name(NULL,"mesonfb");
	vpu_np = pdev->dev.of_node;
	if (!vpu_np) {
		printk("don't find match vpu node\n");
		return -1;
	}

	ret = of_property_read_u32(vpu_np,"clk_level",&val);
	if(ret){
		printk("don't find to match clk_level, use default setting.\n");
	}
	else {
		if (val >= vpu_config.clk_level_max) {
			printk("vpu clk_level in dts is out of support range, use default setting\n");
			val = vpu_config.clk_level_dft;;
		}
		
		vpu_config.clk_level = val;
		printk("load vpu_clk in dts: %uHz(%u)\n", vpu_clk_setting[vpu_config.chip_type][vpu_config.clk_level][0], vpu_config.clk_level);
	}
	
	return ret;
}

static struct of_device_id vpu_of_table[]=
{
	{
		.compatible="amlogic,vpu",
	},
	{},
};

static int vpu_probe(struct platform_device *pdev)
{	
	int ret;
	
	spin_lock_init(&vpu_lock);
	spin_lock_init(&vpu_mem_lock);
	
	printk("VPU driver version: %s\n", VPU_VERION);
	detect_vpu_chip();
	get_vpu_config(pdev);
	set_vpu_clk(vpu_config.clk_level);
	
	ret = class_register(&aml_vpu_debug_class);
	if(ret){
		printk("class register aml_vpu_debug_class fail!\n");
	}
	
	printk("%s OK\n", __FUNCTION__);
	return 0;
}

static int vpu_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver vpu_driver = {
	.driver = {
		.name = "vpu",
		.owner = THIS_MODULE,
		.of_match_table=of_match_ptr(vpu_of_table),
	},
	.probe = vpu_probe,
	.remove = vpu_remove,
#ifdef CONFIG_PM
	.suspend    = vpu_suspend,
	.resume     = vpu_resume,
#endif
};

static int __init vpu_init(void)
{
	return platform_driver_register(&vpu_driver);
}

static void __exit vpu_exit(void)
{
	platform_driver_unregister(&vpu_driver);
}

postcore_initcall(vpu_init);
module_exit(vpu_exit);

MODULE_DESCRIPTION("m8 vpu control");
MODULE_LICENSE("GPL v2");
