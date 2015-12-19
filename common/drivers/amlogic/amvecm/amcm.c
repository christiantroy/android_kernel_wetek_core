/*
 * Color Management
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <mach/am_regs.h>
#include <linux/module.h>
#include <linux/amlogic/cm.h>
#include <linux/amlogic/aml_common.h>
#include <linux/amlogic/vframe.h>
#include <linux/amlogic/amvecm.h>
#include <asm/uaccess.h>
#include "cm_regs.h"
#include "amcm.h"
#include "amcm_regmap.h"

#define pr_amcm_dbg(fmt, args...)\
	do{\
		if(debug_amcm)\
			printk("AMCM: " fmt, ## args);\
	}while(0)

static bool debug_amcm = 0;
module_param(debug_amcm, bool, 0664);
MODULE_PARM_DESC(debug_amcm, "\n debug_amcm \n");

static bool debug_regload = 0;
module_param(debug_regload, bool, 0664);
MODULE_PARM_DESC(debug_regload, "\n debug_regload \n");

static int cm_level = 1;//0:optimize;1:enhancement
module_param(cm_level, int, 0664);
MODULE_PARM_DESC(cm_level, "\n selcet cm lever \n");

int cm_en = 1;//0:disabel;1:enable
module_param(cm_en, int, 0664);
MODULE_PARM_DESC(cm_en, "\n enable or disable cm \n");

#if 0
struct cm_region_s cm_region;
struct cm_top_s    cm_top;
struct cm_demo_s   cm_demo;
#endif
static int cm_level_last = 0xff;//0:optimize;1:enhancement
unsigned int cm2_patch_flag = 0;
unsigned int cm_size;
static struct am_regs_s amregs0;
static struct am_regs_s amregs1;
static struct am_regs_s amregs2;
static struct am_regs_s amregs3;
static struct am_regs_s amregs4;
static struct am_regs_s amregs5;

extern unsigned int vecm_latch_flag;

void am_set_regmap(struct am_regs_s *p)
{
	unsigned short i;
	unsigned int temp = 0;
	for (i=0; i<p->length; i++) {
		switch (p->am_reg[i].type){
		case REG_TYPE_PHY:
			break;
		case REG_TYPE_CBUS:
			if (p->am_reg[i].mask == 0xffffffff)
				WRITE_CBUS_REG(p->am_reg[i].addr, p->am_reg[i].val);
			else
				WRITE_CBUS_REG(p->am_reg[i].addr, (READ_CBUS_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
			break;
		case REG_TYPE_APB:
			if (p->am_reg[i].mask == 0xffffffff)
				WRITE_APB_REG(p->am_reg[i].addr, p->am_reg[i].val);
			else
				WRITE_APB_REG(p->am_reg[i].addr, (READ_APB_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
			break;
		case REG_TYPE_MPEG:
			if (p->am_reg[i].mask == 0xffffffff)
				WRITE_MPEG_REG(p->am_reg[i].addr, p->am_reg[i].val);
			else
				WRITE_MPEG_REG(p->am_reg[i].addr, (READ_MPEG_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
			break;
		case REG_TYPE_AXI:
			if (p->am_reg[i].mask == 0xffffffff)
				WRITE_AXI_REG(p->am_reg[i].addr, p->am_reg[i].val);
			else
				WRITE_AXI_REG(p->am_reg[i].addr, (READ_AXI_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
			break;
		case REG_TYPE_INDEX_VPPCHROMA:
			/*  add for vm2 demo frame size setting */
			if (p->am_reg[i].addr == 0x20f) {
				if ((p->am_reg[i].val & 0xff) != 0) {
					cm2_patch_flag = p->am_reg[i].val;
					p->am_reg[i].val = p->am_reg[i].val & 0xffffff00;
				}
				else
					cm2_patch_flag = 0;
			}
			WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, p->am_reg[i].addr);
			if (p->am_reg[i].mask == 0xffffffff)
				WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, p->am_reg[i].val);
			else{
				temp = READ_VPP_REG(VPP_CHROMA_DATA_PORT);
				WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, p->am_reg[i].addr);
				WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, (temp & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
			}
			break;
		case REG_TYPE_INDEX_GAMMA:
			break;
		case VALUE_TYPE_CONTRAST_BRIGHTNESS:
			break;
		case REG_TYPE_INDEX_VPP_COEF:
			if (((p->am_reg[i].addr&0xf) == 0)||((p->am_reg[i].addr&0xf) == 0x8)){
				WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, p->am_reg[i].addr);
				WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, p->am_reg[i].val);
			}
			else
				WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, p->am_reg[i].val);
			break;
