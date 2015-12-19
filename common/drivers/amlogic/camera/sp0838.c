/*
 *sp0838 - This code emulates a real video device with v4l2 api
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <media/videobuf-res.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/wakelock.h>

#include <linux/i2c.h>
#include <media/v4l2-chip-ident.h>
#include <linux/amlogic/camera/aml_cam_info.h>
#include <linux/amlogic/vmapi.h>
#include "common/vm.h"

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
//#include <mach/gpio_data.h>
#include "common/plat_ctrl.h"
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#define SP0838_CAMERA_MODULE_NAME "sp0838"


#define  SP0838_P0_0x30  0x00
//Filter en&dis
#define  SP0838_P0_0x56  0x70
#define  SP0838_P0_0x57  0x10  //filter outdoor
#define  SP0838_P0_0x58  0x10  //filter indoor
#define  SP0838_P0_0x59  0x10  //filter night
#define  SP0838_P0_0x5a  0x02  //smooth outdoor
#define  SP0838_P0_0x5b  0x02  //smooth indoor
#define  SP0838_P0_0x5c  0x20  //smooht night
//outdoor sharpness
#define  SP0838_P0_0x65  0x03
#define  SP0838_P0_0x66  0x01
#define  SP0838_P0_0x67  0x03
#define  SP0838_P0_0x68  0x46
//indoor sharpness
#define  SP0838_P0_0x6b  0x04
#define  SP0838_P0_0x6c  0x01
#define  SP0838_P0_0x6d  0x03
#define  SP0838_P0_0x6e  0x46
//night sharpness
#define  SP0838_P0_0x71  0x05
#define  SP0838_P0_0x72  0x01
#define  SP0838_P0_0x73  0x03
#define  SP0838_P0_0x74  0x46
//color
#define  SP0838_P0_0x7f  0xd7  //R 
#define  SP0838_P0_0x87  0xf8  //B
//satutation
#define  SP0838_P0_0xd8  0x48
#define  SP0838_P0_0xd9  0x48
#define  SP0838_P0_0xda  0x48
#define  SP0838_P0_0xdb  0x48
//AE target
#define  SP0838_P0_0xf7  0x78
#define  SP0838_P0_0xf8  0x63
#define  SP0838_P0_0xf9  0x68
#define  SP0838_P0_0xfa  0x53
//HEQ
#define  SP0838_P0_0xdd  0x70
#define  SP0838_P0_0xde  0x90
//AWB pre gain
#define  SP0838_P1_0x28  0x5a
#define  SP0838_P1_0x29  0x62

//VBLANK
#define  SP0838_P0_0x05  0x00
#define  SP0838_P0_0x06  0x00
//HBLANK
#define  SP0838_P0_0x09  0x01
#define  SP0838_P0_0x0a  0x76





/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT     msecs_to_jiffies(500)  /* 0.5 seconds */

#define SP0838_CAMERA_MAJOR_VERSION 0
#define SP0838_CAMERA_MINOR_VERSION 7
#define SP0838_CAMERA_RELEASE 0
#define SP0838_CAMERA_VERSION \
    KERNEL_VERSION(SP0838_CAMERA_MAJOR_VERSION, SP0838_CAMERA_MINOR_VERSION, SP0838_CAMERA_RELEASE)

MODULE_DESCRIPTION("sp0838 On Board");
MODULE_AUTHOR("amlogic-sh");
MODULE_LICENSE("GPL v2");

#define SP0838_DRIVER_VERSION "SP0838-COMMON-01-140808"

static unsigned video_nr = -1;  /* videoX start number, -1 is autodetect. */

static unsigned debug;
//module_param(debug, uint, 0644);
//MODULE_PARM_DESC(debug, "activates debug info");

static unsigned int vid_limit = 16;
//module_param(vid_limit, uint, 0644);
//MODULE_PARM_DESC(vid_limit, "capture memory limit in megabytes");

static int sp0838_have_open=0;

static int sp0838_h_active=320;
static int sp0838_v_active=240;
static struct v4l2_fract sp0838_frmintervals_active = {
    .numerator = 1,
    .denominator = 15,
};

static struct vdin_v4l2_ops_s *vops;

/* supported controls */
static struct v4l2_queryctrl sp0838_qctrl[] = {
{
        .id            = V4L2_CID_DO_WHITE_BALANCE,
        .type          = V4L2_CTRL_TYPE_MENU,
        .name          = "white balance",
        .minimum       = 0,
        .maximum       = 6,
        .step          = 0x1,
        .default_value = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_EXPOSURE,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "exposure",
        .minimum       = 0,
        .maximum       = 8,
        .step          = 0x1,
        .default_value = 4,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_COLORFX,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "effect",
        .minimum       = 0,
        .maximum       = 6,
        .step          = 0x1,
        .default_value = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_WHITENESS,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "banding",
        .minimum       = 0,
        .maximum       = 1,
        .step          = 0x1,
        .default_value = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_BLUE_BALANCE,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "scene mode",
        .minimum       = 0,
        .maximum       = 1,
        .step          = 0x1,
        .default_value = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_HFLIP,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "flip on horizontal",
        .minimum       = 0,
        .maximum       = 1,
        .step          = 0x1,
        .default_value = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_VFLIP,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "flip on vertical",
        .minimum       = 0,
        .maximum       = 1,
        .step          = 0x1,
        .default_value = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id            = V4L2_CID_ZOOM_ABSOLUTE,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "Zoom, Absolute",
        .minimum       = 100,
        .maximum       = 300,
        .step          = 20,
        .default_value = 100,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },{
        .id     = V4L2_CID_ROTATE,
        .type       = V4L2_CTRL_TYPE_INTEGER,
        .name       = "Rotate",
        .minimum    = 0,
        .maximum    = 270,
        .step       = 90,
        .default_value  = 0,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    }
};

static struct v4l2_frmivalenum sp0838_frmivalenum[]={
    {
        .index      = 0,
        .pixel_format   = V4L2_PIX_FMT_NV21,
        .width      = 640,
        .height     = 480,
        .type       = V4L2_FRMIVAL_TYPE_DISCRETE,
        {
            .discrete   ={
                .numerator  = 1,
                .denominator    = 15,
            }
        }
    },{
        .index      = 1,
        .pixel_format   = V4L2_PIX_FMT_NV21,
        .width      = 1600,
        .height     = 1200,
        .type       = V4L2_FRMIVAL_TYPE_DISCRETE,
        {
            .discrete   ={
                .numerator  = 1,
                .denominator    = 5,
            }
        }
    },
};

struct v4l2_querymenu sp0838_qmenu_wbmode[] = {
    {
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_AUTO,
        .name       = "auto",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_CLOUD,
        .name       = "cloudy-daylight",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_INCANDESCENCE,
        .name       = "incandescent",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_DAYLIGHT,
        .name       = "daylight",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_FLUORESCENT,
        .name       = "fluorescent", 
        .reserved   = 0,
    },{
        .id         = V4L2_CID_DO_WHITE_BALANCE,
        .index      = CAM_WB_FLUORESCENT,
        .name       = "warm-fluorescent", 
        .reserved   = 0,
    },
};

struct v4l2_querymenu sp0838_qmenu_anti_banding_mode[] = {
    {
        .id         = V4L2_CID_POWER_LINE_FREQUENCY,
        .index      = CAM_BANDING_50HZ, 
        .name       = "50hz",
        .reserved   = 0,
    },{
        .id         = V4L2_CID_POWER_LINE_FREQUENCY,
        .index      = CAM_BANDING_60HZ, 
        .name       = "60hz",
        .reserved   = 0,
    },
};

typedef struct {
    __u32   id;
    int     num;
    struct v4l2_querymenu* sp0838_qmenu;
}sp0838_qmenu_set_t;

sp0838_qmenu_set_t sp0838_qmenu_set[] = {
    {
        .id             = V4L2_CID_DO_WHITE_BALANCE,
        .num            = ARRAY_SIZE(sp0838_qmenu_wbmode),
        .sp0838_qmenu   = sp0838_qmenu_wbmode,
    },{
        .id             = V4L2_CID_POWER_LINE_FREQUENCY,
        .num            = ARRAY_SIZE(sp0838_qmenu_anti_banding_mode),
        .sp0838_qmenu   = sp0838_qmenu_anti_banding_mode,
    },
};

