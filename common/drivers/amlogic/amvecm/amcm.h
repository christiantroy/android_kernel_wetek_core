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

#ifndef __AM_CM_H
#define __AM_CM_H


#include "linux/amlogic/vframe.h"
#include "linux/amlogic/cm.h"

typedef struct cm_regs_s {
    unsigned int val  : 32;
    unsigned int reg  : 14;
    unsigned int port :  2; // port port_addr            port_data            remark
                        // 0    NA                   NA                   direct access
                        // 1    VPP_CHROMA_ADDR_PORT VPP_CHROMA_DATA_PORT CM port registers
                        // 2    NA                   NA                   reserved
                        // 3    NA                   NA                   reserved
    unsigned int bit  :  5;
    unsigned int wid  :  5;
    unsigned int mode :  1; // 0:read, 1:write
    unsigned int rsv  :  5;
} cm_regs_t;


// ***************************************************************************
// *** IOCTL-oriented functions *********************************************
// ***************************************************************************
void am_set_regmap(struct am_regs_s *p);
extern void amcm_enable(void);
extern void amcm_level_sel(unsigned int cm_level);
extern void cm2_frame_size_patch(unsigned int width,unsigned int height);
extern void cm2_frame_switch_patch(void);
extern void cm_latch_process(void);
extern int cm_load_reg(am_regs_t *arg);

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
#define WRITE_VPP_REG(x,val)				WRITE_VCBUS_REG(x,val)
#define WRITE_VPP_REG_BITS(x,val,start,length)		WRITE_VCBUS_REG_BITS(x,val,start,length)
#define READ_VPP_REG(x)					READ_VCBUS_REG(x)
#define READ_VPP_REG_BITS(x,start,length)		READ_VCBUS_REG_BITS(x,start,length)
#else
#define WRITE_VPP_REG(x,val)				WRITE_CBUS_REG(x,val)
#define WRITE_VPP_REG_BITS(x,val,start,length)		WRITE_CBUS_REG_BITS(x,val,start,length)
#define READ_VPP_REG(x)					READ_CBUS_REG(x)
#define READ_VPP_REG_BITS(x,start,length)		READ_CBUS_REG_BITS(x,start,length)
#endif

#endif