#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESONG9TV)
		case REG_TYPE_VCBUS:
			if (p->am_reg[i].mask == 0xffffffff)
				WRITE_VCBUS_REG(p->am_reg[i].addr, p->am_reg[i].val);
			else
				WRITE_VCBUS_REG(p->am_reg[i].addr, (READ_VCBUS_REG(p->am_reg[i].addr) & (~(p->am_reg[i].mask))) | (p->am_reg[i].val & p->am_reg[i].mask));
			break;
#endif
		default:
			break;
		}
	}
	return;
}

void amcm_enable(void)
{
	WRITE_VPP_REG_BITS(VPP_MISC,0,28,1);//CM manage enable
}

void cm_regmap_latch(am_regs_t *am_regs,unsigned int reg_map)
{
	am_set_regmap(am_regs);
	vecm_latch_flag &= ~reg_map;
	pr_amcm_dbg("\n[amcm..] load reg %d table OK!!!\n",reg_map);
}

void amcm_level_sel(unsigned int cm_level)
{
	if(cm_level == 1)
		am_set_regmap(&cmreg_lever1);
	else if(cm_level == 2)
		am_set_regmap(&cmreg_lever2);
	else if(cm_level == 3)
		am_set_regmap(&cmreg_lever3);
	else if(cm_level == 4)
		am_set_regmap(&cmreg_enhancement);
	else
		am_set_regmap(&cmreg_optimize);
	WRITE_VPP_REG_BITS(VPP_MISC,1,28,1);//CM manage enable
}

void cm2_frame_size_patch(unsigned int width,unsigned int height)
{
	unsigned int vpp_size;
	/*check if the cm2 enable/disable to config the cm2 size*/
	if(!(READ_VPP_REG(VPP_MISC)&(0x1<<28)))
		return;
	vpp_size = width|(height << 16);
	if(cm_size == 0){
		WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, 0x205);
		cm_size = READ_VPP_REG(VPP_CHROMA_DATA_PORT);
	}
	if (cm_size != vpp_size) {
		WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, 0x205);
		WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, vpp_size);
		WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, 0x209);
		WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, width<<15);
		WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, 0x20a);
		WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, height<<16);
		cm_size =  vpp_size;
		pr_amcm_dbg("\n[amcm..]cm2_frame_patch: set cm2 framesize %x, set demo mode  %x\n",
				vpp_size, cm2_patch_flag);
	}
}

/*
* set the frame size for cm2 demo
*/
void cm2_frame_switch_patch(void)
{
	WRITE_VPP_REG(VPP_CHROMA_ADDR_PORT, 0x20f);
	WRITE_VPP_REG(VPP_CHROMA_DATA_PORT, cm2_patch_flag);
}

void cm_latch_process(void)
{
	/*if ((vecm_latch_flag & FLAG_REG_MAP0) || (vecm_latch_flag & FLAG_REG_MAP1) ||
	(vecm_latch_flag & FLAG_REG_MAP2) || (vecm_latch_flag & FLAG_REG_MAP3) ||
	(vecm_latch_flag & FLAG_REG_MAP4) || (vecm_latch_flag & FLAG_REG_MAP5)){*/
	do{
		if (vecm_latch_flag & FLAG_REG_MAP0){
			cm_regmap_latch(&amregs0,FLAG_REG_MAP0);
			break;
		}
		if (vecm_latch_flag & FLAG_REG_MAP1){
			cm_regmap_latch(&amregs1,FLAG_REG_MAP1);
			break;
		}
		if (vecm_latch_flag & FLAG_REG_MAP2){
			cm_regmap_latch(&amregs2,FLAG_REG_MAP2);
			break;
		}
		if (vecm_latch_flag & FLAG_REG_MAP3){
			cm_regmap_latch(&amregs3,FLAG_REG_MAP3);
			break;
		}
		if (vecm_latch_flag & FLAG_REG_MAP4){
			cm_regmap_latch(&amregs4,FLAG_REG_MAP4);
			break;
		}
		if (vecm_latch_flag & FLAG_REG_MAP5){
			cm_regmap_latch(&amregs5,FLAG_REG_MAP5);
			break;
		}
		if((cm2_patch_flag & 0xff) > 0)
			cm2_frame_switch_patch();
	}while(0);
	if (cm_en&&(cm_level_last != cm_level)){
		cm_level_last = cm_level;
		amcm_level_sel(cm_level);
		pr_amcm_dbg("\n[amcm..] set cm2 load OK!!!\n");
	}
	else if((cm_en == 0)&&(cm_level_last != 0xff)){
		cm_level_last = 0xff;
		amcm_enable();//CM manage disable
	}
}

