#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>

#include <linux/amlogic/amports/tsync.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/ptsserv.h>

#include "tsync_pcr.h"
#include "amvdec.h"
#include "tsdemux.h"
#include "streambuf.h"
#include "amports_priv.h"

#define CONFIG_AM_PCRSYNC_LOG

#ifdef CONFIG_AM_PCRSYNC_LOG
#define AMLOG
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_ATTENTION 1
#define LOG_LEVEL_INFO      2
#define LOG_LEVEL_VAR       amlog_level_tsync_pcr
#define LOG_MASK_VAR        amlog_mask_tsync_pcr
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0, LOG_DEFAULT_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

typedef enum {
    PLAY_MODE_NORMAL=0,
    PLAY_MODE_SLOW,
    PLAY_MODE_SPEED,
    PLAY_MODE_FORCE_SLOW,
    PLAY_MODE_FORCE_SPEED,
} play_mode_t;

typedef enum {
    INIT_PRIORITY_PCR=0,
    INIT_PRIORITY_AUDIO=1,
    INIT_PRIORITY_VIDEO=2,
} pcr_init_priority_t;

#define PCR_DISCONTINUE		0x01
#define VIDEO_DISCONTINUE	0x02
#define AUDIO_DISCONTINUE	0x10

#define CHECK_INTERVAL  (HZ * 5)

#define START_AUDIO_LEVEL       256
#define START_VIDEO_LEVEL       2048
#define PAUSE_AUDIO_LEVEL         32
#define PAUSE_VIDEO_LEVEL         2560
#define UP_RESAMPLE_AUDIO_LEVEL      128
#define UP_RESAMPLE_VIDEO_LEVEL      1024
#define DOWN_RESAMPLE_CACHE_TIME     90000*2
#define NO_DATA_CHECK_TIME           4000

/* the diff of system time and referrence lock, which use the threshold to adjust the system time  */
#define OPEN_RECOVERY_THRESHOLD 18000
#define CLOSE_RECOVERY_THRESHOLD 300
#define RECOVERY_SPAN 3
#define FORCE_RECOVERY_SPAN 20
#define PAUSE_CHECK_TIME   2700
#define PAUSE_RESUME_TIME   18000

static u32 tsync_pcr_recovery_span = 3;


/* the delay from ts demuxer to the amvideo  */
#define DEFAULT_VSTREAM_DELAY 18000

#define RESAMPLE_TYPE_NONE      0
#define RESAMPLE_TYPE_DOWN      1
#define RESAMPLE_TYPE_UP        2
#define RESAMPLE_DOWN_FORCE_PCR_SLOW 3

#define MS_INTERVAL  (HZ/1000)		
#define TEN_MS_INTERVAL  (HZ/100)		

// local system inited check type
#define TSYNC_PCR_INITCHECK_PCR       0x0001 
#define TSYNC_PCR_INITCHECK_VPTS       0x0002
#define TSYNC_PCR_INITCHECK_APTS       0x0004
#define TSYNC_PCR_INITCHECK_RECORD      0x0008
#define TSYNC_PCR_INITCHECK_END       0x0010

#define MIN_GAP 90000*3 // 3s
#define MAX_GAP 90000*3 // 4s

// av sync monitor threshold
#define MAX_SYNC_VGAP_TIME   90000
#define MIN_SYNC_VCHACH_TIME   45000

#define MAX_SYNC_AGAP_TIME   45000
#define MIN_SYNC_ACHACH_TIME   27000

// ------------------------------------------------------------------
// The const 

static u32 tsync_pcr_discontinue_threshold = (TIME_UNIT90K * 1.5);
static u32 tsync_pcr_ref_latency = (TIME_UNIT90K * 0.3);				//TIME_UNIT90K*0.3

// use for pcr valid mode
static u32 tsync_pcr_max_cache_time = TIME_UNIT90K*1.5;			//TIME_UNIT90K*1.5;
static u32 tsync_pcr_up_cache_time = TIME_UNIT90K*1.2;				//TIME_UNIT90K*1.2;
static u32 tsync_pcr_down_cache_time = TIME_UNIT90K*0.6;			//TIME_UNIT90K*0.6;
static u32 tsync_pcr_min_cache_time = TIME_UNIT90K*0.2;			//TIME_UNIT90K*0.2;


// use for pcr invalid mode
static u32 tsync_pcr_max_delay_time = TIME_UNIT90K*3;				//TIME_UNIT90K*3;
static u32 tsync_pcr_up_delay_time = TIME_UNIT90K*2;				//TIME_UNIT90K*2;
static u32 tsync_pcr_down_delay_time = TIME_UNIT90K*1.5;			//TIME_UNIT90K*1.5;
static u32 tsync_pcr_min_delay_time = TIME_UNIT90K*0.8;			//TIME_UNIT90K*0.8;


static u32 tsync_pcr_first_video_frame_pts = 0;				
static u32 tsync_pcr_first_audio_frame_pts = 0;

// reset control flag
static u8 tsync_pcr_reset_flag = 0;
static int tsync_pcr_asynccheck_cnt = 0;
static int tsync_pcr_vsynccheck_cnt = 0;

// ------------------------------------------------------------------
// The variate

static struct timer_list tsync_pcr_check_timer;

static u32 tsync_pcr_system_startpcr=0;
static u32 tsync_pcr_tsdemux_startpcr=0;

static int tsync_pcr_vpause_flag = 0;
static int tsync_pcr_apause_flag = 0;
static int tsync_pcr_vstart_flag = 0;
static int tsync_pcr_astart_flag = 0;
static u8 tsync_pcr_inited_flag = 0;
static u8 tsync_pcr_inited_mode = INIT_PRIORITY_PCR;
static u32 tsync_pcr_freerun_mode=0;

// pause handle paramter
static int64_t tsync_pcr_lastcheckin_vpts=0;
static int64_t tsync_pcr_lastcheckin_apts=0;
static int tsync_pcr_pausecheck_cnt=0;
static int64_t tsync_pcr_stream_delta=0;