static int vidioc_querymenu(struct file *file, void *priv,
                struct v4l2_querymenu *a)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(sp0838_qmenu_set); i++)
    if (a->id && a->id == sp0838_qmenu_set[i].id) {
        for (j = 0; j < sp0838_qmenu_set[i].num; j++)
        if (a->index == sp0838_qmenu_set[i].sp0838_qmenu[j].index) {
            memcpy(a, &( sp0838_qmenu_set[i].sp0838_qmenu[j]),
                sizeof(*a));
            return (0);
        }
    }

    return -EINVAL;
}
#define dprintk(dev, level, fmt, arg...) \
    v4l2_dbg(level, debug, &dev->v4l2_dev, fmt, ## arg)

/* ------------------------------------------------------------------
    Basic structures
   ------------------------------------------------------------------*/

struct sp0838_fmt {
    char  *name;
    u32   fourcc;          /* v4l2 format id */
    int   depth;
};

static struct sp0838_fmt formats[] = {
    {
        .name     = "RGB565 (BE)",
        .fourcc   = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
        .depth    = 16,
    },

    {
        .name     = "RGB888 (24)",
        .fourcc   = V4L2_PIX_FMT_RGB24, /* 24  RGB-8-8-8 */
        .depth    = 24,
    },
    {
        .name     = "BGR888 (24)",
        .fourcc   = V4L2_PIX_FMT_BGR24, /* 24  BGR-8-8-8 */
        .depth    = 24,
    },
    {
        .name     = "12  Y/CbCr 4:2:0",
        .fourcc   = V4L2_PIX_FMT_NV12,
        .depth    = 12,
    },
    {
        .name     = "12  Y/CbCr 4:2:0",
        .fourcc   = V4L2_PIX_FMT_NV21,
        .depth    = 12,
    },
    {
        .name     = "YUV420P",
        .fourcc   = V4L2_PIX_FMT_YUV420,
        .depth    = 12,
    },
    {
        .name     = "YVU420P",
        .fourcc   = V4L2_PIX_FMT_YVU420,
        .depth    = 12,
    }
};

static struct sp0838_fmt *get_format(struct v4l2_format *f) {
    struct sp0838_fmt *fmt;
    unsigned int k;

    for (k = 0; k < ARRAY_SIZE(formats); k++) {
        fmt = &formats[k];
        if (fmt->fourcc == f->fmt.pix.pixelformat)
            break;
    }

    if (k == ARRAY_SIZE(formats))
        return NULL;

    return &formats[k];
}

struct sg_to_addr {
    int pos;
    struct scatterlist *sg;
};

/* buffer for one video frame */
struct sp0838_buffer {
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;

    struct sp0838_fmt        *fmt;

    unsigned int canvas_id;
};

struct sp0838_dmaqueue {
    struct list_head       active;

    /* thread for generating video stream*/
    struct task_struct         *kthread;
    wait_queue_head_t          wq;
    /* Counters to control fps rate */
    int                        frame;
    int                        ini_jiffies;
};

static LIST_HEAD(sp0838_devicelist);

struct sp0838_device {
    struct list_head            sp0838_devicelist;
    struct v4l2_subdev          sd;
    struct v4l2_device          v4l2_dev;

    spinlock_t                 slock;
    struct mutex                mutex;

    int                        users;

    /* various device info */
    struct video_device        *vdev;

    struct sp0838_dmaqueue       vidq;

    /* Several counters */
    unsigned long              jiffies;

    /* Input Number */
    int            input;

    /* platform device data from board initting. */
    aml_cam_info_t  cam_info;

    /* wake lock */
    struct wake_lock    wake_lock;

    /* Control 'registers' */
    int                qctl_regs[ARRAY_SIZE(sp0838_qctrl)];
    vm_init_t vminfo;
};

static inline struct sp0838_device *to_dev(struct v4l2_subdev *sd)
{
    return container_of(sd, struct sp0838_device, sd);
}

struct sp0838_fh {
    struct sp0838_device            *dev;

    /* video capture */
    struct sp0838_fmt            *fmt;
    unsigned int               width, height;
    struct videobuf_queue      vb_vidq;

    struct videobuf_res_privdata res;
    enum v4l2_buf_type         type;
    int            input;   /* Input Number on bars */
    int  stream_on;
    unsigned int        f_flags;
};

/*static inline struct sp0838_fh *to_fh(struct sp0838_device *dev)
{
    return container_of(dev, struct sp0838_fh, dev);
}*/

static struct v4l2_frmsize_discrete sp0838_prev_resolution[] = //should include 320x240 and 640x480, those two size are used for recording
{
    {176,144},
    {320,240},
    {352,288},
    {640,480},
};

static struct v4l2_frmsize_discrete sp0838_pic_resolution[] =
{
    {640,480},
};

/* ------------------------------------------------------------------
    reg spec of SP0838
   ------------------------------------------------------------------*/

struct aml_camera_i2c_fig1_s SP0838_script[] = {
//INIT
    {0xfd,0x00},
    {0x1B,0x02},
    {0x27,0xe8},
    {0x28,0x0B},
    {0x32,0x00},
    {0x22,0xc0},
    {0x26,0x10},
    {0x31,0x10},
    {0x5f,0x11},
    {0xfd,0x01},
    {0x25,0x1a},
    {0x26,0xfb},
    {0x28,SP0838_P1_0x28},
    {0x29,SP0838_P1_0x29},
    {0xfd,0x00},
    {0xe7,0x03},
    {0xe7,0x00},
    {0xfd,0x01},
    {0x31,0x00},
    {0x32,0x18},
    {0x4d,0xdc},
    {0x4e,0x53},
    {0x41,0x8c},
    {0x42,0x57},
    {0x55,0xff},
    {0x56,0x00},
    {0x59,0x82},
    {0x5a,0x00},
    {0x5d,0xff},
    {0x5e,0x6f},
    {0x57,0xff},
    {0x58,0x00},
    {0x5b,0xff},
    {0x5c,0xa8},
    {0x5f,0x75},
    {0x60,0x00},
    /*{0x2d,0x00},
    {0x2e,0x00},
    {0x30,0x00},
    {0x33,0x00},
    {0x34,0x00},
    {0x37,0x00},
    {0x38,0x00},*/
    {0x2f,0x80},
    {0xfd,0x00},
    {0x33,0x6f},
    {0x51,0x3f},
    {0x52,0x09},
    {0x53,0x00},
    {0x54,0x00},
    {0x55,0x10},
    {0x4f,0x08},
    {0x50,0x08},
    {0x57,SP0838_P0_0x57},//Raw filter debut start
    {0x58,SP0838_P0_0x58},
    {0x59,SP0838_P0_0x59},
    {0x56,SP0838_P0_0x56},
    {0x5a,SP0838_P0_0x5a},
    {0x5b,SP0838_P0_0x5b},
    {0x5c,SP0838_P0_0x5c},//Raw filter debut end
    {0x65,SP0838_P0_0x65},//Sharpness debug start
    {0x66,SP0838_P0_0x66},
    {0x67,SP0838_P0_0x67},
    {0x68,SP0838_P0_0x68},
    {0x69,0x7f},
    {0x6a,0x01},
    {0x6b,SP0838_P0_0x6b},
    {0x6c,SP0838_P0_0x6c},
    {0x6d,SP0838_P0_0x6d},//Edge gain normal
    {0x6e,SP0838_P0_0x6e},//Edge gain normal
    {0x6f,0x7f},
    {0x70,0x01},
    {0x71,SP0838_P0_0x71}, //�����
    {0x72,SP0838_P0_0x72}, //��������ֵ
    {0x73,SP0838_P0_0x73}, //��Ե���������
    {0x74,SP0838_P0_0x74}, //��Ե���������
    {0x75,0x7f},
    {0x76,0x01},
    {0xcb,0x07},
    {0xcc,0x04},
    {0xce,0xff},
    {0xcf,0x10},
    {0xd0,0x20},
    {0xd1,0x00},
    {0xd2,0x1c},
    {0xd3,0x16},
    {0xd4,0x00},
    {0xd6,0x1c},
    {0xd7,0x16},
    {0xdd,SP0838_P0_0xdd},//Contrast
    {0xde,SP0838_P0_0xde},//HEQ&Saturation debug end
    #if 0
    {0x7f,0xd7},
    {0x80,0xbc},
    {0x81,0xed},
    {0x82,0xd7},
    {0x83,0xd4},
    {0x84,0xd6},
    {0x85,0xff},
    {0x86,0x89},
    {0x87,0xf8},
    {0x88,0x3c},
    {0x89,0x33},
    {0x8a,0x0f},
    #else //sp_yc 20140626
    {0x7f,SP0838_P0_0x7f},//Color Correction start
    {0x80,0xbc},
    {0x81,0x00},
    {0x82,0xd7},
    {0x83,0xf0},
    {0x84,0xb9},
    {0x85,0xff},
    {0x86,0x89},
    {0x87,SP0838_P0_0x87},
    {0x88,0x0c},
    {0x89,0x33},
    {0x8a,0x0f},
    #endif
    {0x8b,0x0 },
    {0x8c,0x1a},
    {0x8d,0x29},
    {0x8e,0x41},
    {0x8f,0x62},
    {0x90,0x7c},
    {0x91,0x90},
    {0x92,0xa2},
    {0x93,0xaf},
    {0x94,0xbc},
    {0x95,0xc5},
    {0x96,0xcd},
    {0x97,0xd5},
    {0x98,0xdd},
    {0x99,0xe5},
    {0x9a,0xed},
    {0x9b,0xf5},
    {0xfd,0x01},
    {0x8d,0xfd},
    {0x8e,0xff},
    {0xfd,0x00},
    {0xca,0xcf},
    {0xd8,SP0838_P0_0xd8},//UV outdoor
    {0xd9,SP0838_P0_0xd9},//UV indoor
    {0xda,SP0838_P0_0xda},//UV dummy
    {0xdb,SP0838_P0_0xdb},//UV lowlight
    {0xb9,0x00},
    {0xba,0x04},
    {0xbb,0x08},
    {0xbc,0x10},
    {0xbd,0x20},
    {0xbe,0x30},
    {0xbf,0x40},
    {0xc0,0x50},
    {0xc1,0x60},
    {0xc2,0x70},
    {0xc3,0x80},
    {0xc4,0x90},
    {0xc5,0xA0},
    {0xc6,0xB0},
    {0xc7,0xC0},
    {0xc8,0xD0},
    {0xc9,0xE0},
    {0xfd,0x01},
    {0x89,0xf0},
    {0x8a,0xff},
    {0xfd,0x00},
    {0xe8,0x30},
    {0xe9,0x30},
    {0xea,0x40},
    {0xf4,0x1b},
    {0xf5,0x80},
    {0xf7,SP0838_P0_0xf7},//AE target
    {0xf8,SP0838_P0_0xf8},
    {0xf9,SP0838_P0_0xf9},//AE target
    {0xfa,SP0838_P0_0xfa},
    {0xfd,0x01},
    {0x09,0x31},
    {0x0a,0x85},
    {0x0b,0x0b},
    {0x14,0x20},
    {0x15,0x0f},

    //sensor AE settings:
    /*
    {0xfd,0x00},
    {0x05,0x00},
    {0x06,0x00},
    {0x09,0x03},
    {0x0a,0x04},
    {0xf0,0x4a},
    {0xf1,0x00},
    {0xf2,0x59},
    {0xf5,0x72},
    {0xfd,0x01},
    {0x00,0xac},
    {0x0f,0x5a},
    {0x16,0x5a},
    {0x17,0x9c},
    {0x18,0xa4},
    {0x1b,0x5a},
    {0x1c,0xa4},
    {0xb4,0x20},
    {0xb5,0x3a},
    {0xb6,0x46},
    {0xb9,0x40},
    {0xba,0x4f},
    {0xbb,0x47},
    {0xbc,0x45},
    {0xbd,0x43},
    {0xbe,0x42},
    {0xbf,0x42},
    {0xc0,0x42},
    {0xc1,0x41},
    {0xc2,0x41},
    {0xc3,0x41},
    {0xc4,0x41},
    {0xc5,0x70},
    {0xc6,0x41},
    {0xca,0x70},
    {0xcb,0x0c},
    {0xfd,0x00},
    */
    {0xfd,0x00},
    {0x05,0x00},
    {0x06,0x00},
    {0x09,0x01},
    {0x0a,0x76},
    {0xf0,0x62},
    {0xf1,0x00},
    {0xf2,0x5f},
    {0xf5,0x78},
    {0xfd,0x01},//P1
    {0x00,0xba},
    {0x0f,0x60},
    {0x16,0x60},
    {0x17,0xa2},
    {0x18,0xaa},
    {0x1b,0x60},
    {0x1c,0xaa},
    {0xb4,0x20},
    {0xb5,0x3a},
    {0xb6,0x5e},
    {0xb9,0x40},
    {0xba,0x4f},
    {0xbb,0x47},
    {0xbc,0x45},
    {0xbd,0x43},
    {0xbe,0x42},
    {0xbf,0x42},
    {0xc0,0x42},
    {0xc1,0x41},
    {0xc2,0x41},
    {0xc3,0x41},
    {0xc4,0x41},
    {0xc5,0x78},
    {0xc6,0x41},
    {0xca,0x70},
    {0xcb,0xc },
    {0xfd,0x00},


    {0xfd,0x00},
    {0x32,0x15},
    {0x34,0x66},
    {0x35,0x40},
    {0x36,0x80},
    {0xFF,0xFF},
};

//load GT2005 parameters
void SP0838_init_regs(struct sp0838_device *dev)
{
    int i=0;//,j;
    unsigned char buf[2];
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    if (!dev->vminfo.isused)
        return;

    while (1) {
        buf[0] = SP0838_script[i].addr;
        buf[1] = SP0838_script[i].val;
        if (SP0838_script[i].val == 0xff && SP0838_script[i].addr == 0xff) {
            printk("SP0838_write_regs success in initial SP0838.\n");
            break;
        }
        if ((i2c_put_byte_add8(client,buf, 2)) < 0) {
            printk("fail in initial SP0838. \n");
            return;
        }
        i++;
    }
    return;

}

#if 0
static struct aml_camera_i2c_fig1_s resolution_320x240_script[] = {
    {0xfd, 0x00},
    {0x47, 0x00},
    {0x48, 0x78},
    {0x49, 0x00},
    {0x4a, 0xf0},
    {0x4b, 0x00},
    {0x4c, 0xa0},
    {0x4d, 0x01},
    {0x4e, 0x40},
    {0xff, 0xff}

};
#endif
static struct aml_camera_i2c_fig1_s resolution_640x480_script[] = {
#if 1
    {0xfd, 0x00},
    {0x47, 0x00},
    {0x48, 0x00},
    {0x49, 0x01},
    {0x4a, 0xe0},
    {0x4b, 0x00},
    {0x4c, 0x00},
    {0x4d, 0x02},
    {0x4e, 0x80},
    {0xff, 0xff}
 #endif


};


static int set_flip(struct sp0838_device *dev)
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    unsigned char temp;
    unsigned char buf[2];

    if (!dev->vminfo.isused)
        return 0;

    temp = i2c_get_byte_add8(client, 0x31);
    temp &= 0x9f;
    temp |= dev->cam_info.m_flip << 5;
    temp |= dev->cam_info.v_flip << 6;
    buf[0] = 0x31;
    buf[1] = temp;
    if ((i2c_put_byte_add8(client,buf, 2)) < 0) {
        printk("fail in setting sensor orientation\n");
        return -1;
    }
    return 0;
}

