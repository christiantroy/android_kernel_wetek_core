/*
 * Amlogic Apollo
 * tv display control driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:   jianfeng_wang@amlogic
 *		   
 *		   
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vinfo.h>
#include <mach/am_regs.h>
#include <asm/uaccess.h>
#include <linux/amlogic/major.h>
#include "tvconf.h"
#include "tvmode.h"
#include "vout_log.h"
#include <linux/amlogic/amlog.h>
#include <mach/power_gate.h>
#include <mach/cpu.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif


static    disp_module_info_t    *info;

SET_TV2_CLASS_ATTR(vdac_setting,parse_vdac_setting)


/*****************************
*	default settings :
*	Y    -----  DAC1
*	PB  -----  DAC2
*	PR  -----  DAC0
*
*	CVBS  	---- DAC1
*	S-LUMA    ---- DAC2
*	S-CHRO	----  DAC0
******************************/

static const tvmode_t vmode_tvmode_tab[] =
{
    TVMODE_480I, TVMODE_480I_RPT, TVMODE_480CVBS, TVMODE_480P,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_480P_59HZ,
#endif
    TVMODE_480P_RPT, TVMODE_576I, TVMODE_576I_RPT, TVMODE_576CVBS, TVMODE_576P, TVMODE_576P_RPT, TVMODE_720P,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_720P_59HZ , // for 720p 59.94hz
#endif
    TVMODE_1080I,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_1080I_59HZ,
#endif
    TVMODE_1080P,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_1080P_59HZ , // for 1080p 59.94hz
#endif
    TVMODE_720P_50HZ, TVMODE_1080I_50HZ, TVMODE_1080P_50HZ,TVMODE_1080P_24HZ, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_1080P_23HZ , // for 1080p 23.97hz
#endif
    TVMODE_4K2K_30HZ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_4K2K_29HZ , // for 4k2k 29.97hz
#endif
    TVMODE_4K2K_25HZ, TVMODE_4K2K_24HZ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_4K2K_23HZ , // for 4k2k 23.97hz
#endif
    TVMODE_4K2K_SMPTE,
    TVMODE_VGA, TVMODE_SVGA, TVMODE_XGA, TVMODE_SXGA
};


