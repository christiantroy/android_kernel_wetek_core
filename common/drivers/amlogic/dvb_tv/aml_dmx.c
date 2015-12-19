/*
 * AMLOGIC Smart card driver.
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
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/fcntl.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#ifdef ARC_700
#include <asm/arch/am_regs.h>
#else
#include <mach/am_regs.h>
#endif

#include <mach/pinmux.h>

#include "aml_dvb.h"

#include "../amports/streambuf.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#include <mach/power_gate.h>
#endif

#include <linux/vmalloc.h>


#define ENABLE_SEC_BUFF_WATCHDOG
#define USE_AHB_MODE

#define pr_dbg(fmt, args...)\
	do {\
		if (debug_dmx&0x1)\
			printk("DVB: " fmt, ## args);\
	}while(0)

#define pr_dbg_sf(fmt, args...)\
	do {\
		if (debug_dmx&0x2)\
			printk("DVB:SF " fmt, ## args);\
	}while(0)

#define pr_dbg_irq(fmt, args...)\
	printk(fmt, ## args)

#define pr_dbg_irq_sf(fmt, args...)\
	do { \
		if (debug_irq&2) \
			pr_dbg_sf(fmt, ## args); \
	}while(0)

#define pr_error(fmt, args...) printk("DVB: " fmt, ## args)

MODULE_PARM_DESC(debug_dmx, "\n\t\t Enable demux debug information");
static int debug_dmx = 0;
module_param(debug_dmx, int, S_IRUGO);

MODULE_PARM_DESC(debug_irq, "\n\t\t Enable demux IRQ debug information");
static int debug_irq = 0;
module_param(debug_irq, int, S_IRUGO);

static int npids=CHANNEL_COUNT;
#define MOD_PARAM_DECLARE_CHANPIDS(_dmx) \
MODULE_PARM_DESC(debug_dmx##_dmx##_chanpids, "\n\t\t pids of dmx channels"); \
static short debug_dmx##_dmx##_chanpids[CHANNEL_COUNT] = {[0 ... (CHANNEL_COUNT - 1)] = -1}; \
module_param_array(debug_dmx##_dmx##_chanpids, short, &npids, 0444)

MOD_PARAM_DECLARE_CHANPIDS(0);
MOD_PARAM_DECLARE_CHANPIDS(1);
MOD_PARAM_DECLARE_CHANPIDS(2);

#define set_debug_dmx_chanpids(_dmx, _idx, _pid)\
	do { \
		if     ((_dmx) == 0) {debug_dmx0_chanpids[(_idx)] = (_pid);} \
		else if((_dmx) == 1) {debug_dmx1_chanpids[(_idx)] = (_pid);} \
		else if((_dmx) == 2) {debug_dmx2_chanpids[(_idx)] = (_pid);} \
	}while(0)

MODULE_PARM_DESC(debug_sf_user, "\n\t\t only for sf mode check");
static int debug_sf_user = 0;
module_param(debug_sf_user, int, 0444);


MODULE_PARM_DESC(force_sec_sf, "\n\t\t force sf mode for sec filter");
static int force_sec_sf = 0;
module_param(force_sec_sf, int, 0644);

MODULE_PARM_DESC(force_pes_sf, "\n\t\t force sf mode for pes filter");
static int force_pes_sf = 0;
module_param(force_pes_sf, int, 0644);


#define DMX_READ_REG(i,r)\
	((i)?((i==1)?READ_MPEG_REG(r##_2):READ_MPEG_REG(r##_3)):READ_MPEG_REG(r))

#define DMX_WRITE_REG(i,r,d)\
	do{\
	if(i==1) {\
		WRITE_MPEG_REG(r##_2,d);\
	} else if(i==2) {\
		WRITE_MPEG_REG(r##_3,d);\
	}\
	else {\
		WRITE_MPEG_REG(r,d);\
	}\
	}while(0)

#define READ_PERI_REG				READ_CBUS_REG
#define WRITE_PERI_REG			WRITE_CBUS_REG

#define READ_ASYNC_FIFO_REG(i,r)\
	((i) ? READ_PERI_REG(ASYNC_FIFO2_##r) : READ_PERI_REG(ASYNC_FIFO_##r))

#define WRITE_ASYNC_FIFO_REG(i,r,d)\
	do {\
		if (i==1) {\
			WRITE_PERI_REG(ASYNC_FIFO2_##r, d);\
		} else {\
			WRITE_PERI_REG(ASYNC_FIFO_##r, d);\
		}\
	}while(0)

#define CLEAR_ASYNC_FIFO_REG_MASK(i,reg,mask) WRITE_ASYNC_FIFO_REG(i, reg, (READ_ASYNC_FIFO_REG(i, reg)&(~(mask))))

#define DVR_FEED(f)							\
	((f) && ((f)->type == DMX_TYPE_TS) &&					\
	(((f)->ts_type & (TS_PACKET | TS_DEMUX)) == TS_PACKET))

#define NO_SUB

#define SYS_CHAN_COUNT    (4)
#define SEC_GRP_LEN_0     (0xc)
#define SEC_GRP_LEN_1     (0xc)
#define SEC_GRP_LEN_2     (0xc)
#define SEC_GRP_LEN_3     (0xc)
#define LARGE_SEC_BUFF_MASK  0xFFFFFFFF
#define LARGE_SEC_BUFF_COUNT 32
#define WATCHDOG_TIMER    250
#define ASYNCFIFO_BUFFER_SIZE_DEFAULT (512*1024)

#define DEMUX_INT_MASK\
			((0<<(AUDIO_SPLICING_POINT))    |\
			(0<<(VIDEO_SPLICING_POINT))     |\
			(0<<(OTHER_PES_READY))          |\
			(1<<(SUB_PES_READY))            |\
			(1<<(SECTION_BUFFER_READY))     |\
			(0<<(OM_CMD_READ_PENDING))      |\
			(1<<(TS_ERROR_PIN))             |\
			(1<<(NEW_PDTS_READY))           |\
			(0<<(DUPLICATED_PACKET))        |\
			(0<<(DIS_CONTINUITY_PACKET)))

#define TS_SRC_MAX 3


/*Reset the demux device*/
#define RESET_DEMUX2      (1<<15)
#define RESET_DEMUX1      (1<<14)
#define RESET_DEMUX0      (1<<13)
#define RESET_S2P1        (1<<12)
#define RESET_S2P0        (1<<11)
#define RESET_DES         (1<<10)
#define RESET_TOP         (1<<9)
static int dmx_remove_feed(struct aml_dmx *dmx, struct dvb_demux_feed *feed);
static void reset_async_fifos(struct aml_dvb *dvb);
static int dmx_add_feed(struct aml_dmx *dmx, struct dvb_demux_feed *feed);

/*Audio & Video PTS value*/
static u32 video_pts = 0;
static u32 audio_pts = 0;
static u32 first_video_pts = 0;
static u32 first_audio_pts = 0;
static int demux_skipbyte = 0;
static int tsfile_clkdiv = 4;


#define SF_DMX_ID 2
#define SF_AFIFO_ID 1
#define sf_dmx_sf(_dmx) (((_dmx)->id==SF_DMX_ID) && ((struct aml_dvb*)(_dmx)->demux.priv)->swfilter.user)
#define sf_afifo_sf(_afifo) (((_afifo)->id==SF_AFIFO_ID) && (_afifo)->dvb->swfilter.user)
#define dump(b, l) \
	do { \
		int i; \
		printk("dump: "); \
		for (i=0; i<(l); i++) \
			printk("%02x ", *(((unsigned char*)(b))+i)); \
		printk("\n"); \
	}while(0)


/*Section buffer watchdog*/
static void section_buffer_watchdog_func(unsigned long arg)
{
	struct aml_dvb *dvb = (struct aml_dvb*)arg;
	struct aml_dmx *dmx;
	u32 section_busy32 = 0, om_cmd_status32 = 0, demux_channel_activity32 = 0;
	u16 demux_int_status1 = 0;
	u32 device_no = 0;
	u32 filter_number = 0;
	u32 i = 0;
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);

	for(device_no=0; device_no<DMX_DEV_COUNT; device_no++) {

		dmx = &dvb->dmx[device_no];

		if(dvb->dmx_watchdog_disable[device_no])
			continue;

		if(dmx->init) {
			om_cmd_status32 = DMX_READ_REG(device_no, OM_CMD_STATUS);
			demux_channel_activity32 = DMX_READ_REG(device_no, DEMUX_CHANNEL_ACTIVITY);
			section_busy32 = DMX_READ_REG(device_no, SEC_BUFF_BUSY);
#if 1
			if(om_cmd_status32 & 0x8fc2) { // bit 15:12 -- om_cmd_count
						       // bit  11:9 -- overflow_count
						       // bit   8:6 -- om_overwrite_count
						       // bit     1 -- om_cmd_overflow
				/*BUG: If the recoder is running, return*/
				if (dmx->record)
					goto end;
				/*Reset the demux*/
				pr_error("reset the demux %04x\t%03x\t%03x\t%03x\t%01x\t%01x\t%x\t%x\tdmx%d:status:0x%x\n",
							(om_cmd_status32 >> 12) & 0xf,
							(om_cmd_status32 >> 9) & 0x7,
							(om_cmd_status32 >> 6) & 0x7,
							(om_cmd_status32 >> 3) & 0x7,
							(om_cmd_status32 >> 2) & 0x1,
							(om_cmd_status32 >> 1) & 0x1,
							demux_channel_activity32,
							section_busy32,
							dmx->id,
							om_cmd_status32);

				dmx_reset_dmx_hw_ex_unlock(dvb, dmx, 0);
				goto end;
			}
#else
			/*
			// bit 15:12 -- om_cmd_count (read only)
			// bit  11:9 -- overflow_count // bit  11:9 -- om_cmd_wr_ptr (read only)
			// bit   8:6 -- om_overwrite_count // bit   8:6 -- om_cmd_rd_ptr (read only)
			// bit   5:3 -- type_stb_om_w_rd (read only)
			// bit     2 -- unit_start_stb_om_w_rd (read only)
			// bit     1 -- om_cmd_overflow (read only)
			// bit     0 -- om_cmd_pending (read)
			// bit     0 -- om_cmd_read_finished (write)
			*/
			if(om_cmd_status32 & 0x0002) {
				pr_error("reset the demux\n");
				dmx_reset_hw_ex(dvb, 0);
				goto end;
			}
#endif
			section_busy32 = DMX_READ_REG(device_no, SEC_BUFF_BUSY);
			if((section_busy32 & LARGE_SEC_BUFF_MASK) == LARGE_SEC_BUFF_MASK)  {
				/*All the largest section buffers occupied, clear buffers*/
				DMX_WRITE_REG(device_no, SEC_BUFF_READY, section_busy32);
			} else {
				for(i = 0; i < SEC_BUF_COUNT; i++) {
					if(section_busy32 & (1 << i)) {
						DMX_WRITE_REG(device_no, SEC_BUFF_NUMBER, i);
						filter_number = (DMX_READ_REG(device_no, SEC_BUFF_NUMBER) >> 8);
						if((filter_number >= 31) /* >=31, do not handle this case*/  ||
						   ((filter_number<FILTER_COUNT) && dmx->filter[filter_number].used)) {
							section_busy32 &= ~(1 << i);
						}
					}
				}
				if(section_busy32) {
					/*Clear invalid buffers*/
					DMX_WRITE_REG(device_no, SEC_BUFF_READY, section_busy32);
					pr_error("clear invalid buffers 0x%x\n", section_busy32);
				}
#if 0
				section_busy32 = 0x7fffffff;
				for(i = 0; i < SEC_BUF_BUSY_SIZE; i++) {
					dmx->section_busy[i] = ((i == SEC_BUF_BUSY_SIZE-1) ?
							DMX_READ_REG(device_no, SEC_BUFF_BUSY) : dmx->section_busy[i+1]);
					section_busy32 &= dmx->section_busy[i];
				}

				/*count the number of '1' bits*/
				i = section_busy32;
				i = (i & 0x55555555) + ((i & 0xaaaaaaaa) >> 1);
				i = (i & 0x33333333) + ((i & 0xcccccccc) >> 2);
				i = (i & 0x0f0f0f0f) + ((i & 0xf0f0f0f0) >> 4);
				i = (i & 0x00ff00ff) + ((i & 0xff00ff00) >> 8);
				i = (i & 0x0000ffff) + ((i & 0xffff0000) >> 16);
				if(i > LARGE_SEC_BUFF_COUNT) {
					/*too long some of the section buffers are being processed*/
					DMX_WRITE_REG(device_no, SEC_BUFF_READY, section_busy32);
				}
#endif
			}
			demux_int_status1 = DMX_READ_REG(device_no, STB_INT_STATUS) & 0xfff7;
			if(demux_int_status1 & (1 << TS_ERROR_PIN)) {
				DMX_WRITE_REG(device_no, STB_INT_STATUS, (1 << TS_ERROR_PIN));
			}
		}
	}

end:
	spin_unlock_irqrestore(&dvb->slock, flags);
#ifdef ENABLE_SEC_BUFF_WATCHDOG
	mod_timer(&dvb->watchdog_timer, jiffies+msecs_to_jiffies(WATCHDOG_TIMER));
#endif
	return;
}

static inline int sec_filter_match(struct aml_dmx *dmx, struct aml_filter *f, u8 *p)
{
	int b;
	u8 neq = 0;

	if(!f->used || !dmx->channel[f->chan_id].used)
		return 0;

	for(b=0; b<FILTER_LEN; b++) {
		u8 xor = p[b]^f->value[b];

		if(xor&f->maskandmode[b])
			return 0;

		if(xor&f->maskandnotmode[b])
			neq = 1;
	}

	if(f->neq && !neq)
		return 0;

	return 1;
}

static int section_crc(struct aml_dmx *dmx, struct aml_filter *f, u8 *p)
{
	int sec_len = (((p[1]&0xF)<<8)|p[2])+3;
	struct dvb_demux_feed *feed = dmx->channel[f->chan_id].feed;

	if(feed->feed.sec.check_crc) {
		struct dvb_demux *demux = feed->demux;
		struct dmx_section_feed *sec = &feed->feed.sec;
		int section_syntax_indicator;

		section_syntax_indicator = ((p[1] & 0x80) != 0);
		sec->seclen  = sec_len;
		sec->crc_val = ~0;
		if (demux->check_crc32(feed, p, sec_len)) {
			pr_error("section CRC check failed!\n");

#if 0
			int i;

			for(i=0; i<sec_len; i++)
			{
				printk("%02x ", p[i]);
				if(!((i+1)%16))
					printk("\n");
			}
			printk("\nerror section data\n");
#endif
			return 0;
		}

#if 0
			int i;

			for(i=0; i<sec_len; i++)
			{
				printk("%02x ", p[i]);
				if(!((i+1)%16))
					printk("\n");
			}
			printk("\nsection data\n");
#endif
	}

	return 1;
}

static void section_notify(struct aml_dmx *dmx, struct aml_filter *f, u8 *p)
{
	int sec_len = (((p[1]&0xF)<<8)|p[2])+3;
	struct dvb_demux_feed *feed = dmx->channel[f->chan_id].feed;

	if(feed && feed->cb.sec) {
		feed->cb.sec(p, sec_len, NULL, 0, f->filter, DMX_OK);
	}
}

static void hardware_match_section(struct aml_dmx *dmx, u16 sec_num, u16 buf_num)
{
	u8 *p = (u8*)dmx->sec_buf[buf_num].addr;
	struct aml_filter *f;
	int chid, i;
	int need_crc = 1;

	if(sec_num>=FILTER_COUNT) {
		pr_dbg("sec_num invalid: %d\n", sec_num);
		return;
	}

	dma_sync_single_for_cpu(NULL, dmx->sec_pages_map+(buf_num<<0x0c), (1<<0x0c), DMA_FROM_DEVICE);

	f = &dmx->filter[sec_num];
	chid = f->chan_id;

	for(i=0; i<FILTER_COUNT; i++) {
		f = &dmx->filter[i];
		if(f->chan_id!=chid)
			continue;
		if(sec_filter_match(dmx, f, p)) {
			if(need_crc){
				if(!section_crc(dmx, f, p))
					return;
				need_crc = 0;
			}

			section_notify(dmx, f, p);
		}
	}
}