// the really ts demuxer pcr, haven't delay
static u32 tsync_pcr_last_tsdemuxpcr = 0;
static u32 tsync_pcr_discontinue_local_point = 0;
static u32 tsync_pcr_discontinue_waited = 0;							// the time waited the v-discontinue to happen
static u8 tsync_pcr_tsdemuxpcr_discontinue = 0;						// the boolean value		
static u32 tsync_pcr_discontinue_point = 0;

static int abuf_level=0;
static int abuf_size=0;
static int vbuf_level=0;
static int vbuf_size=0;
static int play_mode=PLAY_MODE_NORMAL;
static u8 tsync_pcr_started=0;
static int tsync_pcr_read_cnt=0;
static u8 tsync_pcr_usepcr=1;
//static int tsync_pcr_debug_pcrscr = 100;
static u64 first_time_record = 0;
static u8 wait_pcr_count = 0;

static DEFINE_SPINLOCK(tsync_pcr_lock);

#define LTRACE() printk("[%s:%d] \n", __FUNCTION__,__LINE__);

extern int get_vsync_pts_inc_mode(void);

u32 get_stbuf_rp(int type){
    stream_buf_t *pbuf=NULL;
    if (type == 0) {
        // video
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        pbuf=get_buf_by_type(PTS_TYPE_HEVC);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_rp(pbuf);
        }
#endif

        pbuf=get_buf_by_type(PTS_TYPE_VIDEO);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_rp(pbuf);
        }
    }
    else{
        // audio
        pbuf=get_buf_by_type(PTS_TYPE_AUDIO);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_rp(pbuf);
        }
    }

    return 0;
}

int get_stream_buffer_level(int type){
    stream_buf_t *pbuf=NULL;
    if (type == 0) {
        // video
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        pbuf=get_buf_by_type(PTS_TYPE_HEVC);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_level(pbuf);
        }
#endif

        pbuf=get_buf_by_type(PTS_TYPE_VIDEO);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_level(pbuf);
        }
    }
    else{
        // audio
        pbuf=get_buf_by_type(PTS_TYPE_AUDIO);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_level(pbuf);
        }
    }

    return 0;
}

int get_stream_buffer_size(int type){
    stream_buf_t *pbuf=NULL;
    if (type == 0) {
        // video
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        pbuf=get_buf_by_type(PTS_TYPE_HEVC);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_size(pbuf);
        }
#endif

        pbuf=get_buf_by_type(PTS_TYPE_VIDEO);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_size(pbuf);
        }
    }
    else{
        // audio
        pbuf=get_buf_by_type(PTS_TYPE_AUDIO);
        if (pbuf != NULL && pbuf->flag&BUF_FLAG_IN_USE) {
            return stbuf_size(pbuf);
        }
    }

    return 0;
}

// 90k HZ
int get_min_cache_delay(void){
    //int video_cache_time = calculation_vcached_delayed();
    //int audio_cache_time = calculation_acached_delayed();
    int audio_cache_time = 90 * calculation_stream_delayed_ms(PTS_TYPE_AUDIO,NULL,NULL);
    int video_cache_time = 90 * calculation_stream_delayed_ms(PTS_TYPE_VIDEO,NULL,NULL);
    if (video_cache_time>0 && audio_cache_time>0) {
        return min(video_cache_time,audio_cache_time);
    }
    else if (video_cache_time>0)
        return video_cache_time;
    else if (audio_cache_time>0)
        return audio_cache_time;
    else
        return 0;
}

u32 tsync_pcr_vstream_delayed(void)
{
    int cur_delay = calculation_vcached_delayed();	
    if (cur_delay == -1)
        return DEFAULT_VSTREAM_DELAY;

    return cur_delay;
}

u32 tsync_pcr_get_min_checkinpts(void)
{
    u32 last_checkin_vpts=get_last_checkin_pts(PTS_TYPE_VIDEO);
    u32 last_checkin_apts=get_last_checkin_pts(PTS_TYPE_AUDIO);
    if (last_checkin_apts>0 && last_checkin_apts>0) {
        return min(last_checkin_vpts,last_checkin_apts);
    }
    else if (last_checkin_apts>0) {
        return last_checkin_apts;
    }
    else if (last_checkin_vpts>0) {
        return last_checkin_vpts;
    }
    else{
        return 0;
    }
}

void tsync_pcr_get_delta(void)
{
    // calculate delta
    u64 cur_pcr=timestamp_pcrscr_get();
    u64 demux_pcr=tsdemux_pcrscr_get();
    if (cur_pcr == 0 || demux_pcr == 0) {
        tsync_pcr_stream_delta=0;
        return;
    }

    if (tsync_pcr_usepcr == 1) {
        tsync_pcr_stream_delta=(int64_t)demux_pcr-(int64_t)cur_pcr;
    }
    else{
        tsync_pcr_stream_delta=(int64_t)(jiffies*TIME_UNIT90K)/HZ - (int64_t)cur_pcr;
    }

    printk("[%s:%d]tsync_pcr_stream_delta = %lld\n",__FUNCTION__,__LINE__,tsync_pcr_stream_delta);
}