static void sp0838_set_resolution(struct sp0838_device *dev,int height,int width)
{
    int i=0;
    unsigned char buf[2];
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    struct aml_camera_i2c_fig1_s* resolution_script;

    if (!dev->vminfo.isused)
        return;

    printk("set resolution 640X480\n");
    resolution_script = resolution_640x480_script;
    sp0838_h_active = 640;
    sp0838_v_active = 478; //480
    sp0838_frmintervals_active.denominator = 15;
    sp0838_frmintervals_active.numerator = 1;

    while (1) {
        buf[0] = resolution_script[i].addr;
        buf[1] = resolution_script[i].val;
        if (resolution_script[i].val == 0xff && resolution_script[i].addr == 0xff) {
            break;
        }
        if ((i2c_put_byte_add8(client,buf, 2)) < 0) {
            printk("fail in setting resolution \n");
            return;
        }
        i++;
    }
        set_flip(dev);
}
/*************************************************************************
* FUNCTION
*   set_SP0838_param_wb
*
* DESCRIPTION
*   SP0838 wb setting.
*
* PARAMETERS
*   none
*
* RETURNS
*   None
*
* GLOBALS AFFECTED  ��ƽ�����
*
*************************************************************************/
void set_SP0838_param_wb(struct sp0838_device *dev,enum  camera_wb_flip_e para)
{
//  kal_uint16 rgain=0x80, ggain=0x80, bgain=0x80;
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    unsigned char buf[4];

    unsigned char  temp_reg;
    //temp_reg=sp0a19_read_byte(0x22);
    //buf[0]=0x22; //SP0A19 enable auto wb
    buf[0]=0x32;

    if (!dev->vminfo.isused)
        return;

    temp_reg=i2c_get_byte_add8(client,buf[0]);

    printk(" camera set_SP0A19_param_wb=%d. \n ",para);
    switch (para) {
    case CAM_WB_AUTO:
        buf[0]=0xfd;
        buf[1]=0x01;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x28;
        buf[1]=0x5a;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x29;
        buf[1]=0x62;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x32;
        buf[1]=0x15;  //temp_reg|0x10;    // SP0A19 AWB enable bit[1]   ie. 0x02;
        i2c_put_byte_add8(client,buf,2);
        break;

    case CAM_WB_CLOUD:
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);

        buf[0]=0x32;
        buf[1]=0x05;//temp_reg&~0x10;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x01;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x28;
        buf[1]=0x88;//71
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x29;
        buf[1]=0x42;//41
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);
        break;

    case CAM_WB_DAYLIGHT:   // tai yang guang
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);

        buf[0]=0x32;
        buf[1]=0x05;//temp_reg&~0x10;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x01;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x28;
        buf[1]=0x6b;//b0
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x29;
        buf[1]=0x48;//70
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);
        break;

    case CAM_WB_INCANDESCENCE:   // bai re guang
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);

        buf[0]=0x32;
        buf[1]=0x05;//temp_reg&~0x10;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x01;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x28;
        buf[1]=0x41;//6b
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x29;
        buf[1]=0x71;//48
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);
        break;

    case CAM_WB_FLUORESCENT:   //ri guang deng
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);

        buf[0]=0x32;
        buf[1]=0x05;//temp_reg&~0x10;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x01;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x28;
        buf[1]=0x5a;//98
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x29;
        buf[1]=0x62;//c0
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);
        break;

    case CAM_WB_TUNGSTEN:   // wu si deng
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);

        buf[0]=0x32;
        buf[1]=0x05;//temp_reg&~0x10;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x01;
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x28;
        buf[1]=0x4e;//41
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0x29;
        buf[1]=0x68;//71
        i2c_put_byte_add8(client,buf,2);
        buf[0]=0xfd;
        buf[1]=0x00;
        i2c_put_byte_add8(client,buf,2);
        break;

    case CAM_WB_MANUAL:
        // TODO
        break;
    default:
        break;
    }