static void software_match_section(struct aml_dmx *dmx, u16 buf_num)
{
	u8 *p = (u8*)dmx->sec_buf[buf_num].addr;
	struct aml_filter *f, *fmatch = NULL;
	int i, fid = -1;

	dma_sync_single_for_cpu(NULL, dmx->sec_pages_map+(buf_num<<0x0c), (1<<0x0c), DMA_FROM_DEVICE);

	for(i=0; i<FILTER_COUNT; i++) {
		f = &dmx->filter[i];

		if(sec_filter_match(dmx, f, p)) {
			pr_dbg("[software match]filter %d match, pid %d\n",
				i, dmx->channel[f->chan_id].pid);
			if (!fmatch){
				fmatch = f;
				fid = i;
			} else {
				pr_dbg("[software match]Muli-filter match this section, will skip this section\n");
				return;
			}
		}
	}

	if (fmatch) {
		pr_dbg("[software match]dispatch section to filter %d pid %d\n", fid, dmx->channel[fmatch->chan_id].pid);
		if(section_crc(dmx, fmatch, p)){
			section_notify(dmx, fmatch, p);
		}
	} else {
		pr_dbg("[software match]this section do not match any filter!!!\n");
	}
}

static void process_section(struct aml_dmx *dmx)
{
	u32 ready, i,sec_busy;
	u16 sec_num;

	//pr_dbg("section\n");
	ready = DMX_READ_REG(dmx->id, SEC_BUFF_READY);
	if(ready) {
#ifdef USE_AHB_MODE
	//	WRITE_ISA_REG(AHB_BRIDGE_CTRL1, READ_ISA_REG (AHB_BRIDGE_CTRL1) | (1 << 31));
	//	WRITE_ISA_REG(AHB_BRIDGE_CTRL1, READ_ISA_REG (AHB_BRIDGE_CTRL1) & (~ (1 << 31)));
#endif

		for(i=0; i<SEC_BUF_COUNT; i++) {
			if (ready & (1 << i)) {
				/* get section busy */
				sec_busy = DMX_READ_REG(dmx->id, SEC_BUFF_BUSY);
				/* get filter number */
				DMX_WRITE_REG(dmx->id, SEC_BUFF_NUMBER, i);
				sec_num = (DMX_READ_REG(dmx->id, SEC_BUFF_NUMBER) >> 8);

				/*
				 * sec_buf_watchdog_count dispatch:
				 * byte0 -- always busy=0 's watchdog count
				 * byte1 -- always busy=1 & filter_num=31 's watchdog count
				*/

				/* sec_busy is not set, check busy=0 watchdog count */
				if (! (sec_busy & (1 << i))) {
					/* clear other wd count of this buffer */
					dmx->sec_buf_watchdog_count[i] &= 0x000000ff;
					dmx->sec_buf_watchdog_count[i] += 0x1;
					pr_dbg("bit%d ready=1, busy=0, sec_num=%d for %d times\n",i,sec_num,dmx->sec_buf_watchdog_count[i]);
					if (dmx->sec_buf_watchdog_count[i] >= 5) {
						pr_dbg("busy=0 reach the max count, try software match.\n");
						software_match_section(dmx, i);
						dmx->sec_buf_watchdog_count[i] = 0;
						DMX_WRITE_REG(dmx->id, SEC_BUFF_READY, (1 << i));
					}
					continue;
				}

				/* filter_num == 31 && busy == 1, check watchdog count */
				if (sec_num >= 31) {
					/* clear other wd count of this buffer */
					dmx->sec_buf_watchdog_count[i] &= 0x0000ff00;
					dmx->sec_buf_watchdog_count[i] += 0x100;
					pr_dbg("bit%d ready=1,busy=1,sec_num=%d for %d times\n",i,sec_num,dmx->sec_buf_watchdog_count[i]>>8);
					if (dmx->sec_buf_watchdog_count[i] >= 0x500) {
						pr_dbg("busy=1&filter_num=31 reach the max count, clear the buf ready & busy!\n");
						software_match_section(dmx, i);
						dmx->sec_buf_watchdog_count[i] = 0;
						DMX_WRITE_REG(dmx->id, SEC_BUFF_READY, (1 << i));
						DMX_WRITE_REG(dmx->id, SEC_BUFF_BUSY, (1 << i));
					}
					continue;
				}

				/* now, ready & busy are both set and filter number is valid */
				if (dmx->sec_buf_watchdog_count[i] != 0)
					dmx->sec_buf_watchdog_count[i] = 0;

				/* process this section */
				hardware_match_section(dmx, sec_num, i);

				/* clear the ready & busy bit */
				DMX_WRITE_REG(dmx->id, SEC_BUFF_READY, (1 << i));
				DMX_WRITE_REG(dmx->id, SEC_BUFF_BUSY, (1 << i));
			}
		}
	}
}

#ifdef NO_SUB
static void process_sub(struct aml_dmx *dmx)
{

	int rd_ptr=0;

	int wr_ptr=READ_MPEG_REG(PARSER_SUB_WP);
	int start_ptr=READ_MPEG_REG(PARSER_SUB_START_PTR);
	int end_ptr=READ_MPEG_REG(PARSER_SUB_END_PTR);

	unsigned char *buffer1=0,*buffer2=0;
	unsigned char *buffer1_virt=0, *buffer2_virt=0;
	int len1=0,len2=0;


	rd_ptr=READ_MPEG_REG(PARSER_SUB_RP);
	if(!rd_ptr)
		return;
	if(rd_ptr>wr_ptr)
	{
		len1=end_ptr-rd_ptr+8;
		buffer1=(unsigned char*)rd_ptr;

		len2=wr_ptr-start_ptr;
		buffer2=(unsigned char*)start_ptr;

		rd_ptr=start_ptr+len2;
	}
	else if(rd_ptr<wr_ptr)
	{
		len1=wr_ptr-rd_ptr;
		buffer1=(unsigned char *)rd_ptr;
		rd_ptr+=len1;
		len2=0;
	}
	else if(rd_ptr==wr_ptr)
	{
		pr_dbg("no data\n");
	}


	if(buffer1)
		buffer1_virt=phys_to_virt((long unsigned int)buffer1);
	if(buffer2)
		buffer2_virt=phys_to_virt((long unsigned int)buffer2);

	if(len1)
		dma_sync_single_for_cpu(NULL, (dma_addr_t)buffer1, len1, DMA_FROM_DEVICE);
	if(len2)
		dma_sync_single_for_cpu(NULL, (dma_addr_t)buffer2, len2, DMA_FROM_DEVICE);

	if(dmx->channel[2].used)
	{
		if(dmx->channel[2].feed && dmx->channel[2].feed->cb.ts)
		{
			dmx->channel[2].feed->cb.ts(buffer1_virt,len1,buffer2_virt,len2,&dmx->channel[2].feed->feed.ts,DMX_OK);
		}
	}
	WRITE_MPEG_REG(PARSER_SUB_RP,rd_ptr);
}
#endif

static void process_pes(struct aml_dmx *dmx)
{
}

static void process_om_read(struct aml_dmx *dmx)
{
	unsigned i;
	unsigned short om_cmd_status_data_0 = 0;
	unsigned short om_cmd_status_data_1 = 0;
//	unsigned short om_cmd_status_data_2 = 0;
	unsigned short om_cmd_data_out = 0;

	om_cmd_status_data_0 = DMX_READ_REG(dmx->id, OM_CMD_STATUS);
	om_cmd_status_data_1 = DMX_READ_REG(dmx->id, OM_CMD_DATA);
//	om_cmd_status_data_2 = DMX_READ_REG(dmx->id, OM_CMD_DATA2);

	if(om_cmd_status_data_0 & 1) {
		DMX_WRITE_REG(dmx->id, OM_DATA_RD_ADDR, (1<<15) | ((om_cmd_status_data_1 & 0xff) << 2));
		for(i = 0; i<(((om_cmd_status_data_1 >> 7) & 0x1fc) >> 1); i++) {
			om_cmd_data_out = DMX_READ_REG(dmx->id, OM_DATA_RD);
		}

		om_cmd_data_out = DMX_READ_REG(dmx->id, OM_DATA_RD_ADDR);
		DMX_WRITE_REG(dmx->id, OM_DATA_RD_ADDR, 0);
		DMX_WRITE_REG(dmx->id, OM_CMD_STATUS, 1);
	}
}

static void dmx_irq_bh_handler(unsigned long arg)
{
#if 0
	struct aml_dmx *dmx = (struct aml_dmx*)arg;
	u32 status;

	status = DMX_READ_REG(dmx->id, STB_INT_STATUS);

	if(status) {
		DMX_WRITE_REG(dmx->id, STB_INT_STATUS, status);
	}
#endif
	return;
}

static irqreturn_t dmx_irq_handler(int irq_number, void *para)
{
	extern void dvb_frontend_retune(struct dvb_frontend *fe);
	struct aml_dmx *dmx = (struct aml_dmx*)para;
	struct aml_dvb *dvb = aml_get_dvb_device();
	u32 status;
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);
	status = DMX_READ_REG(dmx->id, STB_INT_STATUS);
	if(!status) goto irq_handled;

	if (debug_irq&1) {
		pr_dbg_irq("demux %d irq status: 0x08%x\n", dmx->id, status);
	}

	if(status & (1<<SECTION_BUFFER_READY)) {
		process_section(dmx);
	}
#ifdef NO_SUB
        if(status & (1<<SUB_PES_READY)) {
		/*If the subtitle is set by tsdemux, do not parser in demux driver.*/
		if(dmx->sub_chan==-1){
			process_sub(dmx);
		}
	}
#endif
	if(status & (1<<OTHER_PES_READY)) {
		process_pes(dmx);
	}
	if(status & (1<<OM_CMD_READ_PENDING)) {
		process_om_read(dmx);
	}
	if(status & (1<<DUPLICATED_PACKET)) {
	}
	if(status & (1<<DIS_CONTINUITY_PACKET)) {
	}
	if(status & (1<<VIDEO_SPLICING_POINT)) {
	}
	if(status & (1<<AUDIO_SPLICING_POINT)) {
	}
	if(status & (1<<TS_ERROR_PIN)) {
		//pr_error("TS_ERROR_PIN\n");
	}
	 if (status & (1 << NEW_PDTS_READY)) {
		 u32 pdts_status = DMX_READ_REG(dmx->id, STB_PTS_DTS_STATUS);

		if (pdts_status & (1 << VIDEO_PTS_READY)){
			video_pts = DMX_READ_REG(dmx->id, VIDEO_PTS_DEMUX);
			if(!first_video_pts || 0 > (int)(video_pts - first_video_pts))
				first_video_pts = video_pts;
		}

		if (pdts_status & (1 << AUDIO_PTS_READY)){
		   	audio_pts = DMX_READ_REG(dmx->id, AUDIO_PTS_DEMUX);
			if(!first_audio_pts || 0 > (int)(audio_pts - first_audio_pts))
				first_audio_pts = audio_pts;
		}
	}

	if(dmx->irq_handler) {
		dmx->irq_handler(dmx->dmx_irq, (void*)dmx->id);
	}

	DMX_WRITE_REG(dmx->id, STB_INT_STATUS, status);

	//tasklet_schedule(&dmx->dmx_tasklet);

	{
		if(!dmx->int_check_time){
			dmx->int_check_time  = jiffies;
			dmx->int_check_count = 0;
		}

		if(jiffies_to_msecs(jiffies - dmx->int_check_time)>=100 || dmx->int_check_count>1000){
			if(dmx->int_check_count > 1000){
				struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
				pr_error("Too many interrupts (%d interrupts in %d ms)!\n",
						dmx->int_check_count, jiffies_to_msecs(jiffies - dmx->int_check_time));
				if(dmx->fe && !dmx->in_tune){
					DMX_WRITE_REG(dmx->id, STB_INT_MASK, 0);
					dvb_frontend_retune(dmx->fe);
				}
				dmx_reset_hw_ex(dvb, 0);
			}
			dmx->int_check_time = 0;
		}

		dmx->int_check_count++;

		if(dmx->in_tune){
			dmx->error_check++;
			if(dmx->error_check>200){
				DMX_WRITE_REG(dmx->id, STB_INT_MASK, 0);
			}
		}
	}

irq_handled:
	spin_unlock_irqrestore(&dvb->slock, flags);
	return IRQ_HANDLED;
}


static int sf_rbuf_write(struct dvb_ringbuffer *buf, const u8 *src, size_t len)
{
	ssize_t free;

	if (!len)
		return 0;
	if (!buf->data)
		return 0;

	free = dvb_ringbuffer_free(buf);
	if (len > free) {
		pr_error("sf: buffer overflow\n");
		return -EOVERFLOW;
	}

	return dvb_ringbuffer_write(buf, src, len);
}

static int sf_rbuf_filter_pkts(struct aml_swfilter *sf,
				void (*swfilter_packets)(struct dvb_demux *demux, const u8 *buf, size_t count),
				struct dvb_demux *demux)
{
	struct dvb_ringbuffer *rb = &sf->rbuf;
	ssize_t len1 = 0;
	ssize_t len2 = 0;
	size_t off;
	size_t count;
	size_t size;

	//dump(&rb->data[rb->pread], 16);

	/*
	    rb|====--------===[0x47]====|
		       ^             ^
		       wr            rd
	*/

	len1 = rb->pwrite - rb->pread;
	if (len1 < 0) {
		len1 = rb->size - rb->pread;
		len2 = rb->pwrite;
	}

	for (off=0; off<len1; off++) {
		if (rb->data[rb->pread+off] == 0x47)
			break;
	}

	if (off) {
		pr_dbg_irq_sf("off ->|%d\n", off);
	}

	len1 -= off;
	rb->pread = (rb->pread + off) % rb->size;

	count = len1/188;
	if (count) {
		pr_dbg_irq_sf("pkt >> 1[%d<->%d]\n", rb->pread, rb->pwrite);
		swfilter_packets(demux, rb->data+rb->pread, count);

		size = count*188;
		len1 -= size;
		rb->pread += size;
	}

	if (len2 && len1 && ((len1+len2)>188)) {
		pr_dbg_irq_sf("pkt >> 2[%d<->%d]\n", rb->pread, rb->pwrite);
		size = 188-len1;
		memcpy(sf->wrapbuf, rb->data+rb->pread, len1);
		memcpy(sf->wrapbuf+len1, rb->data, size);
		swfilter_packets(demux, sf->wrapbuf, 1);
		rb->pread = size;
		len2 -= size;
	}

	if (len2) {
		pr_dbg_irq_sf("pkt >> 3[%d<->%d]\n", rb->pread, rb->pwrite);
		count = len2/188;
		if (count) {
			swfilter_packets(demux, rb->data+rb->pread, count);
			rb->pread += count*188;
		}
	}
	return 0;
}

static inline int dmx_get_order(unsigned long size){
	int order;

	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);

	return order;
}