void tsync_pcr_pcrscr_set(void)
{
    u32 first_pcr=0 ,first_vpts = 0, first_apts = 0;
    u32 cur_checkin_apts=0, cur_checkin_vpts=0, min_checkinpts=0;
    u32 cur_pcr = 0, ref_pcr=0;
    u8 complete_init_flag=TSYNC_PCR_INITCHECK_PCR|TSYNC_PCR_INITCHECK_VPTS|TSYNC_PCR_INITCHECK_APTS;

    if (tsync_pcr_inited_flag & complete_init_flag) {
        //printk("pcr already inited.flag=0x%x \n",tsync_pcr_inited_flag);
        return;
    }

    first_pcr =tsdemux_first_pcrscr_get();
    first_apts = timestamp_firstapts_get();
    first_vpts = timestamp_firstvpts_get();
    cur_checkin_vpts = get_last_checkin_pts(PTS_TYPE_VIDEO);
    cur_checkin_apts = get_last_checkin_pts(PTS_TYPE_AUDIO);
    min_checkinpts=tsync_pcr_get_min_checkinpts();
    cur_pcr = tsdemux_pcrscr_get();
    abuf_level=get_stream_buffer_level(1);
    vbuf_level=get_stream_buffer_level(0);

    // check the valid of the pcr
    if (cur_pcr && cur_checkin_vpts && cur_checkin_apts) {
        u32 gap_pa, gap_pv, gap_av;
        gap_pa = abs(cur_pcr-cur_checkin_apts);
        gap_av = abs(cur_checkin_apts-cur_checkin_vpts);
        gap_pv = abs(cur_pcr-cur_checkin_vpts);
        if ((gap_pa>MAX_GAP) && (gap_pv>MAX_GAP)) {
            //printk("[%d]invalid cur_pcr = 0x%x, gap_pa = %d, gap_pv = %d, gap_av = %d \n", __LINE__,cur_pcr, gap_pa,gap_pv,gap_pv);
            cur_pcr = 0;
        }
    }

    // decide use which para to init
    if (cur_pcr && first_apts && first_vpts && !(tsync_pcr_inited_flag&complete_init_flag) && (cur_pcr<min_checkinpts)) {
        tsync_pcr_inited_flag|=TSYNC_PCR_INITCHECK_PCR;
        ref_pcr=cur_pcr-tsync_pcr_ref_latency;
        timestamp_pcrscr_set(ref_pcr);
        tsync_pcr_usepcr=1;

        timestamp_pcrscr_enable(1);
        printk("check init.first_pcr=0x%x, first_apts=0x%x, first_vpts=0x%x, cur_pcr = 0x%x, checkin_vpts=0x%x, checkin_apts=0x%x alevel=%d vlevel=%d\n",
            first_pcr, first_apts, first_vpts, cur_pcr, cur_checkin_vpts, cur_checkin_apts,abuf_level, vbuf_level);
        printk("[%d]init by pcr. pcr=%x usepcr=%d \n", __LINE__,ref_pcr,tsync_pcr_usepcr);
    }

    if (first_apts && !(tsync_pcr_inited_flag&complete_init_flag) && (first_apts<min_checkinpts && tsync_pcr_inited_mode >= INIT_PRIORITY_AUDIO)) {
        tsync_pcr_inited_flag|=TSYNC_PCR_INITCHECK_APTS;
        ref_pcr=first_apts;
        timestamp_pcrscr_set(ref_pcr);
        if (cur_pcr > 0)
            tsync_pcr_usepcr=1;

        timestamp_pcrscr_enable(1);
        printk("check init.first_pcr=0x%x, first_apts=0x%x, first_vpts=0x%x, cur_pcr = 0x%x, checkin_vpts=0x%x, checkin_apts=0x%x alevel=%d vlevel=%d \n",
            first_pcr, first_apts, first_vpts, cur_pcr, cur_checkin_vpts, cur_checkin_apts,abuf_level, vbuf_level);
        printk("[%d]init by apts. pcr=%x usepcr=%d \n", __LINE__,ref_pcr,tsync_pcr_usepcr);
    }

    if (first_vpts && !(tsync_pcr_inited_flag&complete_init_flag) && (first_vpts<min_checkinpts && tsync_pcr_inited_mode >= INIT_PRIORITY_VIDEO)) {
        tsync_pcr_inited_flag|=TSYNC_PCR_INITCHECK_VPTS;
        ref_pcr = first_vpts - tsync_pcr_ref_latency * 2;
        timestamp_pcrscr_set(ref_pcr);
        if (cur_pcr > 0)
            tsync_pcr_usepcr=1;

        timestamp_pcrscr_enable(1);
        printk("check init.first_pcr=0x%x, first_apts=0x%x, first_vpts=0x%x, cur_pcr = 0x%x, checkin_vpts=0x%x, checkin_apts=0x%x alevel=%d vlevel=%d \n",
            first_pcr, first_apts, first_vpts, cur_pcr, cur_checkin_vpts, cur_checkin_apts,abuf_level, vbuf_level);
        printk("[%d]init by vpts. pcr=%x usepcr=%d \n", __LINE__,ref_pcr,tsync_pcr_usepcr);
    }
}