static const vinfo_t tv_info[] = 
{
    { /* VMODE_480I */
        .name              = "480i",
	.mode              = VMODE_480I,
        .width             = 720,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_480P_59HZ */
        .name              = "480p59hz",
        .mode              = VMODE_480P_59HZ,
        .width             = 720,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 27000000,
    },
#endif
    { /* VMODE_480I_RPT */
        .name              = "480i_rpt",
        .mode              = VMODE_480I_RPT,
        .width             = 720,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
     },
     { /* VMODE_480CVBS*/
        .name              = "480cvbs",
        .mode              = VMODE_480CVBS,
        .width             = 720,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_480P */
        .name              = "480p",
        .mode              = VMODE_480P,
        .width             = 720,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_480P_RPT */
        .name              = "480p_rpt",
        .mode              = VMODE_480P_RPT,
        .width             = 720,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576I */
        .name              = "576i",
        .mode              = VMODE_576I,
        .width             = 720,
        .height            = 576,
        .field_height      = 288,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576I_RPT */
        .name              = "576i_rpt",
        .mode              = VMODE_576I_RPT,
        .width             = 720,
        .height            = 576,
        .field_height      = 288,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576I */
        .name              = "576cvbs",
        .mode              = VMODE_576CVBS,
        .width             = 720,
        .height            = 576,
        .field_height      = 288,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576P */
        .name              = "576p",
        .mode              = VMODE_576P,
        .width             = 720,
        .height            = 576,
        .field_height      = 576,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576P_RPT */
        .name              = "576p_rpt",
        .mode              = VMODE_576P_RPT,
        .width             = 720,
        .height            = 576,
        .field_height      = 576,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_720P */
        .name              = "720p",
        .mode              = VMODE_720P,
        .width             = 1280,
        .height            = 720,
        .field_height      = 720,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_720P_59HZ */
        .name              = "720p59hz",
        .mode              = VMODE_720P_59HZ,
        .width             = 1280,
        .height            = 720,
        .field_height      = 720,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 74250000,
    },
#endif
    { /* VMODE_1080I */
        .name              = "1080i",
        .mode              = VMODE_1080I,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 540,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_1080I_59HZ */
        .name              = "1080i59hz",
        .mode              = VMODE_1080I_59HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 540,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 74250000,
    },
#endif
    { /* VMODE_1080P */
        .name              = "1080p",
        .mode              = VMODE_1080P,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 148500000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_1080P_59HZ */
        .name              = "1080p59hz",
        .mode              = VMODE_1080P_59HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 148500000,
    },
#endif
    { /* VMODE_720P_50hz */
        .name              = "720p50hz",
        .mode              = VMODE_720P_50HZ,
        .width             = 1280,
        .height            = 720,
        .field_height      = 720,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
    { /* VMODE_1080I_50HZ */
        .name              = "1080i50hz",
        .mode              = VMODE_1080I_50HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 540,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
    { /* VMODE_1080P_50HZ */
        .name              = "1080p50hz",
        .mode              = VMODE_1080P_50HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 148500000,
    },
    { /* VMODE_1080P_24HZ */
        .name              = "1080p24hz",
        .mode              = VMODE_1080P_24HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 24,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_1080P_23HZ */
        .name              = "1080p23hz",
        .mode              = VMODE_1080P_23HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 2397,
        .sync_duration_den = 100,
        .video_clk         = 74250000,
    },
#endif
    { /* VMODE_4K2K_30HZ */
        .name              = "4k2k30hz",
        .mode              = TVMODE_4K2K_30HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 30,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_4K2K_29HZ */
        .name              = "4k2k29hz",
        .mode              = TVMODE_4K2K_29HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 2997,
        .sync_duration_den = 100,
        .video_clk         = 297000000,
    },
#endif
    { /* VMODE_4K2K_25HZ */
        .name              = "4k2k25hz",
        .mode              = TVMODE_4K2K_25HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 25,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
    { /* VMODE_4K2K_24HZ */
        .name              = "4k2k24hz",
        .mode              = TVMODE_4K2K_24HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 24,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    { /* VMODE_4K2K_23HZ */
        .name              = "4k2k23hz",
        .mode              = TVMODE_4K2K_23HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 2397,
        .sync_duration_den = 100,
        .video_clk         = 297000000,
    },
#endif
    { /* VMODE_4K2K_SMPTE */
        .name              = "4k2ksmpte",
        .mode              = TVMODE_4K2K_SMPTE,
        .width             = 4096,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 24,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
    { /* VMODE_vga */
        .name              = "vga",
        .mode              = VMODE_VGA,
        .width             = 640,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 25175000,
    },
    { /* VMODE_SVGA */
        .name              = "svga",
        .mode              = VMODE_SVGA,
        .width             = 800,
        .height            = 600,
        .field_height      = 600,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 40000000,
    },
    { /* VMODE_XGA */
        .name              = "xga",
        .mode              = VMODE_XGA,
        .width             = 1024,
        .height            = 768,
        .field_height      = 768,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 65000000,
    },
    { /* VMODE_sxga */
        .name              = "sxga",
        .mode              = VMODE_SXGA,
        .width             = 1280,
        .height            = 1024,
        .field_height      = 1024,
        .aspect_ratio_num  = 5,
        .aspect_ratio_den  = 4,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 108000000,
    },
	{ /* VMODE_wsxga */
        .name              = "wsxga",
        .mode              = VMODE_WSXGA,
        .width             = 1440,
        .height            = 900,
        .field_height      = 900,
        .aspect_ratio_num  = 8,
        .aspect_ratio_den  = 5,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 88750000,
    },
	{ /* VMODE_fhdvga */
        .name              = "fhdvga",
        .mode              = VMODE_FHDVGA,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 148500000,
    },
};

static const struct file_operations am_tv_fops = {
	.open	= NULL,  
	.read	= NULL,//am_tv_read, 
	.write	= NULL, 
	.unlocked_ioctl	= NULL,//am_tv_ioctl, 
	.release	= NULL, 	
	.poll		= NULL,
};

static const vinfo_t *get_valid_vinfo(char  *mode)
{
	const vinfo_t * vinfo = NULL;
	int  i,count=ARRAY_SIZE(tv_info);
	int mode_name_len=0;
	
	for(i=0;i<count;i++)
	{
		if(strncmp(tv_info[i].name,mode,strlen(tv_info[i].name))==0)
		{
			if((vinfo==NULL)||(strlen(tv_info[i].name)>mode_name_len)){
			    vinfo = &tv_info[i];
			    mode_name_len = strlen(tv_info[i].name);
			}
		}
	}
	return vinfo;
}

static const vinfo_t *tv_get_current_info(void)
{
	return info->vinfo;
}

static int tv_set_current_vmode(vmode_t mod)
{
	if ((mod&VMODE_MODE_BIT_MASK)> VMODE_SXGA)
		return -EINVAL;

	info->vinfo = &tv_info[mod];
	if(mod&VMODE_LOGO_BIT_MASK)  return 0;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	switch_vpu_mem_pd_vmod(info->vinfo->mode, VPU_MEM_POWER_ON);
	request_vpu_clk_vmod(info->vinfo->video_clk, info->vinfo->mode);
#endif
	tvoutc_setmode2(vmode_tvmode_tab[mod]);
	//change_vdac_setting2(get_current_vdac_setting(),mod);
	return 0;
}

static vmode_t tv_validate_vmode(char *mode)
{
	const vinfo_t *info = get_valid_vinfo(mode);
	
	if (info)
		return info->mode;
	
	return VMODE_MAX;
}
static int tv_vmode_is_supported(vmode_t mode)
{
	int  i,count=ARRAY_SIZE(tv_info);
	mode&=VMODE_MODE_BIT_MASK;
	for(i=0;i<count;i++)
	{
		if(tv_info[i].mode==mode)
		{
			return true;
		}
	}
	return false;
}
static int tv_module_disable(vmode_t cur_vmod)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if (info->vinfo) {
		release_vpu_clk_vmod(info->vinfo->mode);
		switch_vpu_mem_pd_vmod(info->vinfo->mode, VPU_MEM_POWER_DOWN);
	}
#endif

	video_dac_disable();
	return 0;
}
#ifdef  CONFIG_PM
static int tv_suspend(int pm_event)
{
	video_dac_disable();
	return 0;
}
static int tv_resume(int pm_event)
{
	video_dac_enable(0xff);
	tv_set_current_vmode(info->vinfo->mode);
	return 0;
}
#endif 
static vout_server_t tv_server={
	.name = "vout2_tv_server",
	.op = {	
		.get_vinfo=tv_get_current_info,
		.set_vmode=tv_set_current_vmode,
		.validate_vmode=tv_validate_vmode,
		.vmode_is_supported=tv_vmode_is_supported,
		.disable = tv_module_disable,
#ifdef  CONFIG_PM  
		.vout_suspend=tv_suspend,
		.vout_resume=tv_resume,
#endif	
	},
};

/***************************************************
**
**	The first digit control component Y output DAC number
**	The 2nd digit control component U output DAC number
**	The 3rd digit control component V output DAC number
**	The 4th digit control composite CVBS output DAC number
**	The 5th digit control s-video Luma output DAC number
**	The 6th digit control s-video chroma output DAC number
** 	examble :
**		echo  120120 > /sys/class/display/vdac_setting
**		the first digit from the left side .	
******************************************************/
static void  parse_vdac_setting(char *para) 
{
	int  i;
	char  *pt=strstrip(para);
	int len=strlen(pt);
	u32  vdac_sequence=get_current_vdac_setting2();
	
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"origin vdac setting:0x%x,strlen:%d\n",vdac_sequence,len);
	if(len!=6)
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"can't parse vdac settings\n");
		return ;
	}
	vdac_sequence=0;
	for(i=0;i<6;i++)
	{
		vdac_sequence<<=4;
		vdac_sequence|=*pt -'0';
		pt++;
	}
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"current vdac setting:0x%x\n",vdac_sequence);
	
	change_vdac_setting2(vdac_sequence,get_current_vmode2());
}
static  struct  class_attribute   *tv_attr[]={
&class_TV2_attr_vdac_setting,
};
static int  create_tv_attr(disp_module_info_t* info)
{
	//create base class for display
	int  i;

	info->base_class=class_create(THIS_MODULE,info->name);
	if(IS_ERR(info->base_class))
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create tv display class fail\n");
		return  -1 ;
	}
	//create  class attr
	for(i=0;i<ARRAY_SIZE(tv_attr);i++)
	{
		if ( class_create_file(info->base_class,tv_attr[i]))
		{
			amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create disp attribute %s fail\n",tv_attr[i]->attr.name);
		}
	}
	sprintf(vdac_setting,"%x",get_current_vdac_setting2());
	return   0;
}
static int __init tv_init_module(void)
{
	int  ret ;

	info=(disp_module_info_t*)kmalloc(sizeof(disp_module_info_t),GFP_KERNEL) ;
	if (!info)
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"can't alloc display info struct\n");
		return -ENOMEM;
	}
	
	memset(info, 0, sizeof(disp_module_info_t));

	sprintf(info->name,TV_CLASS_NAME) ;
	ret=register_chrdev(TV2_CONF_MAJOR,info->name,&am_tv_fops);
	if(ret <0) 
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"register char dev tv error\n");
		return  ret ;
	}
	info->major=TV2_CONF_MAJOR;
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"major number %d for disp\n",ret);
	if(vout2_register_server(&tv_server))
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"register tv module server fail\n");
	}
	else
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"register tv module server ok\n");
	}
	create_tv_attr(info);
	return 0;

}
static __exit void tv_exit_module(void)
{
	int i;
	
	if(info->base_class)
	{
		for(i=0;i<ARRAY_SIZE(tv_attr);i++)
		{
			class_remove_file(info->base_class,tv_attr[i]) ;
		}
		
		class_destroy(info->base_class);
	}	
	if(info)
	{
		unregister_chrdev(info->major,info->name)	;
		kfree(info);
	}
	vout2_unregister_server(&tv_server);
	
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"exit tv module\n");
}


arch_initcall(tv_init_module);
module_exit(tv_exit_module);

MODULE_DESCRIPTION("display configure  module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jianfeng_wang <jianfeng.wang@amlogic.com>");