static void dvr_irq_bh_handler(unsigned long arg)
{
	struct aml_asyncfifo *afifo = (struct aml_asyncfifo *)arg;
	struct aml_dvb *dvb = afifo->dvb;
	struct aml_dmx *dmx;
	u32 size, total;
	int i, factor;
	unsigned long flags;

	if (debug_irq&2) {
		pr_dbg_irq("async fifo %d irq\n", afifo->id);
	}

	spin_lock_irqsave(&dvb->slock, flags);

	if (dvb && afifo->source >= AM_DMX_0 && afifo->source < AM_DMX_MAX) {
		dmx = &dvb->dmx[afifo->source];
		if (dmx->init && dmx->record) {
			struct aml_swfilter *sf = &dvb->swfilter;
			int issf = 0;

			total  = afifo->buf_len/afifo->flush_size;
			factor = dmx_get_order(total);
			size   = afifo->buf_len >> factor;

			if (sf->user && (sf->afifo == afifo))
				issf = 1;

			for(i=0; i<CHANNEL_COUNT; i++) {
				if(dmx->channel[i].used && dmx->channel[i].dvr_feed) {
					int cnt;
					int ret=0;

					if(afifo->buf_read > afifo->buf_toggle){
						cnt = total - afifo->buf_read;
						dma_sync_single_for_cpu(NULL, afifo->pages_map+afifo->buf_read*size, cnt*size, DMA_FROM_DEVICE);
						if (issf)
							ret = sf_rbuf_write(&sf->rbuf, (u8*)afifo->pages+afifo->buf_read*size, cnt*size);
						else
							dmx->channel[i].dvr_feed->cb.ts((u8*)afifo->pages+afifo->buf_read*size, cnt*size, NULL, 0, &dmx->channel[i].dvr_feed->feed.ts, DMX_OK);
						afifo->buf_read = 0;
					}

					if(afifo->buf_toggle > afifo->buf_read){
						cnt = afifo->buf_toggle - afifo->buf_read;
						dma_sync_single_for_cpu(NULL, afifo->pages_map+afifo->buf_read*size, cnt*size, DMA_FROM_DEVICE);
						if (issf) {
							if (ret >= 0)
								ret = sf_rbuf_write(&sf->rbuf, (u8*)afifo->pages+afifo->buf_read*size, cnt*size);
						} else
							dmx->channel[i].dvr_feed->cb.ts((u8*)afifo->pages+afifo->buf_read*size, cnt*size, NULL, 0, &dmx->channel[i].dvr_feed->feed.ts, DMX_OK);
						afifo->buf_read = afifo->buf_toggle;
					}

					if (issf) {
						if (ret>0) {
							sf_rbuf_filter_pkts(sf, dvb_dmx_swfilter_packets, dmx->channel[i].dvr_feed->demux);
						} else {
							pr_error("sf rbuf write error[%d]\n", ret);
						}
					} else if (debug_irq&2) {
						pr_dbg_irq("write data to dvr\n");
					}

					break;
				}
			}

		}
	}
	spin_unlock_irqrestore(&dvb->slock, flags);
	return;
}

static irqreturn_t dvr_irq_handler(int irq_number, void *para)
{
	struct aml_asyncfifo *afifo = (struct aml_asyncfifo *)para;
	int factor = dmx_get_order(afifo->buf_len/afifo->flush_size);

	afifo->buf_toggle++;
	afifo->buf_toggle %= (1 << factor);
	tasklet_schedule(&afifo->asyncfifo_tasklet);
	return  IRQ_HANDLED;
}

/*Enable the STB*/
static void stb_enable(struct aml_dvb *dvb)
{
	int out_src, des_in, en_des, fec_clk, hiu ,dec_clk_en;
	int src, tso_src, i;
	u32 fec_s0, fec_s1;
	u32 invert0, invert1;

	switch(dvb->stb_source) {
		case AM_TS_SRC_DMX0:
			src = dvb->dmx[0].source;
		break;
		case AM_TS_SRC_DMX1:
			src = dvb->dmx[1].source;
		break;
		case AM_TS_SRC_DMX2:
			src = dvb->dmx[2].source;
		break;
		default:
			src = dvb->stb_source;
		break;
	}

	switch(src) {
		case AM_TS_SRC_TS0:
			fec_clk = tsfile_clkdiv;
			hiu     = 0;
		break;
		case AM_TS_SRC_TS1:
			fec_clk = tsfile_clkdiv;
			hiu     = 0;
		break;
		case AM_TS_SRC_TS2:
			fec_clk = tsfile_clkdiv;
			hiu     = 0;
		break;
		case AM_TS_SRC_S_TS0:
			fec_clk = tsfile_clkdiv;
			hiu     = 0;
		break;
		case AM_TS_SRC_S_TS1:
			fec_clk = tsfile_clkdiv;
			hiu     = 0;
		break;
		case AM_TS_SRC_S_TS2:
			fec_clk = tsfile_clkdiv;
			hiu     = 0;
		break;
		case AM_TS_SRC_HIU:
			fec_clk = tsfile_clkdiv;
			hiu     = 1;
		break;
		default:
			fec_clk = 0;
			hiu     = 0;
		break;
	}

	switch(dvb->dsc_source) {
		case AM_TS_SRC_DMX0:
			des_in = 0;
			en_des = 1;
			dec_clk_en=1;
		break;
		case AM_TS_SRC_DMX1:
			des_in = 1;
			en_des = 1;
			dec_clk_en=1;
		break;
		case AM_TS_SRC_DMX2:
			des_in = 2;
			en_des = 1;
			dec_clk_en=1;
		break;
		default:
			des_in = 0;
			en_des = 0;
			dec_clk_en=0;
		break;
	}

	switch(dvb->tso_source) {
		case AM_TS_SRC_DMX0:
			tso_src = dvb->dmx[0].source;
		break;
		case AM_TS_SRC_DMX1:
			tso_src = dvb->dmx[1].source;
		break;
		case AM_TS_SRC_DMX2:
			tso_src = dvb->dmx[2].source;
		break;
		default:
			tso_src = dvb->tso_source;
		break;
	}

	switch(tso_src) {
		case AM_TS_SRC_TS0:
			out_src = 0;
		break;
		case AM_TS_SRC_TS1:
			out_src = 1;
		break;
		case AM_TS_SRC_TS2:
			out_src = 2;
		break;
		case AM_TS_SRC_S_TS0:
		case AM_TS_SRC_S_TS1:
		case AM_TS_SRC_S_TS2:
			out_src = 6;
		break;
		case AM_TS_SRC_HIU:
			out_src = 7;
		break;
		default:
			out_src = 0;
		break;
	}

	printk("[stb]src: %d, dsc: %d, tso: %d\n",
			src, des_in, out_src);

	fec_s0  = 0;
	fec_s1  = 0;
	invert0 = 0;
	invert1 = 0;

	for(i = 0; i < TS_IN_COUNT; i++){
		if(dvb->ts[i].s2p_id == 0)
			fec_s0 = i;
		else if(dvb->ts[i].s2p_id == 1)
			fec_s1 = i;
	}

	invert0 = dvb->s2p[0].invert;
	invert1 = dvb->s2p[1].invert;

	WRITE_MPEG_REG(STB_TOP_CONFIG,
		(invert1<<INVERT_S2P1_FEC_CLK) |
		(fec_s1<<S2P1_FEC_SERIAL_SEL)  |
		(out_src<<TS_OUTPUT_SOURCE) |
		(des_in<<DES_INPUT_SEL)     |
		(en_des<<ENABLE_DES_PL)     |
		(dec_clk_en<<ENABLE_DES_PL_CLK)|
		(invert0<<INVERT_S2P0_FEC_CLK) |
		(fec_s0<<S2P0_FEC_SERIAL_SEL));

	if(dvb->reset_flag)
		hiu = 0;

	WRITE_MPEG_REG(TS_FILE_CONFIG,
		(demux_skipbyte << 16)                  |
		(6<<DES_OUT_DLY)                      |
		(3<<TRANSPORT_SCRAMBLING_CONTROL_ODD) |
		(hiu<<TS_HIU_ENABLE)                  |
		(fec_clk<<FEC_FILE_CLK_DIV));
}

int dsc_set_pid(struct aml_dsc *dsc, int pid)
{
	u32 data;

	WRITE_MPEG_REG(TS_PL_PID_INDEX, (dsc->id & 0x0f)>>1);
	data = READ_MPEG_REG(TS_PL_PID_DATA);
	if(dsc->id&1) {
		data &= 0xFFFF0000;
		data |= pid&0x1fff;
		if (!dsc->used)
			data |= 1<<PID_MATCH_DISABLE_LOW;
	} else {
		data &= 0xFFFF;
		data |= (pid&0x1fff)<<16;
		if (!dsc->used)
			data |= 1<<PID_MATCH_DISABLE_HIGH;
	}
	WRITE_MPEG_REG(TS_PL_PID_INDEX, (dsc->id & 0x0f)>>1);
	WRITE_MPEG_REG(TS_PL_PID_DATA, data);
	WRITE_MPEG_REG(TS_PL_PID_INDEX, 0);

	if (dsc->used) {
		pr_dbg("set DSC %d PID %d\n", dsc->id, pid);
	} else {
		pr_dbg("disable DSC %d\n", dsc->id);
	}
	return 0;
}

int dsc_set_key(struct aml_dsc *dsc, int type, u8 *key)
{
	u16 k0, k1, k2, k3;
	u32 key0, key1;

	k0 = (key[0]<<8)|key[1];
	k1 = (key[2]<<8)|key[3];
	k2 = (key[4]<<8)|key[5];
	k3 = (key[6]<<8)|key[7];

	key0 = (k0<<16)|k1;
	key1 = (k2<<16)|k3;
	WRITE_MPEG_REG(COMM_DESC_KEY0, key0);
	WRITE_MPEG_REG(COMM_DESC_KEY1, key1);
	WRITE_MPEG_REG(COMM_DESC_KEY_RW, (dsc->id + type*DSC_COUNT));

	pr_dbg("set DSC %d type %d key %04x %04x %04x %04x\n", dsc->id, type,
			k0, k1, k2, k3);
	return 0;
}

/*Set section buffer*/
static int dmx_alloc_sec_buffer(struct aml_dmx *dmx)
{
	unsigned long base;
	unsigned long grp_addr[SEC_BUF_GRP_COUNT];
	int grp_len[SEC_BUF_GRP_COUNT];
	int i;

	if(dmx->sec_pages)
		return 0;

	grp_len[0] = (1<<SEC_GRP_LEN_0)*8;
	grp_len[1] = (1<<SEC_GRP_LEN_1)*8;
	grp_len[2] = (1<<SEC_GRP_LEN_2)*8;
	grp_len[3] = (1<<SEC_GRP_LEN_3)*8;

	dmx->sec_total_len = grp_len[0]+grp_len[1]+grp_len[2]+grp_len[3];
	dmx->sec_pages = __get_free_pages(GFP_KERNEL, get_order(dmx->sec_total_len));
	if(!dmx->sec_pages) {
		pr_error("cannot allocate section buffer %d bytes %d order\n", dmx->sec_total_len, get_order(dmx->sec_total_len));
		return -1;
	}
	dmx->sec_pages_map = dma_map_single(NULL, (void*)dmx->sec_pages, dmx->sec_total_len, DMA_FROM_DEVICE);

	grp_addr[0] = dmx->sec_pages_map;

	grp_addr[1] = grp_addr[0]+grp_len[0];
	grp_addr[2] = grp_addr[1]+grp_len[1];
	grp_addr[3] = grp_addr[2]+grp_len[2];

	dmx->sec_buf[0].addr = dmx->sec_pages;
	dmx->sec_buf[0].len  = grp_len[0]/8;

	for(i=1; i<SEC_BUF_COUNT; i++) {
		dmx->sec_buf[i].addr = dmx->sec_buf[i-1].addr+dmx->sec_buf[i-1].len;
		dmx->sec_buf[i].len  = grp_len[i/8]/8;
	}

	base = grp_addr[0]&0xFFFF0000;
	DMX_WRITE_REG(dmx->id, SEC_BUFF_BASE, base>>16);
	DMX_WRITE_REG(dmx->id, SEC_BUFF_01_START, (((grp_addr[0]-base)>>8)<<16)|((grp_addr[1]-base)>>8));
	DMX_WRITE_REG(dmx->id, SEC_BUFF_23_START, (((grp_addr[2]-base)>>8)<<16)|((grp_addr[3]-base)>>8));
	DMX_WRITE_REG(dmx->id, SEC_BUFF_SIZE, SEC_GRP_LEN_0|
		(SEC_GRP_LEN_1<<4)|
		(SEC_GRP_LEN_2<<8)|
		(SEC_GRP_LEN_3<<12));

	return 0;
}

#ifdef NO_SUB
/*Set subtitle buffer*/
static int dmx_alloc_sub_buffer(struct aml_dmx *dmx)
{
	unsigned long addr;

	if(dmx->sub_pages)
		return 0;

	dmx->sub_buf_len = 64*1024;
	dmx->sub_pages = __get_free_pages(GFP_KERNEL, get_order(dmx->sub_buf_len));
	if(!dmx->sub_pages) {
		pr_error("cannot allocate subtitle buffer\n");
		return -1;
	}
	dmx->sub_pages_map = dma_map_single(NULL, (void*)dmx->sub_pages, dmx->sub_buf_len, DMA_FROM_DEVICE);

	addr = virt_to_phys((void*)dmx->sub_pages);
	DMX_WRITE_REG(dmx->id, SB_START, addr>>12);
	DMX_WRITE_REG(dmx->id, SB_LAST_ADDR, (dmx->sub_buf_len>>3)-1);
	return 0;
}
#endif /*NO_SUB*/

/*Set PES buffer*/
static int dmx_alloc_pes_buffer(struct aml_dmx *dmx)
{
	unsigned long addr;

	if(dmx->pes_pages)
		return 0;

	dmx->pes_buf_len = 64*1024;
	dmx->pes_pages = __get_free_pages(GFP_KERNEL, get_order(dmx->pes_buf_len));
	if(!dmx->pes_pages) {
		pr_error("cannot allocate pes buffer\n");
		return -1;
	}
	dmx->pes_pages_map = dma_map_single(NULL, (void*)dmx->pes_pages, dmx->pes_buf_len, DMA_FROM_DEVICE);

	addr = virt_to_phys((void*)dmx->pes_pages);
	DMX_WRITE_REG(dmx->id, OB_START, addr>>12);
	DMX_WRITE_REG(dmx->id, OB_LAST_ADDR, (dmx->pes_buf_len>>3)-1);
	return 0;
}

/*Allocate ASYNC FIFO Buffer*/
static unsigned long asyncfifo_alloc_buffer(int len)
{
	unsigned long pages = __get_free_pages(GFP_KERNEL, get_order(len));
	if (!pages) {
		pr_error("cannot allocate async fifo buffer\n");
		return 0;
	}
	return pages;
}
static void asyncfifo_free_buffer(unsigned long buf, int len)
{
	free_pages(buf, get_order(len));
}

static int asyncfifo_set_buffer(struct aml_asyncfifo *afifo,
							int len, unsigned long buf)
{
	if (afifo->pages)
		return -1;

	afifo->buf_toggle = 0;
	afifo->buf_read   = 0;
	afifo->buf_len = len;
	pr_error("async fifo %d buf size %d, flush size %d\n", afifo->id, afifo->buf_len, afifo->flush_size);
	if (afifo->flush_size <= 0) {
		afifo->flush_size = afifo->buf_len>>1;
	}
	afifo->pages = buf;
	if (!afifo->pages)
		return -1;

	afifo->pages_map = dma_map_single(NULL, (void*)afifo->pages, afifo->buf_len, DMA_FROM_DEVICE);

	return 0;
}
static void asyncfifo_put_buffer(struct aml_asyncfifo *afifo)
{
	if (afifo->pages) {
		dma_unmap_single(NULL, afifo->pages_map, afifo->buf_len, DMA_FROM_DEVICE);
		asyncfifo_free_buffer(afifo->pages, afifo->buf_len);
		afifo->pages_map = 0;
		afifo->pages = 0;
	}
}

int async_fifo_init(struct aml_asyncfifo *afifo, int initirq,
			int buf_len, unsigned long buf)
{
	int ret = 0;
	int irq;

	if (afifo->init)
		return -1;

	afifo->source  = AM_DMX_MAX;
	afifo->pages = 0;
	afifo->buf_toggle = 0;
	afifo->buf_read = 0;
	afifo->buf_len = 0;

	if (afifo->asyncfifo_irq == -1) {
		pr_error("no irq for ASYNC_FIFO%d\n", afifo->id);
		/*Do not return error*/
		return -1;
	}

	tasklet_init(&afifo->asyncfifo_tasklet, dvr_irq_bh_handler, (unsigned long)afifo);
	if (initirq)
		irq = request_irq(afifo->asyncfifo_irq, dvr_irq_handler, IRQF_SHARED, "dvr irq", afifo);
	else
		enable_irq(afifo->asyncfifo_irq);

	/*alloc buffer*/
	ret = asyncfifo_set_buffer(afifo, buf_len, buf);

	afifo->init = 1;

	return ret;
}