void tsync_pcr_avevent_locked(avevent_t event, u32 param)
{
    ulong flags;
    spin_lock_irqsave(&tsync_pcr_lock, flags);

    switch (event) {
    case VIDEO_START:        
        if (tsync_pcr_vstart_flag == 0) {
            //play_mode=PLAY_MODE_NORMAL;
            tsync_pcr_first_video_frame_pts = param;
            printk("video start! param=%x cur_pcr=%x\n",param,timestamp_pcrscr_get());
        }

        tsync_pcr_pcrscr_set();

        tsync_pcr_vstart_flag=1;
        break;

    case VIDEO_STOP:
        timestamp_pcrscr_enable(0);
        printk("timestamp_firstvpts_set !\n");
        timestamp_firstvpts_set(0);
        timestamp_firstapts_set(0);
        timestamp_vpts_set(0);
        //tsync_pcr_debug_pcrscr=100;

        tsync_pcr_vpause_flag=0;
        tsync_pcr_vstart_flag=0;
        tsync_pcr_inited_flag=0;
        wait_pcr_count = 0;

        printk("wait_pcr_count = 0 \n");
        tsync_pcr_tsdemuxpcr_discontinue=0;
        tsync_pcr_discontinue_point=0;
        tsync_pcr_discontinue_local_point=0;
        tsync_pcr_discontinue_waited=0;
        tsync_pcr_first_video_frame_pts=0;

        tsync_pcr_tsdemux_startpcr = 0;
        tsync_pcr_system_startpcr = 0;
        play_mode=PLAY_MODE_NORMAL;
        printk("video stop! \n");
        break;
        
    case VIDEO_TSTAMP_DISCONTINUITY:  	 
        {
            u32 tsdemux_pcr = tsdemux_pcrscr_get();

            printk("[%s]video discontinue happen.discontinue=%d oldpts=%x new=%x cur_pcr=%x\n",__FUNCTION__,
                tsync_pcr_tsdemuxpcr_discontinue,timestamp_vpts_get(), param, timestamp_pcrscr_get());
            //if((abs(param-oldpts)>AV_DISCONTINUE_THREDHOLD_MIN) && (!get_vsync_pts_inc_mode())){
            if (!get_vsync_pts_inc_mode()) {
                u32 last_checkin_minpts=tsync_pcr_get_min_checkinpts();
                u32 ref_pcr = 0;
                //if (last_checkin_minpts<param && last_checkin_minpts) {
                    //ref_pcr= last_checkin_minpts-tsync_pcr_ref_latency;
                    //printk("[%s]Use checkin minpts. minpts=%x param=%x ref_pcr=%x\n",__FUNCTION__,last_checkin_minpts,param, oldpts,ref_pcr);
                //}
                //else{
                ref_pcr = param-tsync_pcr_ref_latency;
                printk("[%s]Use param. minpts=%x ref_pcr=%x\n",__FUNCTION__,last_checkin_minpts, ref_pcr);
                //}
                //if(ref_pcr == 0)
                //ref_pcr=tsdemux_pcr-tsync_pcr_vstream_delayed();

                timestamp_pcrscr_set(ref_pcr);

                tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
                tsync_pcr_system_startpcr = ref_pcr;
                //play_mode=PLAY_MODE_FORCE_SLOW;

                /* to resume the pcr check*/
                tsync_pcr_tsdemuxpcr_discontinue|=VIDEO_DISCONTINUE;
                tsync_pcr_discontinue_point=0;
                tsync_pcr_discontinue_local_point=0;
                tsync_pcr_discontinue_waited=0;
            }
            timestamp_vpts_set(param);

            break;
        }
    case AUDIO_TSTAMP_DISCONTINUITY:
        {
            printk("[%s]audio discontinue happen.discontinue=%d param=%x\n",__FUNCTION__,tsync_pcr_tsdemuxpcr_discontinue,param);
            tsync_pcr_tsdemuxpcr_discontinue|=AUDIO_DISCONTINUE;
        }
        break;

    case AUDIO_PRE_START:
        timestamp_apts_start(0);
        tsync_pcr_astart_flag=0;
        printk("audio prestart!   \n");
        break;

    case AUDIO_START:		
        timestamp_apts_set(param);
        timestamp_apts_enable(1);
        timestamp_apts_start(1);
        tsync_pcr_first_audio_frame_pts=param;
        tsync_pcr_astart_flag=1;
        tsync_pcr_apause_flag=0;

        tsync_pcr_pcrscr_set();

        printk("audio start!timestamp_apts_set =%x. \n",param);
        break;

    case AUDIO_RESUME:
        timestamp_apts_enable(1);
        tsync_pcr_apause_flag=0;
        printk("audio resume!   \n");
        break;

    case AUDIO_STOP:
        timestamp_apts_enable(0);
        timestamp_apts_set(-1);
        timestamp_firstapts_set(0);
        timestamp_apts_start(0);
        tsync_pcr_astart_flag=0;
        tsync_pcr_apause_flag=0;
        tsync_pcr_first_audio_frame_pts=0;
        printk("audio stop!   \n");
        break;

    case AUDIO_PAUSE:
        timestamp_apts_enable(0);
        tsync_pcr_apause_flag=1;
        printk("audio pause!   \n");
        break;

    case VIDEO_PAUSE:
        if (param == 1) {
            timestamp_pcrscr_enable(0);
            tsync_pcr_vpause_flag = 1;
            printk("video pause!\n");
        }else {
            timestamp_pcrscr_enable(1);
            tsync_pcr_vpause_flag = 0;
            printk("video resume\n");
        }
        break;

    default:
        break;
    }
    switch (event) {
    case VIDEO_START:
    case AUDIO_START:
    case AUDIO_RESUME:
        amvdev_resume();
        break;
    case VIDEO_STOP:
    case AUDIO_STOP:
    case AUDIO_PAUSE:
        amvdev_pause();
        break;
    case VIDEO_PAUSE:
        if (tsync_pcr_vpause_flag)
            amvdev_pause();
        else
            amvdev_resume();
        break;
    default:
        break;
    }

    spin_unlock_irqrestore(&tsync_pcr_lock, flags);
}

