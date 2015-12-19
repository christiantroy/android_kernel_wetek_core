/*
 * Amlogic Meson HDMI Transmitter Driver
 * hdmitx driver-----------HDMI_TX
 * Copyright (C) 2013 Amlogic, Inc.
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
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/am_regs.h>
#include <mach/clock.h>
#include <mach/power_gate.h>
#include <linux/clk.h>
#include <mach/clock.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/enc_clk_config.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_compliance.h>

typedef struct {
    char *ReceiverBrandName;
    char *ReceiverProductName;
    unsigned char blk0_chksum;
}special_tv;

static special_tv special_N_6144x2_tv_tab[]= {
    /*SONY KDL-32R300B*/
    {
        .ReceiverBrandName="SNY",
        .ReceiverProductName="SONY",
        .blk0_chksum=0xf8,
    },
    /*TCL L19F3270B*/
    {
        .ReceiverBrandName="TCL",
        .ReceiverProductName="MST6M16",
        .blk0_chksum=0xa9,
    },
    /*Panasonic TH-32A400C*/
    {
        .ReceiverBrandName="MEI",
        .ReceiverProductName="Panasonic-TV",
        .blk0_chksum=0x28,
    },
};

static special_tv samsung_future_tv_tab[]= {
    /*UA50HU7000JXXZ*/
    {
        .ReceiverBrandName="SAM",
        .ReceiverProductName="SAMSUNG",
    },
};


/*
 * # cat /sys/class/amhdmitx/amhdmitx0/edid
 * Receiver Brand Name: GSM
 * Receiver Product Name: LG
 * blk0 chksum: 0xe7
 *
 * recoginze_tv()
 * parameters:
 *      brand_name: the name of "Receiver Brand Name"
 *      prod_name: the name of "Receiver Product Name"
 *      blk0_chksum: the value of blk0 chksum
 */
static int recoginze_tv(hdmitx_dev_t* hdev, char *brand_name, char *prod_name, unsigned char blk0_chksum)
{
    if ((strncmp(hdev->RXCap.ReceiverBrandName, brand_name, strlen(brand_name)) == 0) && \
        (strncmp(hdev->RXCap.ReceiverProductName, prod_name, strlen(prod_name)) == 0) && \
        (hdev->RXCap.blk0_chksum == blk0_chksum))
        return 1;
    else
        return 0;
}

static int is_special_tv = 0;
int hdmitx_is_special_tv(void)
{
    if (is_special_tv)
        return 1;
    else
        return 0;
}

static int is_support_4k_60(rx_cap_t *pRXCap)
{
    int i;

    for (i = 0 ; i < pRXCap->VIC_count; i++) {
        switch (pRXCap->VIC[i]) {
        case HDMI_3840x2160p60_16x9:
        case HDMI_3840x2160p50_16x9:
        case HDMI_4096x2160p50_256x135:
        case HDMI_4096x2160p60_256x135:
            printk("support 4k 60/50\n");
            return 1;
            break;
        default:
            break;
        }
    }
    return 0;
}

/*
 * hdmitx_special_handler_video()
 */
void hdmitx_special_handler_video(hdmitx_dev_t* hdev)
{
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(samsung_future_tv_tab); i++) {
        if ((strncmp(hdev->RXCap.ReceiverBrandName, samsung_future_tv_tab[i].ReceiverBrandName, strlen(samsung_future_tv_tab[i].ReceiverBrandName)) == 0)
            && (strncmp(hdev->RXCap.ReceiverProductName, samsung_future_tv_tab[i].ReceiverProductName, strlen(samsung_future_tv_tab[i].ReceiverProductName)) == 0)
            && (is_support_4k_60(&hdev->RXCap))) {
            is_special_tv = 1;
            return;
        }
    }
    is_special_tv = 0;
}

/*
 * hdmitx_special_handler_audio()
 */
void hdmitx_special_handler_audio(hdmitx_dev_t* hdev)
{
    int i = 0;
    for(i = 0; i < ARRAY_SIZE(special_N_6144x2_tv_tab); i++) {
        if (recoginze_tv(hdev, special_N_6144x2_tv_tab[i].ReceiverBrandName, special_N_6144x2_tv_tab[i].ReceiverProductName, special_N_6144x2_tv_tab[i].blk0_chksum))
			hdev->HWOp.CntlMisc(hdev, MISC_COMP_AUDIO, COMP_AUDIO_SET_N_6144x2);
    }

#if 0
	// TODO

	if (recoginze_tv(hdev, "SAM", "SAMSUNG", 0x22)) {
        hdev->HWOp.CntlMisc(hdev, MISC_COMP_AUDIO, COMP_AUDIO_SET_N_6144x2);
    }
    if (recoginze_tv(hdev, "GSM", "LG", 0xE7)) {
        hdev->HWOp.CntlMisc(hdev, MISC_COMP_AUDIO, COMP_AUDIO_SET_N_6144x3);
    }
#endif
}