static int amvecm_regmap_info(am_regs_t *p)
{
	unsigned short i;

	for (i=0; i<p->length; i++) {
		switch (p->am_reg[i].type){
		case REG_TYPE_PHY:
			printk("%s:%d bus type: phy..............\n", __func__,i);
			break;
		case REG_TYPE_CBUS:
			printk("%s:%-3d cbus: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		case REG_TYPE_APB:
			printk("%s:%-3d apb: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		case REG_TYPE_MPEG:
			printk("%s:%-3d mpeg: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		case REG_TYPE_AXI:
			printk("%s:%-3d axi: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		case REG_TYPE_INDEX_VPPCHROMA:
			printk("%s:%-3d chroma: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		case REG_TYPE_INDEX_GAMMA:
			printk("%s:%-3d bus type: REG_TYPE_INDEX_GAMMA..............\n", __func__,i);
			break;
		case VALUE_TYPE_CONTRAST_BRIGHTNESS:
			printk("%s:%-3d bus type: VALUE_TYPE_CONTRAST_BRIGHTNESS..............\n", __func__,i);
			break;
		case REG_TYPE_INDEX_VPP_COEF:
			printk("%s:%-3d coef: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		case REG_TYPE_VCBUS:
			printk("%s:%-3d vcbus: 0x%-4x=0x%-8x (%-5u)=(%-10u) mask=%-8x(%u)\n", __func__,
					i, p->am_reg[i].addr,(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].addr,
					(p->am_reg[i].val & p->am_reg[i].mask),p->am_reg[i].mask,p->am_reg[i].mask);
			break;
		default:
			printk("%s:%3d bus type error!!!bustype = 0x%x................\n", __func__,i, p->am_reg[i].type);
			break;
		}
	}
	return 0;
}

static long amvecm_regmap_set(am_regs_t *regs,am_regs_t *arg,unsigned int reg_map)
{
	int ret = 0;
	if(!(memcpy(regs, arg, sizeof(struct am_regs_s)))) {
		pr_amcm_dbg(KERN_ERR "[amcm..]0x%x load reg errors: can't get buffer lenght\n",reg_map);
		return -EFAULT;
	}
	if (!regs->length || (regs->length > 512)) {
		pr_amcm_dbg(KERN_ERR "[amcm..]0x%x load regs error: buffer length abnormal!!!, length=0x%x \n",
				reg_map,regs->length);
		return -EINVAL;
	}
	pr_amcm_dbg("\n[amcm..]0x%x reg length=0x%x ......\n",reg_map, regs->length);

	if (debug_regload)
		amvecm_regmap_info(regs);

	vecm_latch_flag |= reg_map;
	return ret;
}

int cm_load_reg(am_regs_t *arg)
{
	int ret = 0;
	/*force set cm size to 0,enable check vpp size*/
	cm_size = 0;
	if (!(vecm_latch_flag & FLAG_REG_MAP0)) {
		ret = amvecm_regmap_set(&amregs0,arg,FLAG_REG_MAP0);
	}else if(!(vecm_latch_flag & FLAG_REG_MAP1)){
		ret = amvecm_regmap_set(&amregs1,arg,FLAG_REG_MAP1);
	}else if(!(vecm_latch_flag & FLAG_REG_MAP2)){
		ret = amvecm_regmap_set(&amregs2,arg,FLAG_REG_MAP2);
	}else if(!(vecm_latch_flag & FLAG_REG_MAP3)){
		ret = amvecm_regmap_set(&amregs3,arg,FLAG_REG_MAP3);
	}else if(!(vecm_latch_flag & FLAG_REG_MAP4)){
		ret = amvecm_regmap_set(&amregs4,arg,FLAG_REG_MAP4);
	}else if(!(vecm_latch_flag & FLAG_REG_MAP5)){
		ret = amvecm_regmap_set(&amregs5,arg,FLAG_REG_MAP5);
	}
	return ret;
}