//  kal_sleep_task(20);
}


/*************************************************************************
* FUNCTION
*   SP0838_night_mode
*
* DESCRIPTION
*   This function night mode of SP0838.
*
* PARAMETERS
*   none
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
void SP0838_night_mode(struct sp0838_device *dev,enum  camera_night_mode_flip_e enable)
{

}
/*************************************************************************
* FUNCTION
*   SP0838_night_mode
*
* DESCRIPTION
*   This function night mode of SP0838.
*
* PARAMETERS
*   none
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/

void SP0838_set_param_banding(struct sp0838_device *dev,enum  camera_night_mode_flip_e banding)
{


}


/*************************************************************************
* FUNCTION
*   set_SP0838_param_exposure
*
* DESCRIPTION
*   SP0838 exposure setting.
*
* PARAMETERS
*   none
*
* RETURNS
*   None
*
* GLOBALS AFFECTED  ���ȵȼ� ���ڲ���
*
*************************************************************************/
void set_SP0838_param_exposure(struct sp0838_device *dev,enum camera_exposure_e para)//�ع����
{
    struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);

    unsigned char buf1[2];
    unsigned char buf2[2];

    if (!dev->vminfo.isused)
        return;

    switch (para) {
    case EXPOSURE_N4_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0xc0;
        break;
    case EXPOSURE_N3_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0xd0;
        break;
    case EXPOSURE_N2_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0xe0;
        break;
    case EXPOSURE_N1_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0xf0;
        break;
    case EXPOSURE_0_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0x00;//6a
        break;
    case EXPOSURE_P1_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0x10;
        break;
    case EXPOSURE_P2_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0x20;
        break;
    case EXPOSURE_P3_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0x30;
        break;
    case EXPOSURE_P4_STEP:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0x40;
        break;
    default:
        buf1[0]=0xfd;
        buf1[1]=0x00;
        buf2[0]=0xdc;
        buf2[1]=0x00;
        break;
    }
    //msleep(300);
    i2c_put_byte_add8(client,buf1,2);
    i2c_put_byte_add8(client,buf2,2);

}


/*************************************************************************
* FUNCTION
*   set_SP0838_param_effect
*
* DESCRIPTION
*   SP0838 effect setting.
*
* PARAMETERS
*   none
*
* RETURNS
*   None
*
* GLOBALS AFFECTED  ��Ч����
*
*************************************************************************/
void set_SP0838_param_effect(struct sp0838_device *dev,enum camera_effect_flip_e para)//��Ч����
{


}

unsigned char v4l_2_sp0838(int val)
{
    int ret=val/0x20;
    if (ret < 4) return ret * 0x20 + 0x80;
    else if (ret<8) return ret * 0x20 + 0x20;
    else return 0;
}

static int convert_canvas_index(unsigned int v4l2_format, unsigned int start_canvas)
{
    int canvas = start_canvas;

    switch (v4l2_format) {
    case V4L2_PIX_FMT_RGB565X:
    case V4L2_PIX_FMT_VYUY:
        canvas = start_canvas;
        break;
    case V4L2_PIX_FMT_YUV444:
    case V4L2_PIX_FMT_BGR24:
    case V4L2_PIX_FMT_RGB24:
        canvas = start_canvas;
        break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
        canvas = start_canvas | ((start_canvas + 1) << 8);
        break;
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YUV420:
        if (V4L2_PIX_FMT_YUV420 == v4l2_format) {
            canvas = start_canvas | ((start_canvas + 1) << 8) | ((start_canvas + 2) << 16);
        }else{
            canvas = start_canvas | ((start_canvas + 2) << 8) | ((start_canvas + 1) << 16);
        }
        break;
    default:
        break;
    }
    return canvas;
}

static int sp0838_setting(struct sp0838_device *dev,int PROP_ID,int value )
{
    int ret=0;
    //unsigned char cur_val;
    //struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
    switch (PROP_ID) {
    case V4L2_CID_DO_WHITE_BALANCE:
        if (sp0838_qctrl[0].default_value != value) {
            sp0838_qctrl[0].default_value = value;
            set_SP0838_param_wb(dev,value);
            printk(KERN_INFO " set camera  white_balance=%d. \n ",value);
        }
        break;
    case V4L2_CID_EXPOSURE:
        if (sp0838_qctrl[1].default_value != value) {
            sp0838_qctrl[1].default_value = value;
            set_SP0838_param_exposure(dev,value);
            printk(KERN_INFO " set camera  exposure=%d. \n ",value);
        }
        break;
    case V4L2_CID_COLORFX:
        if (sp0838_qctrl[2].default_value != value) {
            sp0838_qctrl[2].default_value = value;
            set_SP0838_param_effect(dev,value);
            printk(KERN_INFO " set camera  effect=%d. \n ",value);
        }
        break;
    case V4L2_CID_WHITENESS:
        if (sp0838_qctrl[3].default_value != value) {
            sp0838_qctrl[3].default_value = value;
            SP0838_set_param_banding(dev,value);
            printk(KERN_INFO " set camera  banding=%d. \n ",value);
        }
        break;
    case V4L2_CID_BLUE_BALANCE:
        if (sp0838_qctrl[4].default_value != value) {
            sp0838_qctrl[4].default_value = value;
            SP0838_night_mode(dev,value);
            printk(KERN_INFO " set camera  scene mode=%d. \n ",value);
        }
        break;
    case V4L2_CID_HFLIP:    /* set flip on H. */
        value = value & 0x3;
        if (sp0838_qctrl[5].default_value != value) {
            sp0838_qctrl[5].default_value = value;
            printk(" set camera  h filp =%d. \n ",value);
        }
        break;
    case V4L2_CID_VFLIP:    /* set flip on V. */
        break;
    case V4L2_CID_ZOOM_ABSOLUTE:
        if (sp0838_qctrl[7].default_value != value) {
            sp0838_qctrl[7].default_value = value;
            //printk(KERN_INFO " set camera  zoom mode=%d. \n ",value);
        }
        break;
    case V4L2_CID_ROTATE:
        if (sp0838_qctrl[8].default_value != value) {
            sp0838_qctrl[8].default_value = value;
            printk(" set camera  rotate =%d. \n ",value);
        }
        break;
    default:
        ret = -1;
        break;
    }
    return ret;

}

/*static void power_down_sp0838(struct sp0838_device *dev)
{


}*/

/* ------------------------------------------------------------------
    DMA and thread functions
   ------------------------------------------------------------------*/

#define TSTAMP_MIN_Y    24
#define TSTAMP_MAX_Y    (TSTAMP_MIN_Y + 15)
#define TSTAMP_INPUT_X  10
#define TSTAMP_MIN_X    (54 + TSTAMP_INPUT_X)