int async_fifo_deinit(struct aml_asyncfifo *afifo, int freeirq)
{
	if (! afifo->init)
		return 0;

	CLEAR_ASYNC_FIFO_REG_MASK(afifo->id, REG1, 1 << ASYNC_FIFO_FLUSH_EN);
	CLEAR_ASYNC_FIFO_REG_MASK(afifo->id, REG2, 1 << ASYNC_FIFO_FILL_EN);

	asyncfifo_put_buffer(afifo);

	afifo->source  = AM_DMX_MAX;
	afifo->buf_toggle = 0;
	afifo->buf_read = 0;
	afifo->buf_len = 0;

	if (afifo->asyncfifo_irq != -1) {
		if (freeirq)
			free_irq(afifo->asyncfifo_irq, afifo);
		else
			disable_irq(afifo->asyncfifo_irq);
	}
	tasklet_kill(&afifo->asyncfifo_tasklet);

	afifo->init = 0;

	return 0;
}

/*Initalize the registers*/
static int dmx_init(struct aml_dmx *dmx)
{
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	int irq;

	if(dmx->init) return 0;

	pr_dbg("demux init\n");

	/*Register irq handlers*/
	if(dmx->dmx_irq!=-1) {
		pr_dbg("request irq\n");
		tasklet_init(&dmx->dmx_tasklet, dmx_irq_bh_handler, (unsigned long)dmx);
		irq = request_irq(dmx->dmx_irq, dmx_irq_handler, IRQF_SHARED, "dmx irq", dmx);
	}

	/*Allocate buffer*/
	if(dmx_alloc_sec_buffer(dmx)<0)
		return -1;
#ifdef NO_SUB
	if(dmx_alloc_sub_buffer(dmx)<0)
		return -1;
#endif
	if(dmx_alloc_pes_buffer(dmx)<0)
		return -1;

	/*Reset the hardware*/
	if (!dvb->dmx_init) {
		init_timer(&dvb->watchdog_timer);
		dvb->watchdog_timer.function = section_buffer_watchdog_func;
		dvb->watchdog_timer.expires  = jiffies + msecs_to_jiffies(WATCHDOG_TIMER);
		dvb->watchdog_timer.data     = (unsigned long)dvb;
#ifdef ENABLE_SEC_BUFF_WATCHDOG
		add_timer(&dvb->watchdog_timer);
#endif
		dmx_reset_hw(dvb);
	}

	dvb->dmx_init++;

	memset(dmx->sec_buf_watchdog_count, 0, sizeof(dmx->sec_buf_watchdog_count));

	dmx->init = 1;

	return 0;
}

/*Release the resource*/
static int dmx_deinit(struct aml_dmx *dmx)
{
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;

	if(!dmx->init) return 0;

	DMX_WRITE_REG(dmx->id, DEMUX_CONTROL, 0);

	dvb->dmx_init--;

	/*Reset the hardware*/
	if(!dvb->dmx_init) {
		dmx_reset_hw(dvb);
#ifdef ENABLE_SEC_BUFF_WATCHDOG
		del_timer_sync(&dvb->watchdog_timer);
#endif
	}

	if(dmx->sec_pages) {
        dma_unmap_single(NULL,  dmx->sec_pages_map, dmx->sec_total_len, DMA_FROM_DEVICE);
		free_pages(dmx->sec_pages, get_order(dmx->sec_total_len));
		dmx->sec_pages = 0;
        dmx->sec_pages_map = 0;
	}
#ifdef NO_SUB
	if(dmx->sub_pages) {
		dma_unmap_single(NULL,  dmx->sub_pages_map, dmx->sub_buf_len, DMA_FROM_DEVICE);
		free_pages(dmx->sub_pages, get_order(dmx->sub_buf_len));
		dmx->sub_pages = 0;
	}
#endif
	if(dmx->pes_pages) {
		dma_unmap_single(NULL,  dmx->pes_pages_map, dmx->pes_buf_len, DMA_FROM_DEVICE);
		free_pages(dmx->pes_pages, get_order(dmx->pes_buf_len));
		dmx->pes_pages = 0;
	}

	if(dmx->dmx_irq!=-1) {
		free_irq(dmx->dmx_irq, dmx);
		tasklet_kill(&dmx->dmx_tasklet);
	}

	dmx->init = 0;

	return 0;
}

/*Check the record flag*/
static int dmx_get_record_flag(struct aml_dmx *dmx)
{
	int i, linked = 0, record_flag = 0;
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;

	/*Check whether a async fifo connected to this dmx*/
	for (i=0; i<ASYNCFIFO_COUNT; i++) {
		if (! dvb->asyncfifo[i].init)
			continue;
		if ((dvb->asyncfifo[i].source == dmx->id)
			//&& !(dvb->swfilter.user && (i==SF_AFIFO_ID))/*sf mode reserved*/
			) {
			linked = 1;
			break;
		}
	}


	for(i=0; i<CHANNEL_COUNT; i++) {
		if(dmx->channel[i].used && dmx->channel[i].dvr_feed) {
			if (! dmx->record) {
				dmx->record = 1;

				if (linked) {
					/*A new record will start, must reset the async fifos for linking the right demux*/
					reset_async_fifos(dvb);
				}
			}
			if (linked) {
				record_flag = 1;
			}
			goto find_done;
		}
	}

	if (dmx->record) {
		dmx->record = 0;
		if (linked) {
			/*A record will stop, reset the async fifos for linking the right demux*/
			reset_async_fifos(dvb);
		}
	}


find_done:
	return record_flag;
}


/*Enable the demux device*/
static int dmx_enable(struct aml_dmx *dmx)
{
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	int fec_sel, hi_bsf, fec_ctrl, record;
	int fec_core_sel=0;
	int set_stb = 0, fec_s = 0;
	int s2p_id;
	u32 invert0 = 0, invert1 = 0, fec_s0 = 0, fec_s1 = 0;

	record = dmx_get_record_flag(dmx);

	switch(dmx->source) {
		case AM_TS_SRC_TS0:
			fec_sel  = 0;
			fec_ctrl = dvb->ts[0].control;
			record   = record?1:0;
			break;
		case AM_TS_SRC_TS1:
			fec_sel  = 1;
			fec_ctrl = dvb->ts[1].control;
			record   = record?1:0;
		break;
		case AM_TS_SRC_TS2:
			fec_sel  = 2;
			fec_ctrl = dvb->ts[2].control;
			record   = record?1:0;
		break;
		case AM_TS_SRC_S_TS0:
		case AM_TS_SRC_S_TS1:
		case AM_TS_SRC_S_TS2:
			s2p_id   = 0;
			fec_ctrl = 0;
			if(dmx->source == AM_TS_SRC_S_TS0){
				s2p_id   = dvb->ts[0].s2p_id;
				fec_ctrl = dvb->ts[0].control;
			}else if(dmx->source == AM_TS_SRC_S_TS1){
				s2p_id   = dvb->ts[1].s2p_id;
				fec_ctrl = dvb->ts[1].control;
			}else if(dmx->source == AM_TS_SRC_S_TS2){
				s2p_id   = dvb->ts[2].s2p_id;
				fec_ctrl = dvb->ts[2].control;
			}
			fec_sel = (s2p_id == 1) ? 5 : 6;
			record  = record?1:0;
			set_stb = 1;
			fec_s   = dmx->source - AM_TS_SRC_S_TS0;
		break;
		case AM_TS_SRC_HIU:
			fec_sel  = 7;
			fec_ctrl = 0;
			record   = 0;
		break;
		default:
			fec_sel  = 0;
			fec_ctrl = 0;
			record   = 0;
		break;
	}

	if(dmx->channel[0].used || dmx->channel[1].used)
		hi_bsf = 1;
	else
		hi_bsf = 0;

	if ((dvb->dsc_source!=-1)
		&& ((dvb->dsc_source-AM_TS_SRC_DMX0)== dmx->id))
		fec_core_sel = 1;

	pr_dbg("[dmx-%d]src: %d, rec: %d, hi_bsf: %d, dsc: %d\n",
			dmx->id, dmx->source, record, hi_bsf, fec_core_sel);

	if(dmx->chan_count) {
		if(set_stb){
			u32 v = READ_MPEG_REG(STB_TOP_CONFIG);
			int i;

			for(i = 0; i < TS_IN_COUNT; i++){
				if (dvb->ts[i].s2p_id == 0){
					fec_s0 = i;
				}else if(dvb->ts[i].s2p_id == 1){
					fec_s1 = i;
				}
			}

			invert0 = dvb->s2p[0].invert;
			invert1 = dvb->s2p[1].invert;

			v &= ~((0x3<<S2P0_FEC_SERIAL_SEL)|
				(0x1f<<INVERT_S2P0_FEC_CLK)|
				(0x3<<S2P1_FEC_SERIAL_SEL) |
				(0x1f<<INVERT_S2P1_FEC_CLK));

			v |= (fec_s0<<S2P0_FEC_SERIAL_SEL)|
				(invert0<<INVERT_S2P0_FEC_CLK)|
				(fec_s1<<S2P1_FEC_SERIAL_SEL) |
				(invert1<<INVERT_S2P1_FEC_CLK);
			WRITE_MPEG_REG(STB_TOP_CONFIG, v);
		}

		/*Initialize the registers*/
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, DEMUX_INT_MASK);
		DMX_WRITE_REG(dmx->id, DEMUX_MEM_REQ_EN,
#ifdef USE_AHB_MODE
			(1<<SECTION_AHB_DMA_EN)         |
			(0<<SUB_AHB_DMA_EN)       |
			(1<<OTHER_PES_AHB_DMA_EN)       |
#endif
			(1<<SECTION_PACKET)             |
			(1<<VIDEO_PACKET)               |
			(1<<AUDIO_PACKET)               |
			(1<<SUB_PACKET)                 |
			(1<<SCR_ONLY_PACKET)		|
			(1<<OTHER_PES_PACKET));
	 	DMX_WRITE_REG(dmx->id, PES_STRONG_SYNC, 0x1234);
 		DMX_WRITE_REG(dmx->id, DEMUX_ENDIAN,
			(7<<OTHER_ENDIAN)               |
			(7<<BYPASS_ENDIAN)              |
			(0<<SECTION_ENDIAN));
		DMX_WRITE_REG(dmx->id, TS_HIU_CTL,
			(0<<LAST_BURST_THRESHOLD)       |
			(hi_bsf<<USE_HI_BSF_INTERFACE));


		DMX_WRITE_REG(dmx->id, FEC_INPUT_CONTROL,
			(fec_core_sel<<FEC_CORE_SEL)    |
			(fec_sel<<FEC_SEL)              |
			(fec_ctrl<<0));
		DMX_WRITE_REG(dmx->id, STB_OM_CTL,
			(0x40<<MAX_OM_DMA_COUNT)        |
			(0x7f<<LAST_OM_ADDR));
		DMX_WRITE_REG(dmx->id, DEMUX_CONTROL,
			(0<<BYPASS_USE_RECODER_PATH)          |
			(0<<INSERT_AUDIO_PES_STRONG_SYNC)     |
			(0<<INSERT_VIDEO_PES_STRONG_SYNC)     |
			(0<<OTHER_INT_AT_PES_BEGINING)        |
			(0<<DISCARD_AV_PACKAGE)               |
			((!!dmx->dump_ts_select)<<TS_RECORDER_SELECT)               |
			(record<<TS_RECORDER_ENABLE)          |
			(1<<KEEP_DUPLICATE_PACKAGE)           |
			//(1<<SECTION_END_WITH_TABLE_ID)        |
			(1<<ENABLE_FREE_CLK_FEC_DATA_VALID)   |
			(1<<ENABLE_FREE_CLK_STB_REG)          |
			(1<<STB_DEMUX_ENABLE));
	} else {
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, 0);
		DMX_WRITE_REG(dmx->id, FEC_INPUT_CONTROL, 0);
		DMX_WRITE_REG(dmx->id, DEMUX_CONTROL, 0);
	}


	return 0;
}

static int dmx_set_misc(struct aml_dmx *dmx, int hi_bsf, int en_dsc)
{
	if(hi_bsf>=0) {
		DMX_WRITE_REG(dmx->id, TS_HIU_CTL,
			hi_bsf? (DMX_READ_REG(dmx->id, TS_HIU_CTL) |(1<<USE_HI_BSF_INTERFACE))
				: (DMX_READ_REG(dmx->id, TS_HIU_CTL) & (~(1<<USE_HI_BSF_INTERFACE))));
	}

	if(en_dsc>=0) {
		DMX_WRITE_REG(dmx->id, FEC_INPUT_CONTROL,
			en_dsc? (DMX_READ_REG(dmx->id, FEC_INPUT_CONTROL) | (1<<FEC_CORE_SEL))
				: (DMX_READ_REG(dmx->id, FEC_INPUT_CONTROL) & (~(1<<FEC_CORE_SEL))));
	}

	return 0;
}

static int dmx_set_misc_id(struct aml_dvb *dvb, int id, int hi_bsf, int en_dsc)
{
	return dmx_set_misc(&dvb->dmx[id], hi_bsf, en_dsc);
}

/*Get the channel's ID by its PID*/
static int dmx_get_chan(struct aml_dmx *dmx, int pid)
{
	int id;

	for(id=0; id<CHANNEL_COUNT; id++) {
		if(dmx->channel[id].used && dmx->channel[id].pid==pid)
			return id;
	}

	return -1;
}

/*Get the channel's target*/
static u32 dmx_get_chan_target(struct aml_dmx *dmx, int cid)
{
	u32 type;

	if(!dmx->channel[cid].used) {
		return 0xFFFF;
	}

	if(dmx->channel[cid].type==DMX_TYPE_SEC) {
		type = SECTION_PACKET;
	} else {
		switch(dmx->channel[cid].pes_type) {
			case DMX_PES_AUDIO:
				type = AUDIO_PACKET;
			break;
			case DMX_PES_VIDEO:
				type = VIDEO_PACKET;
			break;
			case DMX_PES_SUBTITLE:
			case DMX_PES_TELETEXT:
				type = SUB_PACKET;
			break;
			case DMX_PES_PCR:
				type=SCR_ONLY_PACKET;
			break;
			default:
				type = OTHER_PES_PACKET;
			break;
		}
	}

	pr_dbg("chan target: %x %x\n", type, dmx->channel[cid].pid);
	return (type<<PID_TYPE)|dmx->channel[cid].pid;
}

/*Get the advance value of the channel*/
static inline u32 dmx_get_chan_advance(struct aml_dmx *dmx, int cid)
{
	return 0;
}

/*Set the channel registers*/
static int dmx_set_chan_regs(struct aml_dmx *dmx, int cid)
{
	u32 data, addr, advance, max;

	pr_dbg("set channel (id:%d PID:0x%x) registers\n", cid, dmx->channel[cid].pid);

	while(DMX_READ_REG(dmx->id, FM_WR_ADDR) & 0x8000) {
		udelay(1);
	}

	if(cid&1) {
		data = (dmx_get_chan_target(dmx, cid-1)<<16) | dmx_get_chan_target(dmx, cid);
		advance = (dmx_get_chan_advance(dmx, cid)<<8) | dmx_get_chan_advance(dmx, cid-1);
	} else {
		data = (dmx_get_chan_target(dmx, cid)<<16) | dmx_get_chan_target(dmx, cid+1);
		advance = (dmx_get_chan_advance(dmx, cid+1)<<8) | dmx_get_chan_advance(dmx, cid);
	}
	addr = cid>>1;
	DMX_WRITE_REG(dmx->id, FM_WR_DATA, data);
	DMX_WRITE_REG(dmx->id, FM_WR_ADDR, (advance<<16)|0x8000|addr);

	pr_dbg("write fm %x:%x\n", (advance<<16)|0x8000|addr, data);

	for(max=CHANNEL_COUNT-1; max>0; max--) {
		if(dmx->channel[max].used)
			break;
	}

	data = DMX_READ_REG(dmx->id, MAX_FM_COMP_ADDR)&0xF0;
	DMX_WRITE_REG(dmx->id, MAX_FM_COMP_ADDR, data|(max>>1));

	pr_dbg("write fm comp %x\n", data|(max>>1));

	if(DMX_READ_REG(dmx->id, OM_CMD_STATUS)&0x8e00) {
		pr_error("error send cmd %x\n", DMX_READ_REG(dmx->id, OM_CMD_STATUS));
	}

	if(cid == 0)
		first_video_pts = 0;
	else if(cid == 1)
		first_audio_pts = 0;

	return 0;
}