// timer to check the system with the referrence time in ts stream.
static unsigned long tsync_pcr_check(void)
{
    u32 tsdemux_pcr=0;
    u32 tsdemux_pcr_diff=0;
    int need_recovery=1;
    unsigned long res=1;
    int64_t last_checkin_vpts=0;
    int64_t last_checkin_apts=0;
    int64_t last_cur_pcr=0;
    int64_t last_checkin_minpts=0;
    u32 cur_apts=0;
    u32 cur_vpts=0;

    if (tsync_get_mode() != TSYNC_MODE_PCRMASTER || tsync_pcr_freerun_mode == 1) {
        return res;
    }

    tsdemux_pcr=tsdemux_pcrscr_get();
    if (tsync_pcr_usepcr == 1) {
        // To monitor the pcr discontinue
        tsdemux_pcr_diff=abs(tsdemux_pcr - tsync_pcr_last_tsdemuxpcr);
        if (tsync_pcr_last_tsdemuxpcr != 0 && tsdemux_pcr != 0 && tsdemux_pcr_diff > tsync_pcr_discontinue_threshold && tsync_pcr_inited_flag >= TSYNC_PCR_INITCHECK_PCR) {
            u32 video_delayed=0;
            tsync_pcr_tsdemuxpcr_discontinue|=PCR_DISCONTINUE;
            video_delayed = tsync_pcr_vstream_delayed();
            if (TIME_UNIT90K*2 <= video_delayed && video_delayed <= TIME_UNIT90K*4)
                tsync_pcr_discontinue_waited=video_delayed+TIME_UNIT90K;
            else if (TIME_UNIT90K*2 > video_delayed)
                tsync_pcr_discontinue_waited=TIME_UNIT90K*3;
            else
                tsync_pcr_discontinue_waited=TIME_UNIT90K*5;

            printk("[tsync_pcr_check] refpcr_discontinue. tsdemux_pcr_diff=%x, last refpcr=%x, repcr=%x,waited=%x\n",tsdemux_pcr_diff,tsync_pcr_last_tsdemuxpcr,tsdemux_pcr,tsync_pcr_discontinue_waited);
            tsync_pcr_discontinue_local_point=timestamp_pcrscr_get();
            tsync_pcr_discontinue_point=tsdemux_pcr-tsync_pcr_ref_latency;
            need_recovery=0;
        }
        else if (tsync_pcr_tsdemuxpcr_discontinue & PCR_DISCONTINUE) {
            // to pause the pcr check
            if (abs(timestamp_pcrscr_get() - tsync_pcr_discontinue_local_point) > tsync_pcr_discontinue_waited) {
                printk("[tsync_pcr_check] audio or video discontinue didn't happen, waited=%x\n",tsync_pcr_discontinue_waited);
                // the v-discontinue did'n happen
                tsync_pcr_tsdemuxpcr_discontinue=0;
                tsync_pcr_discontinue_point=0;
                tsync_pcr_discontinue_local_point=0;
                tsync_pcr_discontinue_waited=0;
            }
            need_recovery=0;
        }
        else if (tsync_pcr_tsdemuxpcr_discontinue & (PCR_DISCONTINUE|VIDEO_DISCONTINUE|AUDIO_DISCONTINUE)) {
            tsync_pcr_tsdemuxpcr_discontinue=0;
        }
        tsync_pcr_last_tsdemuxpcr=tsdemux_pcr;
    }

     //if(!(tsync_pcr_inited_flag&TSYNC_PCR_INITCHECK_PCR)||!(tsync_pcr_inited_flag&TSYNC_PCR_INITCHECK_VPTS)){
    if ((!(tsync_pcr_inited_flag&TSYNC_PCR_INITCHECK_VPTS)) && (!(tsync_pcr_inited_flag&TSYNC_PCR_INITCHECK_PCR)) && (!(tsync_pcr_inited_flag&TSYNC_PCR_INITCHECK_APTS))){
        u64 cur_system_time=(jiffies * TIME_UNIT90K) / HZ;
        if (cur_system_time - first_time_record < 270000) {
            tsync_pcr_pcrscr_set();
        }
        else {
            tsync_pcr_inited_mode=INIT_PRIORITY_VIDEO;
            tsync_pcr_pcrscr_set();
        }

        return res;
     }

    if (tsync_pcr_stream_delta == 0)
        tsync_pcr_get_delta();

    abuf_level= get_stream_buffer_level(1);
    abuf_size= get_stream_buffer_size(1);
    vbuf_level= get_stream_buffer_level(0);
    vbuf_size= get_stream_buffer_size(0);

    last_checkin_vpts=(u32)get_last_checkin_pts(PTS_TYPE_VIDEO);
    last_checkin_apts=(u32)get_last_checkin_pts(PTS_TYPE_AUDIO);
    last_cur_pcr=timestamp_pcrscr_get();
    last_checkin_minpts=tsync_pcr_get_min_checkinpts();
    cur_apts=timestamp_apts_get();
    cur_vpts=timestamp_vpts_get();

    if (tsync_pcr_reset_flag == 0) {
        // check whether it need reset
        if (tsync_pcr_lastcheckin_apts == last_checkin_apts && tsync_pcr_lastcheckin_vpts == last_checkin_vpts) {
            ++tsync_pcr_pausecheck_cnt;
            if (tsync_pcr_pausecheck_cnt > 50) {
                printk("[tsync_pcr_check]reset abuf_level=%x vbuf_level=%x stream_delta=%lld apts=%llx vpts=%llx\n",
                    abuf_level,vbuf_level,tsync_pcr_stream_delta, last_checkin_apts,last_checkin_vpts);
                tsync_pcr_reset_flag=1;
                return res;
            }
            //else if (290 < tsync_pcr_pausecheck_cnt && tsync_pcr_pausecheck_cnt < 300) {
                //printk("[tsync_pcr_check] abuf_level=%x vbuf_level=%x apts=%llx vpts=%llx cnt=%d\n",
                    //abuf_level,vbuf_level, last_checkin_apts,last_checkin_vpts,tsync_pcr_pausecheck_cnt);
            //}
        }
        else{
            tsync_pcr_lastcheckin_apts = last_checkin_apts;
            tsync_pcr_lastcheckin_vpts= last_checkin_vpts;
            tsync_pcr_pausecheck_cnt=0;
        }

        // check whether audio and video can sync
        if (last_cur_pcr-cur_apts > MAX_SYNC_AGAP_TIME &&
            abs(cur_apts-cur_vpts) > MAX_SYNC_AGAP_TIME &&
            last_checkin_apts > 0 && last_checkin_apts - last_cur_pcr < MIN_SYNC_ACHACH_TIME &&
            !(tsync_pcr_tsdemuxpcr_discontinue & PCR_DISCONTINUE)) {
            tsync_pcr_asynccheck_cnt++;
        }
        else
            tsync_pcr_asynccheck_cnt=0;

        if (last_cur_pcr - cur_vpts > MAX_SYNC_VGAP_TIME &&
            abs(cur_apts - cur_vpts) > MAX_SYNC_VGAP_TIME &&
            last_checkin_vpts > 0 && last_checkin_vpts - last_cur_pcr < MIN_SYNC_VCHACH_TIME &&
            !(tsync_pcr_tsdemuxpcr_discontinue & PCR_DISCONTINUE)) {
            tsync_pcr_vsynccheck_cnt++;
        }
        else
            tsync_pcr_vsynccheck_cnt=0;

        if (tsync_pcr_vsynccheck_cnt > 100 && last_checkin_minpts > 0) {
            int64_t  new_pcr =  last_checkin_minpts-tsync_pcr_ref_latency*2;
            int64_t  old_pcr=timestamp_pcrscr_get();
            timestamp_pcrscr_set(new_pcr);
            printk("[tsync_pcr_check] video stream underrun,force slow play. apts=%d vpts=%d pcr=%lld new_pcr=%lld checkin_apts=%lld checkin_vpts=%lld\n",
                cur_apts,cur_vpts,old_pcr,new_pcr,last_checkin_apts,last_checkin_vpts);
        }
        else if (tsync_pcr_asynccheck_cnt > 100 && last_checkin_minpts > 0) {
            int64_t  new_pcr =  last_checkin_minpts-tsync_pcr_ref_latency*2;
            int64_t  old_pcr=timestamp_pcrscr_get();
            timestamp_pcrscr_set(new_pcr);
            printk("[tsync_pcr_check] audio stream underrun,force slow play. apts=%d vpts=%d pcr=%lld new_pcr=%lld checkin_apts=%lld checkin_vpts=%lld\n",
                cur_apts,cur_vpts,old_pcr,new_pcr,last_checkin_apts,last_checkin_vpts);
        }
    }

    if (((vbuf_level * 3 > vbuf_size * 2 && vbuf_size > 0) || (abuf_level * 3 > abuf_size * 2 && abuf_size > 0)) && play_mode != PLAY_MODE_FORCE_SPEED) {
        // the stream buffer have too much data. speed out
        printk("[tsync_pcr_check]Buffer will overflow and speed play. vlevel=%x vsize=%x alevel=%x asize=%x play_mode=%d\n",
            vbuf_level,vbuf_size,abuf_level,abuf_size, play_mode);
        play_mode=PLAY_MODE_FORCE_SPEED;
    }
    else if ((vbuf_level * 5 > vbuf_size * 4 && vbuf_size > 0) || (abuf_level * 5 > abuf_size * 4 && abuf_size > 0)) {
        // the stream buffer will happen overflow. quikly speed out
        u32 new_pcr=0;
        printk("[tsync_pcr_check]Buffer will overflow and fast speed play. vlevel=%x vsize=%x alevel=%x asize=%x play_mode=%d\n",
            vbuf_level,vbuf_size,abuf_level,abuf_size, play_mode);
        play_mode=PLAY_MODE_FORCE_SPEED;
        new_pcr=timestamp_pcrscr_get()+72000;		// 90000*0.8
        if (new_pcr < timestamp_pcrscr_get())
            printk("[%s]set the system pts now system 0x%x  pcr 0x%x!\n",__func__,timestamp_pcrscr_get(),new_pcr);
        timestamp_pcrscr_set(new_pcr);
        printk("[tsync_pcr_check]set new pcr =%x",new_pcr);
    }

    if (play_mode == PLAY_MODE_FORCE_SLOW) {
        if ((vbuf_level * 50 > vbuf_size && abuf_level * 50 > abuf_size && vbuf_size > 0 && abuf_size > 0) ||
            (vbuf_level * 20 > vbuf_size && vbuf_size > 0) ||
            (abuf_level * 20 > abuf_size && abuf_size > 0)) {
            play_mode=PLAY_MODE_NORMAL;
            printk("[tsync_pcr_check]Buffer to vlevel=%x vsize=%x alevel=%x asize=%x. slow to normal play\n",
                vbuf_level,vbuf_size,abuf_level,abuf_size);
        }
    }
    else if (play_mode == PLAY_MODE_FORCE_SPEED) {
        if ((vbuf_size > 0 && vbuf_level * 4 < vbuf_size && abuf_size > 0 && abuf_level * 4 < abuf_size) ||
            (vbuf_size > 0 && vbuf_level * 4 < vbuf_size && abuf_level == 0)||
            (abuf_size > 0 && abuf_level * 4 < abuf_size && vbuf_level == 0)) {
            play_mode=PLAY_MODE_NORMAL;
            tsync_pcr_tsdemux_startpcr = tsdemux_pcr;
            tsync_pcr_system_startpcr = timestamp_pcrscr_get();
            printk("[tsync_pcr_check]Buffer to vlevel=%x vsize=%x alevel=%x asize=%x. speed to normal play\n",
                vbuf_level,vbuf_size,abuf_level,abuf_size);
        }
    }
/*
    tsync_pcr_debug_pcrscr++;
    if(tsync_pcr_debug_pcrscr>=100){    	
        printk("[tsync_pcr_check]debug pcr=%x,refer lock=%x, vpts =%x, apts=%x\n",pcr,tsdemux_pcr,timestamp_vpts_get(),timestamp_apts_get());
        tsync_pcr_debug_pcrscr=0;
    }
*/

    //if(need_recovery==1 || play_mode == PLAY_MODE_FORCE_SLOW || play_mode == PLAY_MODE_FORCE_SPEED){
    /* To check the system time with ts demuxer pcr */
    if ((play_mode != PLAY_MODE_FORCE_SLOW) && (play_mode != PLAY_MODE_FORCE_SPEED) && (tsync_pcr_usepcr == 1) && tsync_pcr_stream_delta != 0) {
        // use the pcr to adjust
        //u32 ref_pcr=tsdemux_pcr-calculation_vcached_delayed();
        u64 ref_pcr=(u64)tsdemux_pcr;//(int64_t)tsdemux_pcr- (int64_t)tsync_pcr_ref_latency;
        u64 cur_pcr=(u64)timestamp_pcrscr_get();
        int64_t cur_delta =  (int64_t)ref_pcr - (int64_t)cur_pcr;
        int64_t diff=(cur_delta -tsync_pcr_stream_delta);

        //if(diff > OPEN_RECOVERY_THRESHOLD && cur_pcr<ref_pcr && play_mode!=PLAY_MODE_SPEED && need_recovery){
        if ((diff > (tsync_pcr_max_cache_time))  && (play_mode != PLAY_MODE_SPEED) && need_recovery) {
            play_mode=PLAY_MODE_SPEED;
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%lld to speed play  \n",diff);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] ref_pcr=%lld to speed play  \n",ref_pcr);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_pcr=%lld to speed play  \n",cur_pcr);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_delta=%lld to speed play  \n",cur_delta);
        }
        //else if(diff > OPEN_RECOVERY_THRESHOLD && cur_pcr>ref_pcr && play_mode!=PLAY_MODE_SLOW && need_recovery){
        else if(diff< (tsync_pcr_min_cache_time) && (play_mode!=PLAY_MODE_SLOW) && need_recovery){
            play_mode=PLAY_MODE_SLOW;
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%lld to slow play  \n",diff);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] ref_pcr=%lld to slow play  \n",ref_pcr);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_pcr=%lld to slow play  \n",cur_pcr);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_delta=%lld to slow play  \n",cur_delta);
        }
        //else if(diff < CLOSE_RECOVERY_THRESHOLD && play_mode!=PLAY_MODE_NORMAL){
        else if((!need_recovery||((tsync_pcr_down_cache_time<diff)&&(diff<tsync_pcr_up_cache_time)))&&(play_mode!=PLAY_MODE_NORMAL)){
            play_mode=PLAY_MODE_NORMAL;
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] ref_pcr=%lld to nomal play  \n",ref_pcr);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_pcr=%lld to nomal play  \n",cur_pcr);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] cur_delta=%lld to nomal play  \n",cur_delta);
            amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] diff=%lld,need_recovery=%d to nomal play  \n",diff,need_recovery);
        }
    }
    else if((play_mode != PLAY_MODE_FORCE_SLOW) && (play_mode != PLAY_MODE_FORCE_SPEED) && (tsync_pcr_usepcr==0)){
        // use the min cache time to adjust
        int min_cache_time = get_min_cache_delay();
        if (min_cache_time > tsync_pcr_max_delay_time) {
            if (play_mode != PLAY_MODE_SPEED) {
                play_mode=PLAY_MODE_SPEED;
                amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] min_cache_time=%d to speed play  \n",min_cache_time);
            }
        }
        else if (min_cache_time < tsync_pcr_min_delay_time && min_cache_time>0 ) {
            if (play_mode != PLAY_MODE_SLOW) {
                play_mode=PLAY_MODE_SLOW;
                amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] min_cache_time=%d to show play  \n",min_cache_time);
            }
        }
        else{
            if (tsync_pcr_down_delay_time <= min_cache_time && min_cache_time <= tsync_pcr_up_delay_time && play_mode != PLAY_MODE_NORMAL) {
                play_mode=PLAY_MODE_NORMAL;
                amlog_level(LOG_LEVEL_INFO, "[tsync_pcr_check] min_cache_time=%d to nomal play  \n", min_cache_time);
            }
        }
    }
 
    if (need_recovery && !tsync_pcr_vpause_flag) {
        if (play_mode == PLAY_MODE_SLOW)
            timestamp_pcrscr_set(timestamp_pcrscr_get()-tsync_pcr_recovery_span);
        else if ( play_mode == PLAY_MODE_FORCE_SLOW)
            timestamp_pcrscr_set(timestamp_pcrscr_get()-FORCE_RECOVERY_SPAN);
        else if (play_mode == PLAY_MODE_SPEED)
            timestamp_pcrscr_set(timestamp_pcrscr_get()+tsync_pcr_recovery_span);
        else if ( play_mode == PLAY_MODE_FORCE_SPEED)
            timestamp_pcrscr_set(timestamp_pcrscr_get()+FORCE_RECOVERY_SPAN);
    }
    //}

    return res;
}