static void sp0838_fillbuff(struct sp0838_fh *fh, struct sp0838_buffer *buf)
{
    int ret;
    void *vbuf;
    vm_output_para_t para = {0};
    struct sp0838_device *dev = fh->dev;
    if (dev->vminfo.mem_alloc_succeed) {
        vbuf = (void *)videobuf_to_res(&buf->vb);
    } else {
        vbuf = videobuf_to_vmalloc(&buf->vb);
    }
    dprintk(dev,1,"%s\n", __func__);
    if (!vbuf)
        return;
 /*  0x18221223 indicate the memory type is MAGIC_VMAL_MEM*/
    if (dev->vminfo.mem_alloc_succeed) {
        if (buf->canvas_id == 0)
            buf->canvas_id = convert_canvas_index(fh->fmt->fourcc, CAMERA_USER_CANVAS_INDEX + buf->vb.i * 3);
        para.v4l2_memory = MAGIC_RE_MEM;
    } else {
        para.v4l2_memory = MAGIC_VMAL_MEM;
    }
    para.v4l2_format = fh->fmt->fourcc;
    para.zoom = sp0838_qctrl[7].default_value;
    para.vaddr = (unsigned)vbuf;
    para.angle =sp0838_qctrl[8].default_value;
    para.ext_canvas = buf->canvas_id;
    para.width = buf->vb.width;
    para.height = buf->vb.height;
    ret = vm_fill_this_buffer(&buf->vb,&para,&dev->vminfo);
/*if the vm is not used by sensor ,we let vm_fill_this_buffer() return -2*/
    if (ret == -2) {
        msleep(100);
    }
    buf->vb.state = VIDEOBUF_DONE;
}

static void sp0838_thread_tick(struct sp0838_fh *fh)
{
    struct sp0838_buffer *buf;
    struct sp0838_device *dev = fh->dev;
    struct sp0838_dmaqueue *dma_q = &dev->vidq;

    unsigned long flags = 0;

    dprintk(dev, 1, "Thread tick\n");
    if (!fh->stream_on) {
        dprintk(dev, 1, "sensor doesn't stream on\n");
        return ;
    }

    spin_lock_irqsave(&dev->slock, flags);
    if (list_empty(&dma_q->active)) {
        dprintk(dev, 1, "No active queue to serve\n");
        goto unlock;
    }

    buf = list_entry(dma_q->active.next,
             struct sp0838_buffer, vb.queue);
    dprintk(dev, 1, "%s\n", __func__);
    dprintk(dev, 1, "list entry get buf is %x\n",(unsigned)buf);

    if (!(fh->f_flags & O_NONBLOCK)) {
        /* Nobody is waiting on this buffer, return */
        if (!waitqueue_active(&buf->vb.done))
            goto unlock;
    }
    buf->vb.state = VIDEOBUF_ACTIVE;

    list_del(&buf->vb.queue);

    do_gettimeofday(&buf->vb.ts);

    /* Fill buffer */
    spin_unlock_irqrestore(&dev->slock, flags);
    sp0838_fillbuff(fh, buf);
    dprintk(dev, 1, "filled buffer %p\n", buf);

    wake_up(&buf->vb.done);
    dprintk(dev, 2, "[%p/%d] wakeup\n", buf, buf->vb. i);
    return;
unlock:
    spin_unlock_irqrestore(&dev->slock, flags);
    return;
}

#define frames_to_ms(frames)                    \
    ((frames * WAKE_NUMERATOR * 1000) / WAKE_DENOMINATOR)

static void sp0838_sleep(struct sp0838_fh *fh)
{
    struct sp0838_device *dev = fh->dev;
    struct sp0838_dmaqueue *dma_q = &dev->vidq;

    DECLARE_WAITQUEUE(wait, current);

    dprintk(dev, 1, "%s dma_q=0x%08lx\n", __func__,
        (unsigned long)dma_q);

    add_wait_queue(&dma_q->wq, &wait);
    if (kthread_should_stop())
        goto stop_task;

    /* Calculate time to wake up */
    //timeout = msecs_to_jiffies(frames_to_ms(1));

    sp0838_thread_tick(fh);

    schedule_timeout_interruptible(2);

stop_task:
    remove_wait_queue(&dma_q->wq, &wait);
    try_to_freeze();
}

static int sp0838_thread(void *data)
{
    struct sp0838_fh  *fh = data;
    struct sp0838_device *dev = fh->dev;

    dprintk(dev, 1, "thread started\n");

    set_freezable();

    for (; ;) {
        sp0838_sleep(fh);

        if (kthread_should_stop())
            break;
    }
    dprintk(dev, 1, "thread: exit\n");
    return 0;
}

static int sp0838_start_thread(struct sp0838_fh *fh)
{
    struct sp0838_device *dev = fh->dev;
    struct sp0838_dmaqueue *dma_q = &dev->vidq;

    dma_q->frame = 0;
    dma_q->ini_jiffies = jiffies;

    dprintk(dev, 1, "%s\n", __func__);

    dma_q->kthread = kthread_run(sp0838_thread, fh, "sp0838");

    if (IS_ERR(dma_q->kthread)) {
        v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
        return PTR_ERR(dma_q->kthread);
    }
    /* Wakes thread */
    wake_up_interruptible(&dma_q->wq);

    dprintk(dev, 1, "returning from %s\n", __func__);
    return 0;
}

static void sp0838_stop_thread(struct sp0838_dmaqueue  *dma_q)
{
    struct sp0838_device *dev = container_of(dma_q, struct sp0838_device, vidq);

    dprintk(dev, 1, "%s\n", __func__);
    /* shutdown control thread */
    if (dma_q->kthread) {
        kthread_stop(dma_q->kthread);
        dma_q->kthread = NULL;
    }
}

/* ------------------------------------------------------------------
    Videobuf operations
   ------------------------------------------------------------------*/
static int
res_buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
    struct videobuf_res_privdata *res = vq->priv_data;
    struct sp0838_fh *fh = container_of(res, struct sp0838_fh, res);
    struct sp0838_device *dev  = fh->dev;
    //int bytes = fh->fmt->depth >> 3 ;
    int height = fh->height;
    if (height == 1080)
       height = 1088;
    *size = (fh->width*height*fh->fmt->depth) >> 3;
    if (0 == *count)
        *count = 32;

    while (*size * *count > vid_limit * 1024 * 1024)
        (*count)--;

    dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
        *count, *size);
    return 0;
}

static int
vmall_buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
    struct sp0838_fh  *fh = vq->priv_data;
    struct sp0838_device *dev  = fh->dev;
    //int bytes = fh->fmt->depth >> 3 ;
    *size = (fh->width*fh->height*fh->fmt->depth)>>3;
    if (0 == *count)
    *count = 32;

    while (*size * *count > vid_limit * 1024 * 1024)
    (*count)--;

    dprintk(dev, 1, "%s, count=%d, size=%d\n", __func__,
    *count, *size);
    return 0;
}

static void res_free_buffer(struct videobuf_queue *vq, struct sp0838_buffer *buf)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct videobuf_res_privdata *res = vq->priv_data;
    fh = container_of(res, struct sp0838_fh, res);
    dev = fh->dev;

    dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

    videobuf_waiton(vq, &buf->vb, 0, 0);
    if (in_interrupt())
        BUG();
    videobuf_res_free(vq, &buf->vb);
    dprintk(dev, 1, "free_buffer: freed\n");
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static void vmall_free_buffer(struct videobuf_queue *vq, struct sp0838_buffer *buf)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    fh = vq->priv_data;
    dev = fh->dev;

    dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

    videobuf_waiton(vq, &buf->vb, 0, 0);
    if (in_interrupt())
        BUG();
    videobuf_vmalloc_free(&buf->vb);
    dprintk(dev, 1, "free_buffer: freed\n");
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1024
#define norm_maxh() 768
static int
res_buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
                        enum v4l2_field field)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct sp0838_buffer *buf;
    int rc;
    struct videobuf_res_privdata *res = vq->priv_data;
    fh = container_of(res, struct sp0838_fh, res);
    dev = fh->dev;
    buf = container_of(vb, struct sp0838_buffer, vb);
    //int bytes = fh->fmt->depth >> 3 ;
    dprintk(dev, 1, "%s, field=%d\n", __func__, field);

    BUG_ON(NULL == fh->fmt);

    if (fh->width  < 48 || fh->width  > norm_maxw() ||
        fh->height < 32 || fh->height > norm_maxh())
        return -EINVAL;

    buf->vb.size = fh->width * fh->height * fh->fmt->depth >> 3;
    if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
        return -EINVAL;

    /* These properties only change when queue is idle, see s_fmt */
    buf->fmt       = fh->fmt;
    buf->vb.width  = fh->width;
    buf->vb.height = fh->height;
    buf->vb.field  = field;

    //precalculate_bars(fh);

    if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
        rc = videobuf_iolock(vq, &buf->vb, NULL);
        if (rc < 0)
            goto fail;
    }

    buf->vb.state = VIDEOBUF_PREPARED;

    return 0;