/*Get the filter target*/
static int dmx_get_filter_target(struct aml_dmx *dmx, int fid, u32 *target, u8 *advance)
{
	struct dmx_section_filter *filter;
	struct aml_filter *f;
	int i, cid, neq_bytes;

	fid = fid&0xFFFF;
	f = &dmx->filter[fid];

	if(!f->used) {
		target[0] = 0x1fff;
		advance[0] = 0;
		for(i=1; i<FILTER_LEN; i++) {
			target[i] = 0x9fff;
			advance[i] = 0;
		}
		return 0;
	}

	cid = f->chan_id;
	filter = f->filter;

	neq_bytes = 0;
	if(filter->filter_mode[0]!=0xFF) {
		neq_bytes = 2;
	} else {
		for(i=3; i<FILTER_LEN; i++) {
			if(filter->filter_mode[i]!=0xFF)
				neq_bytes++;
		}
	}

	f->neq = 0;

	for(i=0; i<FILTER_LEN; i++) {
		u8 value = filter->filter_value[i];
		u8 mask  = filter->filter_mask[i];
		u8 mode  = filter->filter_mode[i];
		u8 mb, mb1, nb, v, t, adv = 0;

		if(!i) {
			mb = 1;
			mb1= 1;
			v  = 0;
			if(mode==0xFF) {
				t = mask&0xF0;
				mb1 = 0;
				adv |= t^0xF0;
				v   |= (value&0xF0)|adv;

				t = mask&0x0F;
				mb  = 0;
				adv |= t^0x0F;
				v   |= (value&0x0F)|adv;
			}

			target[i] = (mb<<SECTION_FIRSTBYTE_MASKLOW)         |
				(mb1<<SECTION_FIRSTBYTE_MASKHIGH)           |
				(0<<SECTION_FIRSTBYTE_DISABLE_PID_CHECK)    |
				(cid<<SECTION_FIRSTBYTE_PID_INDEX)          |
				v;
			advance[i] = adv;
		} else {
			if(i < 3){
				value = 0;
				mask  = 0;
				mode  = 0xff;
			}
			mb = 1;
			nb = 0;
			v = 0;

			if((i>=3) && mask) {
			if(mode==0xFF) {
				mb  = 0;
				nb  = 0;
				adv = mask^0xFF;
				v   = value|adv;
			} else {
				if(neq_bytes==1) {
					mb  = 0;
					nb  = 1;
					adv = mask^0xFF;
					v   = value&~adv;
				}
			}
			}
			target[i] = (mb<<SECTION_RESTBYTE_MASK)             |
				(nb<<SECTION_RESTBYTE_MASK_EQ)              |
				(0<<SECTION_RESTBYTE_DISABLE_PID_CHECK)     |
				(cid<<SECTION_RESTBYTE_PID_INDEX)           |
				v;
			advance[i] = adv;
		}

		f->value[i] = value;
		f->maskandmode[i] = mask&mode;
		f->maskandnotmode[i] = mask&~mode;

		if(f->maskandnotmode[i])
			f->neq = 1;
	}

	return 0;
}

/*Set the filter registers*/
static int dmx_set_filter_regs(struct aml_dmx *dmx, int fid)
{
	u32 t1[FILTER_LEN], t2[FILTER_LEN];
	u8 advance1[FILTER_LEN], advance2[FILTER_LEN];
	u32 addr, data, max, adv;
	int i;

	pr_dbg("set filter (id:%d) registers\n", fid);

	if(fid&1) {
		dmx_get_filter_target(dmx, fid-1, t1, advance1);
		dmx_get_filter_target(dmx, fid, t2, advance2);
	} else {
		dmx_get_filter_target(dmx, fid, t1, advance1);
		dmx_get_filter_target(dmx, fid+1, t2, advance2);
	}

	for(i=0; i<FILTER_LEN; i++) {
		while(DMX_READ_REG(dmx->id, FM_WR_ADDR) & 0x8000) {
			udelay(1);
		}

		data = (t1[i]<<16)|t2[i];
		addr = (fid>>1)|((i+1)<<4);
		adv  = (advance1[i]<<8)|advance2[i];

		DMX_WRITE_REG(dmx->id, FM_WR_DATA, data);
		DMX_WRITE_REG(dmx->id, FM_WR_ADDR, (adv<<16)|0x8000|addr);

		pr_dbg("write fm %x:%x\n", (adv<<16)|0x8000|addr, data);
	}

	for(max=FILTER_COUNT-1; max>0; max--) {
		if(dmx->filter[max].used)
			break;
	}

	data = DMX_READ_REG(dmx->id, MAX_FM_COMP_ADDR)&0xF;
	DMX_WRITE_REG(dmx->id, MAX_FM_COMP_ADDR, data|((max>>1)<<4));

	pr_dbg("write fm comp %x\n", data|((max>>1)<<4));

	if(DMX_READ_REG(dmx->id, OM_CMD_STATUS)&0x8e00) {
		pr_error("error send cmd %x\n",DMX_READ_REG(dmx->id, OM_CMD_STATUS));
	}

	return 0;
}

/*Clear the filter's buffer*/
static void dmx_clear_filter_buffer(struct aml_dmx *dmx, int fid)
{
	u32 section_busy32 = DMX_READ_REG(dmx->id, SEC_BUFF_READY);
	u32 filter_number;
	int i;

	if(!section_busy32)
		return;

	for(i = 0; i < SEC_BUF_COUNT; i++) {
		if(section_busy32 & (1 << i)) {
			DMX_WRITE_REG(dmx->id, SEC_BUFF_NUMBER, i);
			filter_number = (DMX_READ_REG(dmx->id, SEC_BUFF_NUMBER) >> 8);
			if(filter_number != fid) {
				section_busy32 &= ~(1 << i);
			}
		}
	}

	if(section_busy32) {
		DMX_WRITE_REG(dmx->id, SEC_BUFF_READY, section_busy32);
	}
}

static void async_fifo_set_regs(struct aml_asyncfifo *afifo, int source_val)
{
	u32 start_addr = virt_to_phys((void*)afifo->pages);
	u32 size = afifo->buf_len;
	u32 flush_size = afifo->flush_size;
	int factor = dmx_get_order(size/flush_size);

	pr_dbg("ASYNC FIFO id=%d, link to DMX%d, start_addr %x, buf_size %d, source value 0x%x, factor %d\n",
			afifo->id, afifo->source, start_addr, size, source_val, factor);
	/* Destination address*/
	WRITE_ASYNC_FIFO_REG(afifo->id, REG0, start_addr);

	/* Setup flush parameters*/
	WRITE_ASYNC_FIFO_REG(afifo->id, REG1,
						(0 << ASYNC_FIFO_TO_HIU)   |
						(0 << ASYNC_FIFO_FLUSH)    |   // don't flush the path
						(1 << ASYNC_FIFO_RESET)    |   // reset the path
						(1 << ASYNC_FIFO_WRAP_EN)  |   // wrap enable
						(0 << ASYNC_FIFO_FLUSH_EN) |   // disable the flush path
						//(0x3 << ASYNC_FIFO_FLUSH_CNT_LSB);     // flush 3 x 32  32-bit words
						//(0x7fff << ASYNC_FIFO_FLUSH_CNT_LSB);     // flush 4MBytes of data
						(((size >> 7) & 0x7fff) << ASYNC_FIFO_FLUSH_CNT_LSB));     // number of 128-byte blocks to flush

	WRITE_ASYNC_FIFO_REG(afifo->id, REG1, READ_ASYNC_FIFO_REG(afifo->id, REG1) & ~(1 << ASYNC_FIFO_RESET));     // clear the reset signal
	WRITE_ASYNC_FIFO_REG(afifo->id, REG1, READ_ASYNC_FIFO_REG(afifo->id, REG1) | (1 << ASYNC_FIFO_FLUSH_EN));   // Enable flush

	/*Setup Fill parameters*/
	WRITE_ASYNC_FIFO_REG(afifo->id, REG2,
					(1 <<  ASYNC_FIFO_ENDIAN_LSB)  |
					(0 <<  ASYNC_FIFO_FILL_EN)     |   // disable fill path to reset fill path
					//(96 << ASYNC_FIFO_FILL_CNT_LSB);   // 3 x 32  32-bit words
					(0 << ASYNC_FIFO_FILL_CNT_LSB));   // forever FILL;
	WRITE_ASYNC_FIFO_REG(afifo->id, REG2, READ_ASYNC_FIFO_REG(afifo->id, REG2) | (1 <<  ASYNC_FIFO_FILL_EN));       // Enable fill path

	WRITE_ASYNC_FIFO_REG(afifo->id, REG3, (READ_ASYNC_FIFO_REG(afifo->id, REG3)&0xffff0000) | ((((size >> (factor + 7)) - 1) & 0x7fff) << ASYNC_FLUSH_SIZE_IRQ_LSB)); // generate flush interrupt

	/* Connect the STB DEMUX to ASYNC_FIFO*/
	WRITE_ASYNC_FIFO_REG(afifo->id, REG2, READ_ASYNC_FIFO_REG(afifo->id, REG2) | (source_val << ASYNC_FIFO_SOURCE_LSB));
}

/*Reset the ASYNC FIFOS when a ASYNC FIFO connect to a different DMX*/
static void reset_async_fifos(struct aml_dvb *dvb)
{
	struct aml_asyncfifo *low_dmx_fifo = NULL;
	struct aml_asyncfifo *high_dmx_fifo = NULL;
	int i, j;
	int record_enable;

	pr_dbg("reset ASYNC FIFOs\n");
	for (i=0; i<ASYNCFIFO_COUNT; i++) {
		if (! dvb->asyncfifo[i].init)
			continue;
		pr_dbg("Disable ASYNC FIFO id=%d\n", dvb->asyncfifo[i].id);
		CLEAR_ASYNC_FIFO_REG_MASK(dvb->asyncfifo[i].id, REG1, 1 << ASYNC_FIFO_FLUSH_EN);
		CLEAR_ASYNC_FIFO_REG_MASK(dvb->asyncfifo[i].id, REG2, 1 << ASYNC_FIFO_FILL_EN);
		if (READ_ASYNC_FIFO_REG(dvb->asyncfifo[i].id,REG2)&(1 << ASYNC_FIFO_FILL_EN) ||
			READ_ASYNC_FIFO_REG(dvb->asyncfifo[i].id,REG1)&(1 << ASYNC_FIFO_FLUSH_EN)) {
			pr_dbg("Set reg failed\n");
		} else {
			pr_dbg("Set reg ok\n");
		}
		dvb->asyncfifo[i].buf_toggle = 0;
		dvb->asyncfifo[i].buf_read = 0;
	}

	for (j=0; j<DMX_DEV_COUNT; j++) {
		if (! dvb->dmx[j].init)
			continue;
		record_enable = 0;
		for (i=0; i<ASYNCFIFO_COUNT; i++) {
			if (! dvb->asyncfifo[i].init)
				continue;

			if (dvb->dmx[j].record && dvb->dmx[j].id == dvb->asyncfifo[i].source) {
				/*This dmx is linked to the async fifo, Enable the TS_RECORDER_ENABLE*/
				record_enable = 1;
				if (! low_dmx_fifo) {
					low_dmx_fifo = &dvb->asyncfifo[i];
				} else if (low_dmx_fifo->source > dvb->asyncfifo[i].source) {
					high_dmx_fifo = low_dmx_fifo;
					low_dmx_fifo = &dvb->asyncfifo[i];
				} else if (low_dmx_fifo->source < dvb->asyncfifo[i].source) {
					high_dmx_fifo = &dvb->asyncfifo[i];
				}

				break;
			}
		}
		pr_dbg("Set DMX%d TS_RECORDER_ENABLE to %d\n", dvb->dmx[j].id, record_enable ? 1 : 0);
		if (record_enable) {
			//DMX_SET_REG_MASK(dvb->dmx[j].id, DEMUX_CONTROL, 1<<TS_RECORDER_ENABLE);
			DMX_WRITE_REG(dvb->dmx[j].id, DEMUX_CONTROL, DMX_READ_REG(dvb->dmx[j].id, DEMUX_CONTROL)|(1<<TS_RECORDER_ENABLE));
		} else {
			//DMX_CLEAR_REG_MASK(dvb->dmx[j].id, DEMUX_CONTROL, 1<<TS_RECORDER_ENABLE);
			DMX_WRITE_REG(dvb->dmx[j].id, DEMUX_CONTROL, DMX_READ_REG(dvb->dmx[j].id, DEMUX_CONTROL)&(~(1<<TS_RECORDER_ENABLE)));
		}
	}

	/*Set the async fifo regs*/
	if (low_dmx_fifo) {
		async_fifo_set_regs(low_dmx_fifo, 0x3);

		if (high_dmx_fifo) {
			async_fifo_set_regs(high_dmx_fifo, 0x2);
		}
	}
}

/*Reset the demux device*/
void dmx_reset_hw(struct aml_dvb *dvb)
{
	dmx_reset_hw_ex(dvb, 1);
}

