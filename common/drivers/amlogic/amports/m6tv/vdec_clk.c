/*
 * AMLOGIC Audio/Video streaming port driver.
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
 :*
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <mach/am_regs.h>

#include "../vdec_reg.h"

#include "../amports_config.h"

#include "../vdec.h"
#include "../vdec_clk.h"

/*
HHI_VDEC_CLK_CNTL..
bits,9~11:
0x106d[11:9] :
0 for fclk_div2,  1GHz
1 for fclk_div3,  2G/3Hz
2 for fclk_div5, 2G/5Hz
3 for fclk_div7, 2G/7HZ

4 for mp1_clk_out
5 for ddr_pll_clk

bit0~6: div N=bit[0-7]+1
bit8: vdec.gate
*/
#define VDEC_166M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (5))
#define VDEC_200M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (4))
#define VDEC_250M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (3))
#define VDEC_333M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (0 << 9) | (1 << 8) | (2))

#define VDEC_CLOCK_GATE_ON()  WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 8, 1, 1)
#define VDEC_CLOCK_GATE_OFF() WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL, 8, 0, 1)

static int clock_level[VDEC_MAX+1];

void vdec_clock_enable(void)
{
    VDEC_200M();
    clock_level[VDEC_1] = 0;
    WRITE_VREG(DOS_GCLK_EN0, 0xffffffff);
}

void vdec_clock_hi_enable(void)
{
    VDEC_250M();
    clock_level[VDEC_1] = 1;
    WRITE_VREG(DOS_GCLK_EN0, 0xffffffff);
}

int vdec_clock_level(vdec_type_t core)
{
    if (core >= VDEC_MAX)
        return 0; 

    return clock_level[core];
}