fail:
    res_free_buffer(vq, buf);
    return rc;
}

static int
vmall_buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
                        enum v4l2_field field)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct sp0838_buffer *buf;
    int rc;
    fh = vq->priv_data;
    dev = fh->dev;
    buf = container_of(vb, struct sp0838_buffer, vb);
    //int bytes = fh->fmt->depth >> 3 ;
    dprintk(dev, 1, "%s, field=%d\n", __func__, field);

    BUG_ON(NULL == fh->fmt);

    if (fh->width  < 48 || fh->width  > norm_maxw() ||
        fh->height < 32 || fh->height > norm_maxh())
        return -EINVAL;

    buf->vb.size = fh->width*fh->height*fh->fmt->depth >> 3;
    if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
        return -EINVAL;

    /* These properties only change when queue is idle, see s_fmt */
    buf->fmt       = fh->fmt;
    buf->vb.width  = fh->width;
    buf->vb.height = fh->height;
    buf->vb.field  = field;

    //precalculate_bars(fh);

    if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
        rc = videobuf_iolock(vq, &buf->vb, NULL);
        if (rc < 0)
            goto fail;
    }

    buf->vb.state = VIDEOBUF_PREPARED;

    return 0;

fail:
    vmall_free_buffer(vq, buf);
    return rc;
}

static void
res_buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct sp0838_dmaqueue *vidq;
    struct sp0838_buffer *buf  = container_of(vb, struct sp0838_buffer, vb);
    struct videobuf_res_privdata *res = vq->priv_data;
    fh = container_of(res, struct sp0838_fh, res);
    dev = fh->dev;
    vidq = &dev->vidq;

    dprintk(dev, 1, "%s\n", __func__);
    buf->vb.state = VIDEOBUF_QUEUED;
    list_add_tail(&buf->vb.queue, &vidq->active);
}

static void
vmall_buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct sp0838_dmaqueue *vidq;
    struct sp0838_buffer *buf  = container_of(vb, struct sp0838_buffer, vb);
    fh = vq->priv_data;
    dev = fh->dev;
    vidq = &dev->vidq;

    dprintk(dev, 1, "%s\n", __func__);
    buf->vb.state = VIDEOBUF_QUEUED;
    list_add_tail(&buf->vb.queue, &vidq->active);
}

static void res_buffer_release(struct videobuf_queue *vq,
               struct videobuf_buffer *vb)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct sp0838_buffer   *buf  = container_of(vb, struct sp0838_buffer, vb);
    struct videobuf_res_privdata *res = vq->priv_data;
    fh = container_of(res, struct sp0838_fh, res);
    dev = (struct sp0838_device *)fh->dev;

    dprintk(dev, 1, "%s\n", __func__);

    res_free_buffer(vq, buf);
}

static void vmall_buffer_release(struct videobuf_queue *vq,
               struct videobuf_buffer *vb)
{
    struct sp0838_fh *fh;
    struct sp0838_device *dev;
    struct sp0838_buffer   *buf  = container_of(vb, struct sp0838_buffer, vb);
    fh = vq->priv_data;
    dev = (struct sp0838_device *)fh->dev;

    dprintk(dev, 1, "%s\n", __func__);

    vmall_free_buffer(vq, buf);
}

static struct videobuf_queue_ops sp0838_video_vmall_qops = {
    .buf_setup      = vmall_buffer_setup,
    .buf_prepare    = vmall_buffer_prepare,
    .buf_queue      = vmall_buffer_queue,
    .buf_release    = vmall_buffer_release,
};

static struct videobuf_queue_ops sp0838_video_res_qops = {
    .buf_setup      = res_buffer_setup,
    .buf_prepare    = res_buffer_prepare,
    .buf_queue      = res_buffer_queue,
    .buf_release    = res_buffer_release,
};