/*Reset the demux device*/
void dmx_reset_hw_ex(struct aml_dvb *dvb, int reset_irq)
{
	int id, times;

	pr_dbg("demux reset begin\n");

	for (id=0; id<DMX_DEV_COUNT; id++) {
		if(!dvb->dmx[id].init)
			continue;
		if(reset_irq){
			if(dvb->dmx[id].dmx_irq!=-1) {
				disable_irq(dvb->dmx[id].dmx_irq);
			}
			if(dvb->dmx[id].dvr_irq!=-1) {
				disable_irq(dvb->dmx[id].dvr_irq);
			}
		}
	}
#ifdef ENABLE_SEC_BUFF_WATCHDOG
	if(reset_irq){
		del_timer_sync(&dvb->watchdog_timer);
	}
#endif

	WRITE_MPEG_REG(RESET1_REGISTER, RESET_DEMUXSTB);

	for (id=0; id<DMX_DEV_COUNT; id++) {
		times = 0;
		while(times++ < 1000000) {
			if (!(DMX_READ_REG(id, OM_CMD_STATUS)&0x01))
				break;
		}
	}

	WRITE_MPEG_REG(STB_TOP_CONFIG, 0);

	for (id=0; id<DMX_DEV_COUNT; id++) {
		u32 version, data;

		if(!dvb->dmx[id].init)
			continue;

		if(reset_irq){
			if(dvb->dmx[id].dmx_irq!=-1) {
				enable_irq(dvb->dmx[id].dmx_irq);
			}
			if(dvb->dmx[id].dvr_irq!=-1) {
				enable_irq(dvb->dmx[id].dvr_irq);
			}
		}
		DMX_WRITE_REG(id, DEMUX_CONTROL, 0x0000);
		version = DMX_READ_REG(id, STB_VERSION);
		DMX_WRITE_REG(id, STB_TEST_REG, version);
		pr_dbg("STB %d hardware version : %d\n", id, version);
		DMX_WRITE_REG(id, STB_TEST_REG, 0x5550);
		data = DMX_READ_REG(id, STB_TEST_REG);
		if(data!=0x5550)
			pr_error("STB %d register access failed\n", id);
		DMX_WRITE_REG(id, STB_TEST_REG, 0xaaa0);
		data = DMX_READ_REG(id, STB_TEST_REG);
		if(data!=0xaaa0)
			pr_error("STB %d register access failed\n", id);
		DMX_WRITE_REG(id, MAX_FM_COMP_ADDR, 0x0000);
		DMX_WRITE_REG(id, STB_INT_MASK, 0);
		DMX_WRITE_REG(id, STB_INT_STATUS, 0xffff);
		DMX_WRITE_REG(id, FEC_INPUT_CONTROL, 0);
	}

	stb_enable(dvb);

	for(id=0; id<DMX_DEV_COUNT; id++)
	{
		struct aml_dmx *dmx = &dvb->dmx[id];
		int n;
		unsigned long addr;
		unsigned long base;
		unsigned long grp_addr[SEC_BUF_GRP_COUNT];
		int grp_len[SEC_BUF_GRP_COUNT];
		if(!dvb->dmx[id].init)
			continue;


		if(dmx->sec_pages) {
			grp_len[0] = (1<<SEC_GRP_LEN_0)*8;
			grp_len[1] = (1<<SEC_GRP_LEN_1)*8;
			grp_len[2] = (1<<SEC_GRP_LEN_2)*8;
			grp_len[3] = (1<<SEC_GRP_LEN_3)*8;


			grp_addr[0] = virt_to_phys((void*)dmx->sec_pages);
			grp_addr[1] = grp_addr[0]+grp_len[0];
			grp_addr[2] = grp_addr[1]+grp_len[1];
			grp_addr[3] = grp_addr[2]+grp_len[2];

			base = grp_addr[0]&0xFFFF0000;
			DMX_WRITE_REG(dmx->id, SEC_BUFF_BASE, base>>16);
			DMX_WRITE_REG(dmx->id, SEC_BUFF_01_START, (((grp_addr[0]-base)>>8)<<16)|((grp_addr[1]-base)>>8));
			DMX_WRITE_REG(dmx->id, SEC_BUFF_23_START, (((grp_addr[2]-base)>>8)<<16)|((grp_addr[3]-base)>>8));
			DMX_WRITE_REG(dmx->id, SEC_BUFF_SIZE, SEC_GRP_LEN_0|
				(SEC_GRP_LEN_1<<4)|
				(SEC_GRP_LEN_2<<8)|
				(SEC_GRP_LEN_3<<12));
		}

		if(dmx->sub_pages) {
			addr = virt_to_phys((void*)dmx->sub_pages);
			DMX_WRITE_REG(dmx->id, SB_START, addr>>12);
			DMX_WRITE_REG(dmx->id, SB_LAST_ADDR, (dmx->sub_buf_len>>3)-1);
		}

		if(dmx->pes_pages) {
			addr = virt_to_phys((void*)dmx->pes_pages);
			DMX_WRITE_REG(dmx->id, OB_START, addr>>12);
			DMX_WRITE_REG(dmx->id, OB_LAST_ADDR, (dmx->pes_buf_len>>3)-1);
		}

		for(n=0; n<CHANNEL_COUNT; n++)
		{
		//	struct aml_channel *chan = &dmx->channel[n];
	
			/*if(chan->used)*/
			{
				dmx_set_chan_regs(dmx, n);
			}
		}

		for(n=0; n<FILTER_COUNT; n++)
		{
			struct aml_filter *filter = &dmx->filter[n];

			if(filter->used)
			{
				dmx_set_filter_regs(dmx, n);
			}
		}

		dmx_enable(&dvb->dmx[id]);
	}

	for(id=0; id<DSC_COUNT; id++)
	{
		struct aml_dsc *dsc = &dvb->dsc[id];

		//if(dsc->used)
		{
			dsc_set_pid(dsc, dsc->pid);

			if(dsc->set&1)
				dsc_set_key(dsc, 0, dsc->even);
			if(dsc->set&2)
				dsc_set_key(dsc, 1, dsc->odd);
		}
	}
#ifdef ENABLE_SEC_BUFF_WATCHDOG
	if(reset_irq){
		mod_timer(&dvb->watchdog_timer, jiffies+msecs_to_jiffies(WATCHDOG_TIMER));
	}
#endif

	pr_dbg("demux reset end\n");
}

/*Reset the individual demux*/
void dmx_reset_dmx_hw_ex_unlock(struct aml_dvb *dvb, struct aml_dmx *dmx, int reset_irq)
{
	{
		if(!dmx->init)
			return;
		if(reset_irq){
			if(dmx->dmx_irq!=-1) {
				disable_irq(dmx->dmx_irq);
			}
			if(dmx->dvr_irq!=-1) {
				disable_irq(dmx->dvr_irq);
			}
		}
	}
#ifdef ENABLE_SEC_BUFF_WATCHDOG
	if(reset_irq){
		//del_timer_sync(&dvb->watchdog_timer);
		dvb->dmx_watchdog_disable[dmx->id] = 1;
	}
#endif

	WRITE_MPEG_REG(RESET3_REGISTER, (dmx->id)? ((dmx->id==1)? RESET_DEMUX1 : RESET_DEMUX2) : RESET_DEMUX0);
	WRITE_MPEG_REG(RESET3_REGISTER, RESET_DES);

	{
		int times;

		times = 0;
		while(times++ < 1000000) {
			if (!(DMX_READ_REG(dmx->id, OM_CMD_STATUS)&0x01))
				break;
		}
	}

	//WRITE_MPEG_REG(STB_TOP_CONFIG, 0);

	{
		u32 version, data;

		if(!dmx->init)
			return ;

		if(reset_irq){
			if(dmx->dmx_irq!=-1) {
				enable_irq(dmx->dmx_irq);
			}
			if(dmx->dvr_irq!=-1) {
				enable_irq(dmx->dvr_irq);
			}
		}
		DMX_WRITE_REG(dmx->id, DEMUX_CONTROL, 0x0000);
		version = DMX_READ_REG(dmx->id, STB_VERSION);
		DMX_WRITE_REG(dmx->id, STB_TEST_REG, version);
		pr_dbg("STB %d hardware version : %d\n", dmx->id, version);
		DMX_WRITE_REG(dmx->id, STB_TEST_REG, 0x5550);
		data = DMX_READ_REG(dmx->id, STB_TEST_REG);
		if(data!=0x5550)
			pr_error("STB %d register access failed\n", dmx->id);
		DMX_WRITE_REG(dmx->id, STB_TEST_REG, 0xaaa0);
		data = DMX_READ_REG(dmx->id, STB_TEST_REG);
		if(data!=0xaaa0)
			pr_error("STB %d register access failed\n", dmx->id);
		DMX_WRITE_REG(dmx->id, MAX_FM_COMP_ADDR, 0x0000);
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, 0);
		DMX_WRITE_REG(dmx->id, STB_INT_STATUS, 0xffff);
		DMX_WRITE_REG(dmx->id, FEC_INPUT_CONTROL, 0);
	}

	stb_enable(dvb);

	{
		int n;
		unsigned long addr;
		unsigned long base;
		unsigned long grp_addr[SEC_BUF_GRP_COUNT];
		int grp_len[SEC_BUF_GRP_COUNT];

		if(!dmx->init)
			return ;

		if(dmx->sec_pages) {
			grp_len[0] = (1<<SEC_GRP_LEN_0)*8;
			grp_len[1] = (1<<SEC_GRP_LEN_1)*8;
			grp_len[2] = (1<<SEC_GRP_LEN_2)*8;
			grp_len[3] = (1<<SEC_GRP_LEN_3)*8;

			grp_addr[0] = virt_to_phys((void*)dmx->sec_pages);
			grp_addr[1] = grp_addr[0]+grp_len[0];
			grp_addr[2] = grp_addr[1]+grp_len[1];
			grp_addr[3] = grp_addr[2]+grp_len[2];

			base = grp_addr[0]&0xFFFF0000;
			DMX_WRITE_REG(dmx->id, SEC_BUFF_BASE, base>>16);
			DMX_WRITE_REG(dmx->id, SEC_BUFF_01_START, (((grp_addr[0]-base)>>8)<<16)|((grp_addr[1]-base)>>8));
			DMX_WRITE_REG(dmx->id, SEC_BUFF_23_START, (((grp_addr[2]-base)>>8)<<16)|((grp_addr[3]-base)>>8));
			DMX_WRITE_REG(dmx->id, SEC_BUFF_SIZE, SEC_GRP_LEN_0|
				(SEC_GRP_LEN_1<<4)|
				(SEC_GRP_LEN_2<<8)|
				(SEC_GRP_LEN_3<<12));
		}

		if(dmx->sub_pages) {
			addr = virt_to_phys((void*)dmx->sub_pages);
			DMX_WRITE_REG(dmx->id, SB_START, addr>>12);
			DMX_WRITE_REG(dmx->id, SB_LAST_ADDR, (dmx->sub_buf_len>>3)-1);
		}

		if(dmx->pes_pages) {
			addr = virt_to_phys((void*)dmx->pes_pages);
			DMX_WRITE_REG(dmx->id, OB_START, addr>>12);
			DMX_WRITE_REG(dmx->id, OB_LAST_ADDR, (dmx->pes_buf_len>>3)-1);
		}

		for(n=0; n<CHANNEL_COUNT; n++)
		{
			//struct aml_channel *chan = &dmx->channel[n];

			//if(chan->used)
			{
				dmx_set_chan_regs(dmx, n);
			}
		}

		for(n=0; n<FILTER_COUNT; n++)
		{
			struct aml_filter *filter = &dmx->filter[n];

			if(filter->used)
			{
				dmx_set_filter_regs(dmx, n);
			}
		}

		dmx_enable(dmx);
	}

	{
		int id;
		for(id=0; id<DSC_COUNT; id++)
		{
			struct aml_dsc *dsc = &dvb->dsc[id];

			//if(dsc->used)
			{
				dsc_set_pid(dsc, dsc->pid);

				if(dsc->set&1)
					dsc_set_key(dsc, 0, dsc->even);
				if(dsc->set&2)
					dsc_set_key(dsc, 1, dsc->odd);
			}
		}
	}
#ifdef ENABLE_SEC_BUFF_WATCHDOG
	if(reset_irq){
		//mod_timer(&dvb->watchdog_timer, jiffies+msecs_to_jiffies(WATCHDOG_TIMER));
		dvb->dmx_watchdog_disable[dmx->id] = 0;
	}
#endif
}

void dmx_reset_dmx_id_hw_ex_unlock(struct aml_dvb *dvb, int id, int reset_irq)
{
	dmx_reset_dmx_hw_ex_unlock(dvb, &dvb->dmx[id], reset_irq);
}

void dmx_reset_dmx_hw_ex(struct aml_dvb *dvb, struct aml_dmx *dmx, int reset_irq)
{
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);
	dmx_reset_dmx_hw_ex_unlock(dvb, dmx, reset_irq);
	spin_unlock_irqrestore(&dvb->slock, flags);
}

void dmx_reset_dmx_id_hw_ex(struct aml_dvb *dvb, int id, int reset_irq)
{
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);
	dmx_reset_dmx_id_hw_ex_unlock(dvb, id, reset_irq);
	spin_unlock_irqrestore(&dvb->slock, flags);
}

void dmx_reset_dmx_hw(struct aml_dvb *dvb, int id)
{
	dmx_reset_dmx_id_hw_ex(dvb, id, 1);
}
/*Allocate subtitle pes buffer*/
static int alloc_subtitle_pes_buffer(struct aml_dmx * dmx)
{
	int start_ptr=0;
	stream_buf_t *sbuff=0;
	 u32 phy_addr;
	start_ptr=READ_MPEG_REG(PARSER_SUB_START_PTR);
	if(start_ptr)
	{
		WRITE_MPEG_REG(PARSER_SUB_RP, start_ptr);
		goto exit;
	}
	sbuff=get_stream_buffer(BUF_TYPE_SUBTITLE);
	if(sbuff)
	{
		if (sbuff->flag & BUF_FLAG_IOMEM)
			phy_addr = sbuff->buf_start;
		else
			phy_addr = virt_to_phys((void *)sbuff->buf_start);

		WRITE_MPEG_REG(PARSER_SUB_RP, phy_addr);
		WRITE_MPEG_REG(PARSER_SUB_START_PTR, phy_addr);
		WRITE_MPEG_REG(PARSER_SUB_END_PTR, phy_addr + sbuff->buf_size - 8);

		pr_dbg("pes buff=:%x %x\n",phy_addr,sbuff->buf_size);
	}
	else
	{
		pr_dbg("Error stream buffer\n");
	}
exit:
	return 0;
}


/*Allocate a new channel*/
int dmx_alloc_chan(struct aml_dmx *dmx, int type, int pes_type, int pid)
{
	int id = -1;
	int ret;

	if(type==DMX_TYPE_TS) {
		switch(pes_type) {
			case DMX_PES_VIDEO:
				if(!dmx->channel[0].used)
					id = 0;
			break;
			case DMX_PES_AUDIO:
				if(!dmx->channel[1].used)
					id = 1;
			break;
			case DMX_PES_SUBTITLE:
			case DMX_PES_TELETEXT:
				if(!dmx->channel[2].used)
					id = 2;
				alloc_subtitle_pes_buffer(dmx);
			break;
			case DMX_PES_PCR:
				if(!dmx->channel[3].used)
					id = 3;
			break;
			case DMX_PES_OTHER:
			{
				int i;
				for(i=SYS_CHAN_COUNT; i<CHANNEL_COUNT; i++) {
					if(!dmx->channel[i].used) {
						id = i;
						break;
					}
				}
			}
			break;
			default:
			break;
		}
	} else {
		int i;
		for(i=SYS_CHAN_COUNT; i<CHANNEL_COUNT; i++) {
			if(!dmx->channel[i].used) {
				id = i;
				break;
			}
		}
	}

	if(id==-1) {
		pr_error("too many channels\n");
		return -1;
	}

	pr_dbg("allocate channel(id:%d PID:0x%x)\n", id, pid);

	if(id <= 3) {
		if ((ret=dmx_get_chan(dmx, pid))>=0 && DVR_FEED(dmx->channel[ret].feed)) {
			dmx_remove_feed(dmx, dmx->channel[ret].feed);
			dmx->channel[id].dvr_feed = dmx->channel[ret].feed;
			dmx->channel[id].dvr_feed->priv = (void*)id;
		} else {
			dmx->channel[id].dvr_feed = NULL;
		}
	}

	dmx->channel[id].type = type;
	dmx->channel[id].pes_type = pes_type;
	dmx->channel[id].pid  = pid;
	dmx->channel[id].used = 1;
	dmx->channel[id].filter_count = 0;

	dmx_set_chan_regs(dmx, id);

	set_debug_dmx_chanpids(dmx->id, id, pid);

	dmx->chan_count++;

	dmx_enable(dmx);

	return id;
}

/*Free a channel*/
void dmx_free_chan(struct aml_dmx *dmx, int cid)
{
	pr_dbg("free channel(id:%d PID:0x%x)\n", cid, dmx->channel[cid].pid);

	dmx->channel[cid].used = 0;
	dmx_set_chan_regs(dmx, cid);

	if(cid==2){
		u32 parser_sub_start_ptr;

		parser_sub_start_ptr = READ_MPEG_REG(PARSER_SUB_START_PTR);
		WRITE_MPEG_REG(PARSER_SUB_RP, parser_sub_start_ptr);
		WRITE_MPEG_REG(PARSER_SUB_WP, parser_sub_start_ptr);
	}

	set_debug_dmx_chanpids(dmx->id, cid, -1);
	dmx->chan_count--;

	dmx_enable(dmx);

	/*Special pes type channel, check its dvr feed*/
	if (cid <= 3 && dmx->channel[cid].dvr_feed) {
		/*start the dvr feed*/
		dmx_add_feed(dmx, dmx->channel[cid].dvr_feed);
	}
}

/*Add a section*/
static int dmx_chan_add_filter(struct aml_dmx *dmx, int cid, struct dvb_demux_filter *filter)
{
	int id = -1;
	int i;

	for(i=0; i<FILTER_COUNT; i++) {
		if(!dmx->filter[i].used) {
			id = i;
			break;
		}
	}

	if(id==-1) {
		pr_error("too many filters\n");
		return -1;
	}

	pr_dbg("channel(id:%d PID:0x%x) add filter(id:%d)\n", cid, filter->feed->pid, id);

	dmx->filter[id].chan_id = cid;
	dmx->filter[id].used = 1;
	dmx->filter[id].filter = (struct dmx_section_filter*)filter;
	dmx->channel[cid].filter_count++;

	dmx_set_filter_regs(dmx, id);

	return id;
}

static void dmx_remove_filter(struct aml_dmx *dmx, int cid, int fid)
{
	pr_dbg("channel(id:%d PID:0x%x) remove filter(id:%d)\n", cid, dmx->channel[cid].pid, fid);

	dmx->filter[fid].used = 0;
	dmx->channel[cid].filter_count--;

	dmx_set_filter_regs(dmx, fid);
	dmx_clear_filter_buffer(dmx, fid);
}

