#ifndef __VPU_H__
#define __VPU_H__

//#define CONFIG_VPU_DYNAMIC_ADJ

#define VPU_MOD_START	100
typedef enum {
	VPU_VIU_OSD1 = VPU_MOD_START, //reg0[1:0]
	VPU_VIU_OSD2,        //reg0[3:2]
	VPU_VIU_VD1,         //reg0[5:4]
	VPU_VIU_VD2,         //reg0[7:6]
	VPU_VIU_CHROMA,      //reg0[9:8]
	VPU_VIU_OFIFO,       //reg0[11:10]
	VPU_VIU_SCALE,       //reg0[13:12]
	VPU_VIU_OSD_SCALE,   //reg0[15:14]
	VPU_VIU_VDIN0,       //reg0[17:16]
	VPU_VIU_VDIN1,       //reg0[19:18]
	VPU_VIU_PROT1,       //reg0[21:20]
	VPU_VIU_PROT2,       //reg0[23:22]
	VPU_VIU_PROT3,       //reg0[25:24]
	VPU_DI_PRE,          //reg0[27:26]
	VPU_DI_POST,         //reg0[29:28]
	VPU_SHARP,           //reg0[31:30]
	VPU_VIU2_OSD1,       //reg1[1:0]
	VPU_VIU2_OSD2,       //reg1[3:2]
	VPU_VIU2_VD1,        //reg1[5:4]
	VPU_VIU2_CHROMA,     //reg1[7:6]
	VPU_VIU2_OFIFO,      //reg1[9:8]
	VPU_VIU2_SCALE,      //reg0[11:10]
	VPU_VIU2_OSD_SCALE,  //reg0[13:12]
	VPU_VENCP,           //reg1[21:20]
	VPU_VENCL,           //reg1[23:22]
	VPU_VENCI,           //reg1[25:24]
	VPU_ISP,             //reg1[27:26]
	VPU_CVD2,            //reg1[29:28]
	VPU_ATV_DMD,         //reg1[31:30]
	VPU_MAX,
} vpu_mod_t;

//VPU memory power down
#define VPU_MEM_POWER_ON		0
#define VPU_MEM_POWER_DOWN		1

extern unsigned int get_vpu_clk(void);
extern unsigned int get_vpu_clk_vmod(unsigned int vmod);
extern int request_vpu_clk_vmod(unsigned int vclk, unsigned int vmod);
extern int release_vpu_clk_vmod(unsigned int vmod);

extern void switch_vpu_mem_pd_vmod(unsigned int vmod, int flag);
extern int get_vpu_mem_pd_vmod(unsigned int vmod);
#endif