/* ------------------------------------------------------------------
    IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
                    struct v4l2_capability *cap)
{
    struct sp0838_fh  *fh  = priv;
    struct sp0838_device *dev = fh->dev;

    strcpy(cap->driver, "sp0838");
    strcpy(cap->card, "sp0838.canvas");
    strlcpy(cap->bus_info, dev->v4l2_dev.name, sizeof(cap->bus_info));
    cap->version = SP0838_CAMERA_VERSION;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
                V4L2_CAP_STREAMING     |
                V4L2_CAP_READWRITE;
    return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
                    struct v4l2_fmtdesc *f)
{
    struct sp0838_fmt *fmt;

    if (f->index >= ARRAY_SIZE(formats))
        return -EINVAL;

    fmt = &formats[f->index];

    strlcpy(f->description, fmt->name, sizeof(f->description));
    f->pixelformat = fmt->fourcc;
    return 0;
}
static int vidioc_enum_frameintervals(struct file *file, void *priv,
        struct v4l2_frmivalenum *fival)
{
        unsigned int k;

    if (fival->index > ARRAY_SIZE(sp0838_frmivalenum))
        return -EINVAL;

    for (k = 0; k < ARRAY_SIZE(sp0838_frmivalenum); k++) {
        if ((fival->index == sp0838_frmivalenum[k].index) &&
                (fival->pixel_format == sp0838_frmivalenum[k].pixel_format ) &&
                (fival->width=sp0838_frmivalenum[k].width) &&
                (fival->height==sp0838_frmivalenum[k].height)) {
            memcpy( fival, &sp0838_frmivalenum[k], sizeof(struct v4l2_frmivalenum));
            return 0;
        }
    }

    return -EINVAL;

}
static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
                    struct v4l2_format *f)
{
    struct sp0838_fh *fh = priv;

    f->fmt.pix.width        = fh->width;
    f->fmt.pix.height       = fh->height;
    f->fmt.pix.field        = fh->vb_vidq.field;
    f->fmt.pix.pixelformat  = fh->fmt->fourcc;
    f->fmt.pix.bytesperline =
        (f->fmt.pix.width * fh->fmt->depth) >> 3;
    f->fmt.pix.sizeimage =
        f->fmt.pix.height * f->fmt.pix.bytesperline;

    return (0);
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
            struct v4l2_format *f)
{
    struct sp0838_fh  *fh  = priv;
    struct sp0838_device *dev = fh->dev;
    struct sp0838_fmt *fmt;
    enum v4l2_field field;
    unsigned int maxw, maxh;

    fmt = get_format(f);
    if (!fmt) {
        dprintk(dev, 1, "Fourcc format (0x%08x) invalid.\n",
            f->fmt.pix.pixelformat);
        return -EINVAL;
    }

    field = f->fmt.pix.field;

    if (field == V4L2_FIELD_ANY) {
        field = V4L2_FIELD_INTERLACED;
    } else if (V4L2_FIELD_INTERLACED != field) {
        dprintk(dev, 1, "Field type invalid.\n");
        return -EINVAL;
    }

    maxw  = norm_maxw();
    maxh  = norm_maxh();

    f->fmt.pix.field = field;
    v4l_bound_align_image(&f->fmt.pix.width, 48, maxw, 2,
                  &f->fmt.pix.height, 32, maxh, 0, 0);
    f->fmt.pix.bytesperline =
        (f->fmt.pix.width * fmt->depth) >> 3;
    f->fmt.pix.sizeimage =
        f->fmt.pix.height * f->fmt.pix.bytesperline;

    return 0;
}

/*FIXME: This seems to be generic enough to be at videodev2 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
                    struct v4l2_format *f)
{
    struct sp0838_fh *fh = priv;
    struct videobuf_queue *q = &fh->vb_vidq;
    int ret;

    f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN-1) ) & (~(CANVAS_WIDTH_ALIGN-1));
    if ((f->fmt.pix.pixelformat == V4L2_PIX_FMT_YVU420) ||
            (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420)) {
        f->fmt.pix.width = (f->fmt.pix.width + (CANVAS_WIDTH_ALIGN*2-1) ) & (~(CANVAS_WIDTH_ALIGN*2-1));
    }

    ret = vidioc_try_fmt_vid_cap(file, fh, f);
    if (ret < 0)
        return ret;

    mutex_lock(&q->vb_lock);

    if (videobuf_queue_is_busy(&fh->vb_vidq)) {
        dprintk(fh->dev, 1, "%s queue busy\n", __func__);
        ret = -EBUSY;
        goto out;
    }

    fh->fmt           = get_format(f);
    fh->width         = f->fmt.pix.width;
    fh->height        = f->fmt.pix.height;
    fh->vb_vidq.field = f->fmt.pix.field;
    fh->type          = f->type;
#if 1
    if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24) {
        sp0838_set_resolution(fh->dev,fh->height,fh->width);
    } else {
        sp0838_set_resolution(fh->dev,fh->height,fh->width);
    }
#endif
    ret = 0;
out:
    mutex_unlock(&q->vb_lock);

    return ret;
}

static int vidioc_g_parm(struct file *file, void *priv,
        struct v4l2_streamparm *parms)
{
    struct sp0838_fh *fh = priv;
    struct sp0838_device *dev = fh->dev;
    struct v4l2_captureparm *cp = &parms->parm.capture;

    dprintk(dev,3,"vidioc_g_parm\n");
    if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    memset(cp, 0, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;

    cp->timeperframe = sp0838_frmintervals_active;
    printk("g_parm,deno=%d, numerator=%d\n", cp->timeperframe.denominator,
            cp->timeperframe.numerator );
    return 0;
}
static int vidioc_reqbufs(struct file *file, void *priv,
              struct v4l2_requestbuffers *p)
{
    struct sp0838_fh  *fh = priv;

    return (videobuf_reqbufs(&fh->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct sp0838_fh  *fh = priv;

        int ret = videobuf_querybuf(&fh->vb_vidq, p);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (ret == 0) {
        p->reserved  = convert_canvas_index(fh->fmt->fourcc, CAMERA_USER_CANVAS_INDEX + p->index * 3);
    } else {
        p->reserved = 0;
    }
#endif
    return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct sp0838_fh *fh = priv;

    return (videobuf_qbuf(&fh->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct sp0838_fh  *fh = priv;

    return (videobuf_dqbuf(&fh->vb_vidq, p,
                file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
    struct sp0838_fh  *fh = priv;

    return videobuf_cgmbuf(&fh->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
    struct sp0838_fh  *fh = priv;
    vdin_parm_t para;
    int ret = 0 ;
    if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;
    if (i != fh->type)
        return -EINVAL;
    memset( &para, 0, sizeof( para ));
    para.port  = TVIN_PORT_CAMERA;
    para.fmt = TVIN_SIG_FMT_MAX;
    para.frame_rate = sp0838_frmintervals_active.denominator;
    para.h_active = sp0838_h_active;
    para.v_active = sp0838_v_active;
    para.hsync_phase = 0;
    para.vsync_phase = 1;
    para.hs_bp = 0;
    para.vs_bp = 2;
    para.cfmt = TVIN_YVYU422;
        para.dfmt = TVIN_YVYU422;
    para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
    para.skip_count =  2; //skip_num
    printk("0a19,h=%d, v=%d, frame_rate=%d\n", sp0838_h_active, sp0838_v_active, sp0838_frmintervals_active.denominator);
    ret =  videobuf_streamon(&fh->vb_vidq);
    if (ret == 0) {
        if (fh->dev->vminfo.isused) {
            vops->start_tvin_service(0,&para);
        }
        fh->stream_on        = 1;
    }
    return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
    struct sp0838_fh  *fh = priv;

    int ret = 0 ;
    if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;
    if (i != fh->type)
        return -EINVAL;
    ret = videobuf_streamoff(&fh->vb_vidq);
    if (ret == 0 ) {
        if (fh->dev->vminfo.isused) {
            vops->stop_tvin_service(0);
        }
        fh->stream_on        = 0;
    }
    return ret;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,struct v4l2_frmsizeenum *fsize)
{
    int ret = 0,i=0;
    struct sp0838_fmt *fmt = NULL;
    struct v4l2_frmsize_discrete *frmsize = NULL;
    for (i = 0; i < ARRAY_SIZE(formats); i++) {
        if (formats[i].fourcc == fsize->pixel_format) {
            fmt = &formats[i];
            break;
        }
    }
    if (fmt == NULL)
        return -EINVAL;
    if ((fmt->fourcc == V4L2_PIX_FMT_NV21)
        ||(fmt->fourcc == V4L2_PIX_FMT_NV12)
        ||(fmt->fourcc == V4L2_PIX_FMT_YUV420)
        ||(fmt->fourcc == V4L2_PIX_FMT_YVU420)
        ) {
        if (fsize->index >= ARRAY_SIZE(sp0838_prev_resolution))
            return -EINVAL;
        frmsize = &sp0838_prev_resolution[fsize->index];
        fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        fsize->discrete.width = frmsize->width;
        fsize->discrete.height = frmsize->height;
    }
    else if (fmt->fourcc == V4L2_PIX_FMT_RGB24) {
        if (fsize->index >= ARRAY_SIZE(sp0838_pic_resolution))
            return -EINVAL;
        frmsize = &sp0838_pic_resolution[fsize->index];
        fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        fsize->discrete.width = frmsize->width;
        fsize->discrete.height = frmsize->height;
    }
    return ret;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id i)
{
    return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
                struct v4l2_input *inp)
{
    //if (inp->index >= NUM_INPUTS)
        //return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->std = V4L2_STD_525_60;
    sprintf(inp->name, "Camera %u", inp->index);

    return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
    struct sp0838_fh *fh = priv;
    struct sp0838_device *dev = fh->dev;

    *i = dev->input;

    return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
    struct sp0838_fh *fh = priv;
    struct sp0838_device *dev = fh->dev;

    //if (i >= NUM_INPUTS)
        //return -EINVAL;

    dev->input = i;
    //precalculate_bars(fh);

    return (0);
}

    /* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
                struct v4l2_queryctrl *qc)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(sp0838_qctrl); i++)
        if (qc->id && qc->id == sp0838_qctrl[i].id) {
            memcpy(qc, &(sp0838_qctrl[i]),
                sizeof(*qc));
            return (0);
        }

    return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
             struct v4l2_control *ctrl)
{
    struct sp0838_fh *fh = priv;
    struct sp0838_device *dev = fh->dev;
    int i;

    for (i = 0; i < ARRAY_SIZE(sp0838_qctrl); i++)
        if (ctrl->id == sp0838_qctrl[i].id) {
            ctrl->value = dev->qctl_regs[i];
            return 0;
        }

    return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
                struct v4l2_control *ctrl)
{
    struct sp0838_fh *fh = priv;
    struct sp0838_device *dev = fh->dev;
    int i;

    for (i = 0; i < ARRAY_SIZE(sp0838_qctrl); i++)
        if (ctrl->id == sp0838_qctrl[i].id) {
            if (ctrl->value < sp0838_qctrl[i].minimum ||
                ctrl->value > sp0838_qctrl[i].maximum ||
                sp0838_setting(dev,ctrl->id,ctrl->value)<0) {
                return -ERANGE;
            }
            dev->qctl_regs[i] = ctrl->value;
            return 0;
        }
    return -EINVAL;
}

/* ------------------------------------------------------------------
    File operations for the device
   ------------------------------------------------------------------*/

static int sp0838_open(struct file *file)
{
    struct sp0838_device *dev = video_drvdata(file);
    struct sp0838_fh *fh = NULL;
    int retval = 0;
#if CONFIG_CMA
    vm_init_resource(16*SZ_1M, &dev->vminfo);
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 1);
#endif
    if (dev->vminfo.isused) {
        aml_cam_init(&dev->cam_info);
    }

    SP0838_init_regs(dev);
    msleep(100);//40
    mutex_lock(&dev->mutex);
    dev->users++;
    if (dev->users > 1) {
        dev->users--;
        mutex_unlock(&dev->mutex);
        return -EBUSY;
    }

    dprintk(dev, 1, "open %s type=%s users=%d\n",
        video_device_node_name(dev->vdev),
        v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);

        /* init video dma queues */
    INIT_LIST_HEAD(&dev->vidq.active);
    init_waitqueue_head(&dev->vidq.wq);
    spin_lock_init(&dev->slock);
    /* allocate + initialize per filehandle data */
    fh = kzalloc(sizeof(*fh), GFP_KERNEL);
    if (NULL == fh) {
        dev->users--;
        retval = -ENOMEM;
    }
    mutex_unlock(&dev->mutex);

    if (retval)
        return retval;

    wake_lock(&(dev->wake_lock));
    file->private_data = fh;
    fh->dev      = dev;

    fh->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fh->fmt      = &formats[6];
    fh->width    = 640;
    fh->height   = 480;
    fh->stream_on = 0 ;
    fh->f_flags  = file->f_flags;
    /* Resets frame counters */
    dev->jiffies = jiffies;