static int sf_add_feed(struct aml_dmx *src_dmx, struct dvb_demux_feed *feed)
{
	int ret;

	struct aml_dvb *dvb = (struct aml_dvb*)src_dmx->demux.priv;
	struct aml_swfilter *sf = &dvb->swfilter;

	pr_dbg_sf("sf add pid[%d]\n", feed->pid);

	/*init sf*/
	if (!sf->user) {
		void *mem;
		mem = vmalloc(SF_BUFFER_SIZE);
		if (!mem) {
			return -ENOMEM;
		}
		dvb_ringbuffer_init(&sf->rbuf, mem, SF_BUFFER_SIZE);

		sf->dmx = &dvb->dmx[SF_DMX_ID];
		sf->afifo = &dvb->asyncfifo[SF_AFIFO_ID];

		sf->dmx->source = src_dmx->source;
		sf->afifo->source = sf->dmx->id;
		sf->track_dmx = src_dmx->id;
		//sf->afifo->flush_size = 188*10;

		pr_dbg_sf("init sf mode.\n");

	} else if (sf->dmx->source != src_dmx->source) {
		pr_error(" pid=%d[src:%d] already used with sfdmx%d[src:%d]\n",
			feed->pid, src_dmx->source, sf->dmx->id, sf->dmx->source);
		return -EBUSY;
	}

	/*setup feed*/
	if ((ret=dmx_get_chan(sf->dmx, feed->pid)) >= 0) {
		pr_error(" pid=%d[dmx:%d] already used [dmx:%d].\n",
			feed->pid, src_dmx->id,
			((struct aml_dmx*)sf->dmx->channel[ret].feed->demux)->id);
		return -EBUSY;
	}
	if ((ret=dmx_alloc_chan(sf->dmx, DMX_TYPE_TS, DMX_PES_OTHER, feed->pid))<0) {
		pr_error(" %s: alloc chan error, ret=%d\n", __func__, ret);
		return ret;
	}
	sf->dmx->channel[ret].feed = feed;
	feed->priv = (void*)ret;

	sf->dmx->channel[ret].dvr_feed = feed;

	sf->user++;
	debug_sf_user = sf->user;

	dmx_enable(sf->dmx);

	return 0;
}

static int sf_remove_feed(struct aml_dmx *src_dmx, struct dvb_demux_feed *feed)
{
	int ret;

	struct aml_dvb *dvb = (struct aml_dvb*)src_dmx->demux.priv;
	struct aml_swfilter *sf = &dvb->swfilter;

	if (!sf->user || (sf->dmx->source != src_dmx->source)) {
		return 0;
	}

	if ((ret=dmx_get_chan(sf->dmx, feed->pid))<0) {
		return 0;
	}

	pr_dbg_sf("sf remove pid[%d]\n", feed->pid);

	dmx_free_chan(sf->dmx, (int)feed->priv);

	sf->dmx->channel[ret].feed = NULL;
	sf->dmx->channel[ret].dvr_feed = NULL;

	sf->user--;
	debug_sf_user = sf->user;

	if (!sf->user) {
		sf->dmx->source = -1;
		sf->afifo->source = AM_DMX_MAX;
		sf->track_dmx = -1;
		//sf->afifo->flush_size = sf->afifo->buf_len>>1;

		if (sf->rbuf.data) {
			void *mem = sf->rbuf.data;
			sf->rbuf.data=NULL;
			vfree(mem);
		}
		pr_dbg_sf("exit sf mode.\n");
	}

	return 0;
}

static int sf_feed_sf(struct aml_dmx *dmx, struct dvb_demux_feed *feed, int add_not_remove)
{
	int sf=0;

	if (sf_dmx_sf(dmx)) {
		pr_error("%s: demux %d is in sf mode\n", __func__, dmx->id);
		return -EINVAL;
	}

	switch (feed->type)
	{
		case DMX_TYPE_TS: {
			struct dmxdev_filter *dmxdevfilter = feed->feed.ts.priv;
			if (!DVR_FEED(feed)) {
				if (dmxdevfilter->params.pes.flags & DMX_USE_SWFILTER)
					sf = 1;
				if (force_pes_sf)
					sf = 1;
			}
		}break;

		case DMX_TYPE_SEC: {
			struct dvb_demux_filter *filter;
			for (filter=feed->filter; filter; filter=filter->next) {
				struct dmxdev_filter *dmxdevfilter = filter->filter.priv;
				if (dmxdevfilter->params.sec.flags & DMX_USE_SWFILTER)
					sf = 1;
				if (add_not_remove)
					filter->hw_handle = (u16)-1;
			}
			if (force_sec_sf)
				sf = 1;
		}break;
	}

	return sf? 0 : 1;
}

static int sf_check_feed(struct aml_dmx *dmx, struct dvb_demux_feed *feed, int add_not_remove)
{
	int ret=0;

	ret = sf_feed_sf(dmx, feed, add_not_remove);
	if (ret)
		return ret;

	pr_dbg_sf("%s [pid:%d] %s\n",
		(feed->type==DMX_TYPE_TS)? "DMX_TYPE_TS" : "DMX_TYPE_SEC",
		feed->pid,
		add_not_remove? "-> sf mode":"sf mode ->");

	if (add_not_remove)
		ret = sf_add_feed(dmx, feed);
	else
		ret = sf_remove_feed(dmx, feed);

	if (ret<0) {
		pr_error("sf %s feed fail[%d]\n", add_not_remove? "add" : "remove", ret);
	}
	return ret;
}

static int dmx_add_feed(struct aml_dmx *dmx, struct dvb_demux_feed *feed)
{
	int id, ret=0;
	struct dvb_demux_filter *filter;
	struct dvb_demux_feed *dfeed = NULL;
	int sf_ret = 0;/*<0:error, =0:sf_on, >0:sf_off*/

	sf_ret = sf_check_feed(dmx, feed, 1/*SF_FEED_OP_ADD*/);
	if (sf_ret<0)
		return sf_ret;

	switch(feed->type)
	{
		case DMX_TYPE_TS:
			pr_dbg("%s: DMX_TYPE_TS\n", __func__);
			if((ret=dmx_get_chan(dmx, feed->pid))>=0) {
				if (DVR_FEED(dmx->channel[ret].feed)){
					if (DVR_FEED(feed)){
						/*dvr feed already work*/
						pr_error("PID %d already used(DVR)\n", feed->pid);
						return -EBUSY;
					}
					if (sf_ret) {
						/*if sf_on, we do not reset the previous dvr feed,
						   just load the pes feed on the sf, a diffrent data path.*/
						dfeed = dmx->channel[ret].feed;
						dmx_remove_feed(dmx, dfeed);
					}
				} else {
					if (DVR_FEED(feed) && (! dmx->channel[ret].dvr_feed)){
						/*just store the dvr_feed*/
						dmx->channel[ret].dvr_feed = feed;
						feed->priv = (void*)ret;
						if (! dmx->record){
							dmx_enable(dmx);
						}
						return 0;
					}else{
						pr_error("PID %d already used\n", feed->pid);
						return -EBUSY;
					}
				}
			}

			if (sf_ret) {/*not sf feed.*/
				if ((ret=dmx_alloc_chan(dmx, feed->type, feed->pes_type, feed->pid))<0) {
					pr_dbg("%s: alloc chan error, ret=%d\n", __func__, ret);
					return ret;
				}
				dmx->channel[ret].feed = feed;
				feed->priv = (void*)ret;
				dmx->channel[ret].dvr_feed = NULL;
			}
			/*dvr*/
			if (DVR_FEED(feed)){
				dmx->channel[ret].dvr_feed = feed;
				feed->priv = (void*)ret;
				if (! dmx->record){
					dmx_enable(dmx);
				}
			}else if (dfeed && sf_ret){
				dmx->channel[ret].dvr_feed = dfeed;
				dfeed->priv = (void*)ret;
				if (! dmx->record){
					dmx_enable(dmx);
				}
			}

		break;
		case DMX_TYPE_SEC:
			pr_dbg("%s: DMX_TYPE_SEC\n", __func__);

			if((ret=dmx_get_chan(dmx, feed->pid))>=0) {
				if (DVR_FEED(dmx->channel[ret].feed)){
					if (sf_ret) {
						/*if sf_on, we do not reset the previous dvr feed,
						just load the pes feed on the sf, a diffrent data path.*/
						dfeed = dmx->channel[ret].feed;
						dmx_remove_feed(dmx, dfeed);
					}
				} else {
					pr_error("PID %d already used\n", feed->pid);
					return -EBUSY;
				}
			}
			if (sf_ret) {/*not sf feed.*/
				if ((id=dmx_alloc_chan(dmx, feed->type, feed->pes_type, feed->pid))<0) {
					return id;
				}
				for (filter=feed->filter; filter; filter=filter->next) {
					if ((ret=dmx_chan_add_filter(dmx, id, filter)) >= 0) {
						filter->hw_handle = ret;
					} else {
						filter->hw_handle = (u16)-1;
					}
				}
				dmx->channel[id].feed = feed;
				feed->priv = (void*)id;
				dmx->channel[id].dvr_feed = NULL;

				if (dfeed) {
					dmx->channel[id].dvr_feed = dfeed;
					dfeed->priv = (void*)id;
					if (! dmx->record) {
						dmx_enable(dmx);
					}
				}
			}
		break;
		default:
			return -EINVAL;
		break;
	}

	dmx->feed_count++;

	return 0;
}

static int dmx_remove_feed(struct aml_dmx *dmx, struct dvb_demux_feed *feed)
{
	struct dvb_demux_filter *filter;
	struct dvb_demux_feed *dfeed = NULL;

	int sf_ret = 0;/*<0:error, =0:sf_on, >0:sf_off*/

	sf_ret = sf_check_feed(dmx, feed, 0/*SF_FEED_OP_RM*/);
	if (sf_ret <= 0)
		return sf_ret;

	switch(feed->type)
	{
		case DMX_TYPE_TS:
			if (dmx->channel[(int)feed->priv].feed == dmx->channel[(int)feed->priv].dvr_feed){
				dmx_free_chan(dmx, (int)feed->priv);
			} else {
				if (feed == dmx->channel[(int)feed->priv].feed) {
					dfeed = dmx->channel[(int)feed->priv].dvr_feed;
					dmx_free_chan(dmx, (int)feed->priv);
					if (dfeed) {
						/*start the dvr feed*/
						dmx_add_feed(dmx, dfeed);
					}
				} else if (feed == dmx->channel[(int)feed->priv].dvr_feed) {
					/*just remove the dvr_feed*/
					dmx->channel[(int)feed->priv].dvr_feed = NULL;
					if (dmx->record) {
						dmx_enable(dmx);
					}
				} else {
					/*This must never happen*/
					pr_error("%s: unknown feed\n", __func__);
					return -EINVAL;
				}
			}

		break;
		case DMX_TYPE_SEC:
			for(filter=feed->filter; filter; filter=filter->next) {
				if(filter->hw_handle!=(u16)-1)
					dmx_remove_filter(dmx, (int)feed->priv, (int)filter->hw_handle);
			}

			dfeed = dmx->channel[(int)feed->priv].dvr_feed;
			dmx_free_chan(dmx, (int)feed->priv);
			if (dfeed) {
				/*start the dvr feed*/
				dmx_add_feed(dmx, dfeed);
			}
		break;
		default:
			return -EINVAL;
		break;
	}

	dmx->feed_count--;
	return 0;
}

int aml_dmx_hw_init(struct aml_dmx *dmx)
{
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	unsigned long flags;
	int ret;

	/*Demux initialize*/
	spin_lock_irqsave(&dvb->slock, flags);
	ret = dmx_init(dmx);
	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_dmx_hw_deinit(struct aml_dmx *dmx)
{
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&dvb->slock, flags);
	ret = dmx_deinit(dmx);
	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_asyncfifo_hw_init(struct aml_asyncfifo *afifo)
{
	struct aml_dvb *dvb = afifo->dvb;
	unsigned long flags;
	int ret;

	int len = ASYNCFIFO_BUFFER_SIZE_DEFAULT;
	unsigned long buf = asyncfifo_alloc_buffer(len);
	if (!buf)
		return -1;

	/*Async FIFO initialize*/
	spin_lock_irqsave(&dvb->slock, flags);
	
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	CLK_GATE_ON(ASYNC_FIFO);
#endif

	ret = async_fifo_init(afifo, 1, len, buf);

	spin_unlock_irqrestore(&dvb->slock, flags);

	if (ret<0)
		asyncfifo_free_buffer(buf, len);

	return ret;
}

int aml_asyncfifo_hw_deinit(struct aml_asyncfifo *afifo)
{
	struct aml_dvb *dvb = afifo->dvb;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dvb->slock, flags);
	ret = async_fifo_deinit(afifo, 1);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	CLK_GATE_OFF(ASYNC_FIFO);
#endif

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_asyncfifo_hw_reset(struct aml_asyncfifo *afifo)
{
	struct aml_dvb *dvb = afifo->dvb;
	unsigned long flags;
	int ret, src = -1;

	int len = ASYNCFIFO_BUFFER_SIZE_DEFAULT;
	unsigned long buf = asyncfifo_alloc_buffer(len);
	if (!buf)
		return -1;

	spin_lock_irqsave(&dvb->slock, flags);
	if (afifo->init) {
		src = afifo->source;
		async_fifo_deinit(afifo, 0);
	}
	ret = async_fifo_init(afifo, 0, len, buf);
	/* restore the source */
	if (src != -1) {
		afifo->source = src;
	}
	if ((ret == 0) && afifo->dvb) {
		reset_async_fifos(afifo->dvb);
	}
	spin_unlock_irqrestore(&dvb->slock, flags);

	if (ret<0)
		asyncfifo_free_buffer(buf, len);

	return ret;
}

int aml_dmx_hw_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct aml_dmx *dmx = (struct aml_dmx*)dvbdmxfeed->demux;
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dvb->slock, flags);
	ret = dmx_add_feed(dmx, dvbdmxfeed);
	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_dmx_hw_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct aml_dmx *dmx = (struct aml_dmx*)dvbdmxfeed->demux;
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);
	dmx_remove_feed(dmx, dvbdmxfeed);
	spin_unlock_irqrestore(&dvb->slock, flags);

	return 0;
}

int sf_dmx_track_source(struct aml_dmx *dmx)
{
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	struct aml_swfilter *sf = &dvb->swfilter;
	if (sf->user && (dmx->id == sf->track_dmx)) {
		pr_dbg_sf("tracking dmx src [%d -> %d]\n",
					sf->dmx->source, dmx->source);
		sf->dmx->source = dmx->source;
		dmx_reset_dmx_hw_ex_unlock(dvb, sf->dmx, 0);
	}
	return 0;
}