static void tsync_pcr_check_timer_func(unsigned long arg)
{
    ulong flags;
    spin_lock_irqsave(&tsync_pcr_lock, flags);
    tsync_pcr_check();
    spin_unlock_irqrestore(&tsync_pcr_lock, flags);

    tsync_pcr_check_timer.expires = jiffies+TEN_MS_INTERVAL;

    add_timer(&tsync_pcr_check_timer);
}

static void tsync_pcr_param_reset(void){
    tsync_pcr_system_startpcr=0;
    tsync_pcr_tsdemux_startpcr=0;

    tsync_pcr_vpause_flag = 0;
    tsync_pcr_apause_flag = 0;
    tsync_pcr_vstart_flag = 0;
    tsync_pcr_astart_flag = 0;
    tsync_pcr_inited_flag = 0;
    wait_pcr_count = 0;
    tsync_pcr_reset_flag=0;
    tsync_pcr_asynccheck_cnt=0;
    tsync_pcr_vsynccheck_cnt=0;
    printk("wait_pcr_count = 0 \n");

    tsync_pcr_last_tsdemuxpcr = 0;
    tsync_pcr_discontinue_local_point=0;
    tsync_pcr_discontinue_point = 0;
    tsync_pcr_discontinue_waited = 0;                                               // the time waited the v-discontinue to happen
    tsync_pcr_tsdemuxpcr_discontinue = 0;                                       // the boolean value

    abuf_level=0;
    abuf_size=0;
    vbuf_level=0;
    vbuf_size=0;
    play_mode=PLAY_MODE_NORMAL;
    tsync_pcr_started=0;
    tsync_pcr_stream_delta=0;
}
int tsync_pcr_set_apts(unsigned pts)
{
    timestamp_apts_set(pts);
    //printk("[tsync_pcr_set_apts]set apts=%x",pts);
    return 0;
}
int tsync_pcr_start(void)
{
    timestamp_pcrscr_enable(0);
    timestamp_pcrscr_set(0);
    
    tsync_pcr_param_reset();
    if (tsync_get_mode() == TSYNC_MODE_PCRMASTER) {
        printk("[tsync_pcr_start]PCRMASTER started success. \n");
        init_timer(&tsync_pcr_check_timer);

        tsync_pcr_check_timer.function = tsync_pcr_check_timer_func;
        tsync_pcr_check_timer.expires = jiffies;

        first_time_record = (jiffies * TIME_UNIT90K) / HZ;
        tsync_pcr_started=1;
        if (tsdemux_pcrscr_valid() == 0) {
            tsync_pcr_usepcr = 0;
            tsync_pcr_inited_mode = INIT_PRIORITY_AUDIO;
        }
        else {
            tsync_pcr_usepcr = 1;
            tsync_pcr_inited_mode = INIT_PRIORITY_PCR;
        }
        tsync_pcr_read_cnt=0;
        printk("[tsync_pcr_start]usepcr=%d tsync_pcr_inited_mode=%d\n", tsync_pcr_usepcr, tsync_pcr_inited_mode);
        add_timer(&tsync_pcr_check_timer);
    }
    return 0;
}

