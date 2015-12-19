/*
 * arch/arm/mach-mesong9tv/cpu.c
 *
 * Copyright (C) 2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/string.h>

#include <mach/am_regs.h>

#include <plat/io.h>
#include <plat/cpu.h>

static int meson_cpu_version[MESON_CPU_VERSION_LVL_MAX+1];

int __init meson_cpu_version_init(void)
{
	unsigned int version,ver;
	unsigned int  *version_map;

	meson_cpu_version[MESON_CPU_VERSION_LVL_MAJOR] =
		aml_read_reg32(P_ASSIST_HW_REV);

	version_map = (unsigned int *)IO_BOOTROM_BASE;
	meson_cpu_version[MESON_CPU_VERSION_LVL_MISC] = version_map[1];

	//version = aml_read_reg32(P_METAL_REVISION);
	version = version_map[1];
	switch (version) {
		case 0x18D7:
			ver = 0xA;
			break;
		case 0x1C64:
			ver = 0xB;
			break;
		default:/*changed?*/
			ver = 0xC;
			break;
	}
	meson_cpu_version[MESON_CPU_VERSION_LVL_MINOR] = ver;
	printk(KERN_INFO "Meson chip version = Rev%X (%X:%X - %X:%X)\n", ver,
		meson_cpu_version[MESON_CPU_VERSION_LVL_MAJOR],
		meson_cpu_version[MESON_CPU_VERSION_LVL_MINOR],
		meson_cpu_version[MESON_CPU_VERSION_LVL_PACK],
		meson_cpu_version[MESON_CPU_VERSION_LVL_MISC]
		);

	return 0;
}
EXPORT_SYMBOL(meson_cpu_version_init);

int get_meson_cpu_version(int level)
{
	if(level >= 0 && level <= MESON_CPU_VERSION_LVL_MAX)
		return meson_cpu_version[level];
	return 0;
}
EXPORT_SYMBOL(get_meson_cpu_version);