int aml_dmx_hw_set_source(struct dmx_demux* demux, dmx_source_t src)
{
	struct aml_dmx *dmx = (struct aml_dmx*)demux;
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	int ret = 0;
	int hw_src;
	unsigned long flags;

	if (sf_dmx_sf(dmx)) {
		pr_error("%s: demux %d is in sf mode\n", __func__, dmx->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&dvb->slock, flags);

	hw_src = dmx->source;

	switch(src) {
		case DMX_SOURCE_FRONT0:
			hw_src = (dvb->ts[0].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS0 : AM_TS_SRC_TS0;
		break;
		case DMX_SOURCE_FRONT1:
			hw_src = (dvb->ts[1].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS1 : AM_TS_SRC_TS1;
		break;
		case DMX_SOURCE_FRONT2:
			hw_src = (dvb->ts[2].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS2 : AM_TS_SRC_TS2;
		break;
		case DMX_SOURCE_DVR0:
			hw_src = AM_TS_SRC_HIU;
		break;
		default:
			pr_error("illegal demux source %d\n", src);
			ret = -EINVAL;
		break;
	}

	if (hw_src != dmx->source) {
		dmx->source = hw_src;
		dmx_reset_dmx_hw_ex_unlock(dvb, dmx, 0);
		sf_dmx_track_source(dmx);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

#define IS_SRC_DMX(_src) ((_src)>=AM_TS_SRC_DMX0 &&( _src)<=AM_TS_SRC_DMX2) 

int aml_stb_hw_set_source(struct aml_dvb *dvb, dmx_source_t src)
{
	unsigned long flags;
	int hw_src;
	int ret;
	ret = 0;
	spin_lock_irqsave(&dvb->slock, flags);

	hw_src = dvb->stb_source;

	switch(src) {
		case DMX_SOURCE_FRONT0:
			hw_src = (dvb->ts[0].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS0 : AM_TS_SRC_TS0;
		break;
		case DMX_SOURCE_FRONT1:
			hw_src = (dvb->ts[1].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS1 : AM_TS_SRC_TS1;
		break;
		case DMX_SOURCE_FRONT2:
			hw_src = (dvb->ts[2].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS2 : AM_TS_SRC_TS2;
		break;
		case DMX_SOURCE_DVR0:
			hw_src = AM_TS_SRC_HIU;
		break;
		case DMX_SOURCE_FRONT0_OFFSET:
			hw_src = AM_TS_SRC_DMX0;
		break;
		case DMX_SOURCE_FRONT1_OFFSET:
			hw_src = AM_TS_SRC_DMX1;
		break;
		case DMX_SOURCE_FRONT2_OFFSET:
			hw_src = AM_TS_SRC_DMX2;
		break;
		default:
			pr_error("illegal demux source %d\n", src);
			ret = -EINVAL;
		break;
	}

	if(dvb->stb_source != hw_src) {
		int old_source = dvb->stb_source;
		dvb->stb_source = hw_src;

		if(IS_SRC_DMX(old_source)) {
			dmx_set_misc_id(dvb,(old_source-AM_TS_SRC_DMX0), 0, -1);
		} else {
			/*which dmx for av-play is unknown, can't avoid reset-all*/
			dmx_reset_hw_ex(dvb, 0);
		}

		if(IS_SRC_DMX(dvb->stb_source)) {
			dmx_set_misc_id(dvb,(dvb->stb_source-AM_TS_SRC_DMX0), 1, -1);
			//dmx_reset_dmx_id_hw_ex_unlock(dvb, (dvb->stb_source-AM_TS_SRC_DMX0), 0);
		} else {
			/*which dmx for av-play is unknown, can't avoid reset-all*/
			dmx_reset_hw_ex(dvb, 0);
		}
	}

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_dsc_hw_set_source(struct aml_dvb *dvb, dmx_source_t src)
{
	int ret = 0;
	unsigned long flags;
	int hw_src;

	spin_lock_irqsave(&dvb->slock, flags);

	hw_src = dvb->dsc_source;

	switch(src) {
		case DMX_SOURCE_FRONT0_OFFSET:
			hw_src = AM_TS_SRC_DMX0;
		break;
		case DMX_SOURCE_FRONT1_OFFSET:
			hw_src = AM_TS_SRC_DMX1;
		break;
		case DMX_SOURCE_FRONT2_OFFSET:
			hw_src = AM_TS_SRC_DMX2;
		break;
		default:
			hw_src=-1;
		break;
	}

	if(hw_src != dvb->dsc_source){
		int old_source = dvb->dsc_source;
		dvb->dsc_source = hw_src;
		if(old_source != -1)
			dmx_reset_dmx_id_hw_ex_unlock(dvb, (old_source-AM_TS_SRC_DMX0), 0);
		if(hw_src != -1)
			dmx_reset_dmx_id_hw_ex_unlock(dvb, (hw_src-AM_TS_SRC_DMX0), 0);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_tso_hw_set_source(struct aml_dvb *dvb, dmx_source_t src)
{
	int ret = 0;
	unsigned long flags;
	int hw_src;

	spin_lock_irqsave(&dvb->slock, flags);

	hw_src = dvb->tso_source;

	switch(src) {
		case DMX_SOURCE_FRONT0:
			hw_src = (dvb->ts[0].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS0 : AM_TS_SRC_TS0;
		break;
		case DMX_SOURCE_FRONT1:
			hw_src = (dvb->ts[1].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS1 : AM_TS_SRC_TS1;
		break;
		case DMX_SOURCE_FRONT2:
			hw_src = (dvb->ts[2].mode == AM_TS_SERIAL) ? AM_TS_SRC_S_TS2 : AM_TS_SRC_TS2;
		break;
		case DMX_SOURCE_DVR0:
			hw_src = AM_TS_SRC_HIU;
		break;
		case DMX_SOURCE_FRONT0+100:
			hw_src = AM_TS_SRC_DMX0;
		break;
		case DMX_SOURCE_FRONT1+100:
			hw_src = AM_TS_SRC_DMX1;
		break;
		case DMX_SOURCE_FRONT2+100:
			hw_src = AM_TS_SRC_DMX2;
		break;
		default:
			hw_src = -1;
			ret = -EINVAL;
		break;
	}

	if(hw_src != dvb->tso_source){
		dvb->tso_source = hw_src;
		stb_enable(dvb);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_asyncfifo_hw_set_source(struct aml_asyncfifo *afifo, aml_dmx_id_t src)
{
	struct aml_dvb *dvb = afifo->dvb;
	int ret = -1;
	unsigned long flags;

	if (sf_afifo_sf(afifo)) {
		pr_error("%s: afifo %d is in sf mode\n", __func__, afifo->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&dvb->slock, flags);

	pr_dbg("asyncfifo %d set source %d->%d", afifo->id, afifo->source, src);
	switch(src) {
		case AM_DMX_0:
		case AM_DMX_1:
		case AM_DMX_2:
			if (afifo->source != src) {
				afifo->source = src;
				ret = 0;
			}
		break;
		default:
			pr_error("illegal async fifo source %d\n", src);
			ret = -EINVAL;
		break;
	}

	if(ret==0 && afifo->dvb){
		reset_async_fifos(afifo->dvb);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

int aml_dmx_hw_set_dump_ts_select(struct dmx_demux* demux, int dump_ts_select)
{
	struct aml_dmx *dmx = (struct aml_dmx*)demux;
	struct aml_dvb *dvb = (struct aml_dvb*)dmx->demux.priv;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);
	dump_ts_select = !!dump_ts_select;
	if (dmx->dump_ts_select != dump_ts_select){
		dmx->dump_ts_select = dump_ts_select;
		dmx_reset_dmx_hw_ex_unlock(dvb, dmx, 0);
	}
	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}

u32 aml_dmx_get_video_pts(struct aml_dvb *dvb)
{
	unsigned long flags;
	u32 pts;

	spin_lock_irqsave(&dvb->slock, flags);
	pts = video_pts;
	spin_unlock_irqrestore(&dvb->slock, flags);

	return pts;
}

u32 aml_dmx_get_audio_pts(struct aml_dvb *dvb)
{
	unsigned long flags;
	u32 pts;

	spin_lock_irqsave(&dvb->slock, flags);
	pts = audio_pts;
	spin_unlock_irqrestore(&dvb->slock, flags);

	return pts;
}

u32 aml_dmx_get_first_video_pts(struct aml_dvb *dvb)
{
	unsigned long flags;
	u32 pts;

	spin_lock_irqsave(&dvb->slock, flags);
	pts = first_video_pts;
	spin_unlock_irqrestore(&dvb->slock, flags);

	return pts;
}

u32 aml_dmx_get_first_audio_pts(struct aml_dvb *dvb)
{
	unsigned long flags;
	u32 pts;

	spin_lock_irqsave(&dvb->slock, flags);
	pts = first_audio_pts;
	spin_unlock_irqrestore(&dvb->slock, flags);

	return pts;
}

int aml_dmx_set_skipbyte(struct aml_dvb *dvb, int skipbyte)
{
	if (demux_skipbyte != skipbyte) {
		pr_dbg("set skip byte %d\n", skipbyte);
		demux_skipbyte = skipbyte;
		dmx_reset_hw_ex(dvb, 0);
	}

	return 0;
}

int aml_dmx_set_demux(struct aml_dvb *dvb, int id)
{
	aml_stb_hw_set_source(dvb, DMX_SOURCE_DVR0);
	if(id < DMX_DEV_COUNT){
		struct aml_dmx *dmx = &dvb->dmx[id];
		aml_dmx_hw_set_source((struct dmx_demux*)dmx, DMX_SOURCE_DVR0);
	}

	return 0;
}

int _set_tsfile_clkdiv(struct aml_dvb *dvb, int clkdiv)
{
	if (tsfile_clkdiv != clkdiv) {
		pr_dbg("set ts file clock div %d\n", clkdiv);
		tsfile_clkdiv = clkdiv;
		dmx_reset_hw(dvb);
	}

	return 0;
}
static ssize_t stb_set_tsfile_clkdiv(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	int div = (int)simple_strtol(buf, NULL, 10);
	_set_tsfile_clkdiv(aml_get_dvb_device(), div);
	return size;
}
static ssize_t stb_get_tsfile_clkdiv(struct class *class, struct class_attribute *attr,char *buf)
{
	ssize_t ret;
	ret = sprintf(buf, "%d\n", tsfile_clkdiv);\
	return ret;
}

#define DEMUX_SCAMBLE_FUNC_DECL(i)  \
static ssize_t dmx_reg_value_show_demux##i##_scramble(struct class *class,  struct class_attribute *attr,char *buf)\
{\
	int data = 0;\
	int aflag = 0;\
	int vflag = 0;\
	ssize_t ret = 0;\
	data = DMX_READ_REG(i, DEMUX_SCRAMBLING_STATE);\
	if ((data & 0x01 )== 0x01){\
		vflag = 1;}\
	if ((data & 0x02 )== 0x02){\
		aflag = 1;}\
	ret = sprintf(buf, "%d %d\n", vflag, aflag);\
	return ret;\
}

#if DMX_DEV_COUNT>0
	DEMUX_SCAMBLE_FUNC_DECL(0)
#endif

#if DMX_DEV_COUNT>1
	DEMUX_SCAMBLE_FUNC_DECL(1)
#endif


static ssize_t dmx_reg_addr_show_source(struct class *class, struct class_attribute *attr,char *buf);
static ssize_t dmx_reg_addr_store_source(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size);
static ssize_t dmx_id_show_source(struct class *class, struct class_attribute *attr,char *buf);
static ssize_t dmx_id_store_source(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size);
static ssize_t dmx_reg_value_show_source(struct class *class, struct class_attribute *attr,char *buf);
static ssize_t dmx_reg_value_store_source(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size);


static int reg_addr=0;
static int dmx_id=0;
static struct class_attribute aml_dmx_class_attrs[] = {
	__ATTR(dmx_id,  S_IRUGO | S_IWUSR, dmx_id_show_source, dmx_id_store_source),
	__ATTR(register_addr,  S_IRUGO | S_IWUSR, dmx_reg_addr_show_source, dmx_reg_addr_store_source),
	__ATTR(register_value,  S_IRUGO | S_IWUSR, dmx_reg_value_show_source, dmx_reg_value_store_source),
	__ATTR(tsfile_clkdiv,  S_IRUGO | S_IWUSR, stb_get_tsfile_clkdiv, stb_set_tsfile_clkdiv),

	#define DEMUX_SCAMBLE_ATTR_DECL(i)\
		__ATTR(demux##i##_scramble,  S_IRUGO | S_IWUSR, dmx_reg_value_show_demux##i##_scramble, NULL)
	#if DMX_DEV_COUNT>0
		DEMUX_SCAMBLE_ATTR_DECL(0),
	#endif
	#if DMX_DEV_COUNT>1
		DEMUX_SCAMBLE_ATTR_DECL(1),
	#endif
	__ATTR_NULL
};

static struct class aml_dmx_class = {
	.name = "dmx",
	.class_attrs = aml_dmx_class_attrs,
};


static ssize_t dmx_id_show_source(struct class *class, struct class_attribute *attr,char *buf)
{
	int ret;
	ret = sprintf(buf, "%d\n", dmx_id);
	return ret;
}
static ssize_t dmx_id_store_source(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	int id=0;
	id=simple_strtol(buf,0,16);

	if(id<0||id>2)
	{
		pr_dbg("dmx id must 0 ~2\n");
	}
	else
	{
		dmx_id=id;
	}

	return size;
}


static ssize_t dmx_reg_addr_show_source(struct class *class, struct class_attribute *attr,char *buf)
{
	int ret;
	ret = sprintf(buf, "%x\n", reg_addr);
	return ret;
}
static ssize_t dmx_reg_addr_store_source(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	int addr=0;
	addr=simple_strtol(buf,0,16);
	reg_addr=addr;
	return size;
}


static ssize_t dmx_reg_value_show_source(struct class *class, struct class_attribute *attr,char *buf)
{
	int ret,value;
	value=READ_MPEG_REG(reg_addr);
	ret = sprintf(buf, "%x\n", value);
	return ret;
}
static ssize_t dmx_reg_value_store_source(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	int value=0;
	value=simple_strtol(buf,0,16);
	WRITE_MPEG_REG(reg_addr,value);
	return size;
}

int aml_regist_dmx_class(void)
{

	if(class_register(&aml_dmx_class)<0) {
                pr_error("register class error\n");
        }

	return 0;
}


int aml_unregist_dmx_class(void)
{

	class_unregister(&aml_dmx_class);
	return 0;
}

static struct aml_dmx* get_dmx_from_src(aml_ts_source_t src)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_dmx *dmx = NULL;
	int i;

	if(dvb){
		switch(src){
			case AM_TS_SRC_TS0:
			case AM_TS_SRC_TS1:
			case AM_TS_SRC_TS2:
			case AM_TS_SRC_S_TS0:
			case AM_TS_SRC_S_TS1:
			case AM_TS_SRC_S_TS2:
				for(i=0; i<DMX_DEV_COUNT; i++){
					if(dvb->dmx[i].source == src){
						dmx = &dvb->dmx[i];
						break;
					}
				}
				break;
			case AM_TS_SRC_DMX0:
				if(0>DMX_DEV_COUNT){
					dmx = &dvb->dmx[0];
				}
				break;
			case AM_TS_SRC_DMX1:
				if(1>DMX_DEV_COUNT){
					dmx = &dvb->dmx[1];
				}
				break;
			case AM_TS_SRC_DMX2:
				if(2>DMX_DEV_COUNT){
					dmx = &dvb->dmx[2];
				}
				break;
			default:
				break;
		}
	}

	return dmx;
}

void aml_dmx_register_frontend(aml_ts_source_t src, struct dvb_frontend *fe)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_dmx *dmx = get_dmx_from_src(src);
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);

	if(dmx){
		dmx->fe = fe;
	}

	spin_unlock_irqrestore(&dvb->slock, flags);
}

void aml_dmx_before_retune(aml_ts_source_t src, struct dvb_frontend *fe)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_dmx *dmx = get_dmx_from_src(src);
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);

	if(dmx){
		dmx->fe = fe;
		dmx->in_tune = 1;
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, 0);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);
}

void aml_dmx_after_retune(aml_ts_source_t src, struct dvb_frontend *fe)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_dmx *dmx = get_dmx_from_src(src);
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);

	if(dmx){
		dmx->fe = fe;
		dmx->in_tune = 0;
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, DEMUX_INT_MASK);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);
}
EXPORT_SYMBOL(aml_dmx_after_retune);

void aml_dmx_start_error_check(aml_ts_source_t src, struct dvb_frontend *fe)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_dmx *dmx = get_dmx_from_src(src);
	unsigned long flags;

	spin_lock_irqsave(&dvb->slock, flags);

	if(dmx){
		dmx->fe = fe;
		dmx->error_check = 0;
		dmx->int_check_time = 0;
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, DEMUX_INT_MASK);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);
}
EXPORT_SYMBOL(aml_dmx_start_error_check);

int  aml_dmx_stop_error_check(aml_ts_source_t src, struct dvb_frontend *fe)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_dmx *dmx = get_dmx_from_src(src);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dvb->slock, flags);

	if(dmx){
		dmx->fe = fe;
		ret = dmx->error_check;
		DMX_WRITE_REG(dmx->id, STB_INT_MASK, DEMUX_INT_MASK);
	}

	spin_unlock_irqrestore(&dvb->slock, flags);

	return ret;
}
EXPORT_SYMBOL(aml_dmx_stop_error_check);