void tsync_pcr_stop(void)
{
    if (tsync_pcr_started == 1) {
        del_timer_sync(&tsync_pcr_check_timer);
        printk("[tsync_pcr_stop]PCRMASTER stop success. \n");
    }
    tsync_pcr_freerun_mode=0;
    tsync_pcr_started=0;
}

// --------------------------------------------------------------------------------
// define of tsync pcr class node

static ssize_t show_play_mode(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d\n", play_mode);
}

static ssize_t show_tsync_pcr_dispoint(struct class *class,
                           struct class_attribute *attr,
                           char *buf)
{
    printk("[%s:%d] tsync_pcr_discontinue_point:%x, HZ:%x, \n", __FUNCTION__, __LINE__, tsync_pcr_discontinue_point, HZ);
    return sprintf(buf, "0x%x\n", tsync_pcr_discontinue_point);
}

static ssize_t store_tsync_pcr_dispoint(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned pts;
    ssize_t r;

    r = sscanf(buf, "0x%x", &pts);
    if (r != 1) {
        return -EINVAL;
    }

    tsync_pcr_discontinue_point = pts;
    printk("[%s:%d] tsync_pcr_discontinue_point:%x, \n", __FUNCTION__, __LINE__, tsync_pcr_discontinue_point);

    return size;
}