//  TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
//  TVIN_SIG_FMT_CAMERA_800X600P_30Hz,
//  TVIN_SIG_FMT_CAMERA_1024X768P_30Hz, // 190
//  TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz,
//  TVIN_SIG_FMT_CAMERA_1280X720P_30Hz,

    if (dev->vminfo.mem_alloc_succeed) {
        fh->res.start = dev->vminfo.buffer_start;
        fh->res.end = dev->vminfo.buffer_start + dev->vminfo.vm_buf_size - 1;
        fh->res.magic = MAGIC_RE_MEM;
        fh->res.priv = NULL;
        videobuf_queue_res_init(&fh->vb_vidq, &sp0838_video_res_qops,
        NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
        sizeof(struct sp0838_buffer), (void*)&fh->res, NULL);
    } else {
        videobuf_queue_vmalloc_init(&fh->vb_vidq, &sp0838_video_vmall_qops,
        NULL, &dev->slock, fh->type, V4L2_FIELD_INTERLACED,
        sizeof(struct sp0838_buffer), fh, NULL);
    }

    sp0838_start_thread(fh);

    return 0;
}

static ssize_t
sp0838_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
    struct sp0838_fh *fh = file->private_data;

    if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
                    file->f_flags & O_NONBLOCK);
    }
    return 0;
}

static unsigned int
sp0838_poll(struct file *file, struct poll_table_struct *wait)
{
    struct sp0838_fh        *fh = file->private_data;
    struct sp0838_device       *dev = fh->dev;
    struct videobuf_queue *q = &fh->vb_vidq;

    dprintk(dev, 1, "%s\n", __func__);

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE != fh->type)
        return POLLERR;

    return videobuf_poll_stream(file, q, wait);
}

static int sp0838_close(struct file *file)
{
    struct sp0838_fh         *fh = file->private_data;
    struct sp0838_device *dev       = fh->dev;
    struct sp0838_dmaqueue *vidq = &dev->vidq;
    struct video_device  *vdev = video_devdata(file);
    sp0838_have_open=0;
    sp0838_stop_thread(vidq);
    videobuf_stop(&fh->vb_vidq);
    if (fh->stream_on) {
        if (dev->vminfo.isused) {
            vops->stop_tvin_service(0);
        }
    }
    videobuf_mmap_free(&fh->vb_vidq);

    kfree(fh);

    mutex_lock(&dev->mutex);
    dev->users--;
    mutex_unlock(&dev->mutex);

    dprintk(dev, 1, "close called (dev=%s, users=%d)\n",
        video_device_node_name(vdev), dev->users);
#if 1
    sp0838_qctrl[0].default_value=0;
    sp0838_qctrl[1].default_value=4;
    sp0838_qctrl[2].default_value=0;
    sp0838_qctrl[3].default_value=0;
    sp0838_qctrl[4].default_value=0;

    sp0838_frmintervals_active.numerator = 1;
    sp0838_frmintervals_active.denominator = 15;
    //power_down_sp0838(dev);
#endif
    aml_cam_uninit(&dev->cam_info);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 0);
#endif
    wake_unlock(&(dev->wake_lock));
#ifdef CONFIG_CMA
    vm_deinit_resource(&dev->vminfo);
#endif

    return 0;
}

static int sp0838_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct sp0838_fh  *fh = file->private_data;
    struct sp0838_device *dev = fh->dev;
    int ret;

    dprintk(dev, 1, "mmap called, vma=0x%08lx\n", (unsigned long)vma);

    ret = videobuf_mmap_mapper(&fh->vb_vidq, vma);

    dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
        (unsigned long)vma->vm_start,
        (unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
        ret);

    return ret;
}

static const struct v4l2_file_operations sp0838_fops = {
    .owner      = THIS_MODULE,
    .open           = sp0838_open,
    .release        = sp0838_close,
    .read           = sp0838_read,
    .poll       = sp0838_poll,
    .ioctl          = video_ioctl2, /* V4L2 ioctl handler */
    .mmap           = sp0838_mmap,
};

static const struct v4l2_ioctl_ops sp0838_ioctl_ops = {
    .vidioc_querycap      = vidioc_querycap,
    .vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
    .vidioc_reqbufs       = vidioc_reqbufs,
    .vidioc_querybuf      = vidioc_querybuf,
    .vidioc_qbuf          = vidioc_qbuf,
    .vidioc_dqbuf         = vidioc_dqbuf,
    .vidioc_s_std         = vidioc_s_std,
    .vidioc_enum_input    = vidioc_enum_input,
    .vidioc_g_input       = vidioc_g_input,
    .vidioc_s_input       = vidioc_s_input,
    .vidioc_queryctrl     = vidioc_queryctrl,
    .vidioc_querymenu     = vidioc_querymenu,
    .vidioc_g_ctrl        = vidioc_g_ctrl,
    .vidioc_s_ctrl        = vidioc_s_ctrl,
    .vidioc_streamon      = vidioc_streamon,
    .vidioc_streamoff     = vidioc_streamoff,
    .vidioc_enum_framesizes = vidioc_enum_framesizes,
    .vidioc_g_parm = vidioc_g_parm,
    .vidioc_enum_frameintervals = vidioc_enum_frameintervals,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
    .vidiocgmbuf          = vidiocgmbuf,
#endif
};

static struct video_device sp0838_template = {
    .name       = "sp0838_v4l",
    .fops           = &sp0838_fops,
    .ioctl_ops  = &sp0838_ioctl_ops,
    .release    = video_device_release,

    .tvnorms              = V4L2_STD_525_60,
    .current_norm         = V4L2_STD_NTSC_M,
};

static int sp0838_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);

    return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SP0838, 0);
    //return v4l2_chip_ident_i2c_client(client, chip,  V4L2_IDENT_GT2005, 0);
}

static const struct v4l2_subdev_core_ops sp0838_core_ops = {
    .g_chip_ident = sp0838_g_chip_ident,
};

static const struct v4l2_subdev_ops sp0838_ops = {
    .core = &sp0838_core_ops,
};

static int sp0838_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    aml_cam_info_t* plat_dat;
    int err;
    struct sp0838_device *t;
    struct v4l2_subdev *sd;

    vops = get_vdin_v4l2_ops();
    v4l_info(client, "chip found @ 0x%x (%s)\n",
            client->addr << 1, client->adapter->name);
    t = kzalloc(sizeof(*t), GFP_KERNEL);
    if (t == NULL)
        return -ENOMEM;
    sd = &t->sd;
    v4l2_i2c_subdev_init(sd, client, &sp0838_ops);

    plat_dat = (aml_cam_info_t*)client->dev.platform_data;

    memset(&t->vminfo,0,sizeof(vm_init_t));

    /* Now create a video4linux device */
    mutex_init(&t->mutex);

    /* Now create a video4linux device */
    t->vdev = video_device_alloc();
    if (t->vdev == NULL) {
        kfree(t);
        kfree(client);
        return -ENOMEM;
    }
    memcpy(t->vdev, &sp0838_template, sizeof(*t->vdev));

    video_set_drvdata(t->vdev, t);

    wake_lock_init(&(t->wake_lock),WAKE_LOCK_SUSPEND, "sp0838");

    /* Register it */
    if (plat_dat) {
        memcpy(&t->cam_info, plat_dat, sizeof(aml_cam_info_t));
        if (plat_dat->front_back >=0)
            video_nr = plat_dat->front_back;
    } else {
        printk("camera sp0838: have no platform data\n");
        kfree(t);
        kfree(client);
        return -1;
    }

    t->cam_info.version = SP0838_DRIVER_VERSION;
    if (aml_cam_info_reg(&t->cam_info) < 0)
        printk("reg caminfo error\n");

    err = video_register_device(t->vdev, VFL_TYPE_GRABBER, video_nr);
    if (err < 0) {
        video_device_release(t->vdev);
        kfree(t);
        return err;
    }



    return 0;
}

static int sp0838_remove(struct i2c_client *client)
{
    struct v4l2_subdev *sd = i2c_get_clientdata(client);
    struct sp0838_device *t = to_dev(sd);

    video_unregister_device(t->vdev);
    v4l2_device_unregister_subdev(sd);
    wake_lock_destroy(&(t->wake_lock));
    aml_cam_info_unreg(&t->cam_info);
    kfree(t);
    return 0;
}

static const struct i2c_device_id sp0838_id[] = {
    { "sp0838", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, sp0838_id);

static struct i2c_driver sp0838_i2c_driver = {
    .driver = {
        .name = "sp0838",
    },
    .probe = sp0838_probe,
    .remove = sp0838_remove,
    .id_table = sp0838_id,
};

module_i2c_driver(sp0838_i2c_driver);

