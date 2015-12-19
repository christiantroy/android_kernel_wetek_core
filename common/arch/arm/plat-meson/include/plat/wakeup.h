/*
 * arch/arm/plat-meson/include/plat/wakeup.h
 *
 * MESON wakeup source
 *
 * Copyright (C) 2012-2014 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __PLAT_MESON_WAKEUP_H
#define __PLAT_MESON_WAKEUP_H

 /*Wake up source flag which be writen to AO_STATUS_REG2*/

#define FLAG_WAKEUP_PWRKEY		0x1234abcd //IR, power key, low power, adapter plug in/out and so on, are all use this flag.
#define FLAG_WAKEUP_ALARM		0x12345678
#define FLAG_WAKEUP_WIFI		0x12340001
#define FLAG_WAKEUP_BT			0x12340002
#define FLAG_WAKEUP_PWROFF		0x12340003

/*add new wakeup source flag here...*/

#endif //__PLAT_MESON_WAKEUP_H