static ssize_t store_tsync_pcr_audio_resample_type(struct class *class,
                            struct class_attribute *attr,
                            const char *buf,
                            size_t size)
{
    unsigned type;
    ssize_t r;

    r = sscanf(buf, "%d", &type);
    if (r != 1) {
        return -EINVAL;
    }

    if(type==RESAMPLE_DOWN_FORCE_PCR_SLOW){
        play_mode=PLAY_MODE_SLOW;
        printk("[%s:%d] Audio to FORCE_PCR_SLOW\n", __FUNCTION__, __LINE__);
    }
    return size;
}

static ssize_t tsync_pcr_freerun_mode_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", tsync_pcr_freerun_mode);
}

static ssize_t tsync_pcr_freerun_mode_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                   size_t count)
{
    size_t r;

    r = sscanf(buf, "%d", &tsync_pcr_freerun_mode);

    printk("%s(%d)\n", __func__, tsync_pcr_freerun_mode);

    if (r != 1) {
        return -EINVAL;
    }

    return count;
}

static ssize_t show_reset_flag(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d\n", tsync_pcr_reset_flag);
}

static ssize_t show_apause_flag(struct class *class,
                         struct class_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d\n", tsync_pcr_apause_flag);
}

static ssize_t tsync_pcr_recovery_span_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", tsync_pcr_recovery_span);
}

static ssize_t tsync_pcr_recovery_span_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                   size_t count)
{
    size_t r;

    r = sscanf(buf, "%d", &tsync_pcr_recovery_span);

    printk("%s(%d)\n", __func__, tsync_pcr_recovery_span);

    if (r != 1) {
        return -EINVAL;
    }

    return count;
}

// --------------------------------------------------------------------------------
// define of tsync pcr module

static struct class_attribute tsync_pcr_class_attrs[] = {
    __ATTR(play_mode,  S_IRUGO | S_IWUSR | S_IWGRP, show_play_mode, NULL),
    __ATTR(tsync_pcr_discontinue_point, S_IRUGO | S_IWUSR, show_tsync_pcr_dispoint,  store_tsync_pcr_dispoint),
    __ATTR(audio_resample_type, S_IRUGO | S_IWUSR, NULL,  store_tsync_pcr_audio_resample_type),
    __ATTR(tsync_pcr_freerun_mode, S_IRUGO | S_IWUSR, tsync_pcr_freerun_mode_show, tsync_pcr_freerun_mode_store),
    __ATTR(tsync_pcr_reset_flag,  S_IRUGO | S_IWUSR | S_IWGRP, show_reset_flag, NULL),
    __ATTR(tsync_pcr_apause_flag,  S_IRUGO | S_IWUSR | S_IWGRP, show_apause_flag, NULL),
    __ATTR(tsync_pcr_recovery_span, S_IRUGO | S_IWUSR, tsync_pcr_recovery_span_show, tsync_pcr_recovery_span_store),
    __ATTR_NULL
};
static struct class tsync_pcr_class = {
        .name = "tsync_pcr",
        .class_attrs = tsync_pcr_class_attrs,
    };

static int __init tsync_pcr_init(void)
{
    int r;

    r = class_register(&tsync_pcr_class);

    if (r) {
        printk("[tsync_pcr_init]tsync_pcr_class create fail.  \n");
        return r;
    }

    /* init audio pts to -1, others to 0 */
    timestamp_apts_set(-1);
    timestamp_vpts_set(0);
    timestamp_pcrscr_set(0);
    wait_pcr_count = 0;
    printk("[tsync_pcr_init]init success. \n");
    return (0);
}

static void __exit tsync_pcr_exit(void)
{
    class_unregister(&tsync_pcr_class);
    printk("[tsync_pcr_exit]exit success.   \n");
}


module_init(tsync_pcr_init);
module_exit(tsync_pcr_exit);

MODULE_DESCRIPTION("AMLOGIC time sync management driver of referrence by pcrscr");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("le yang <le.yang@amlogic.com>");
