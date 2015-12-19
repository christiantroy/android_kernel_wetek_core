#ifndef __HDMI_COMMON_H__
#define __HDMI_COMMON_H__

// HDMI VIC definitions
typedef enum HDMI_Video_Type_ {
// Refer to CEA 861-D
    HDMI_Unkown = 0,
    HDMI_640x480p60_4x3 = 1,
    HDMI_720x480p60_4x3 = 2,
    HDMI_720x480p60_16x9 = 3,
    HDMI_1280x720p60_16x9 = 4,
    HDMI_1920x1080i60_16x9 = 5,
    HDMI_720x480i60_4x3 = 6,
    HDMI_720x480i60_16x9 = 7,
    HDMI_720x240p60_4x3 = 8,
    HDMI_720x240p60_16x9 = 9,
    HDMI_2880x480i60_4x3 = 10,
    HDMI_2880x480i60_16x9 = 11,
    HDMI_2880x240p60_4x3 = 12,
    HDMI_2880x240p60_16x9 = 13,
    HDMI_1440x480p60_4x3 = 14,
    HDMI_1440x480p60_16x9 = 15,
    HDMI_1920x1080p60_16x9 = 16,
    HDMI_720x576p50_4x3 = 17,
    HDMI_720x576p50_16x9 = 18,
    HDMI_1280x720p50_16x9 = 19,
    HDMI_1920x1080i50_16x9 = 20,
    HDMI_720x576i50_4x3 = 21,
    HDMI_720x576i50_16x9 = 22,
    HDMI_720x288p_4x3 = 23,
    HDMI_720x288p_16x9 = 24,
    HDMI_2880x576i50_4x3 = 25,
    HDMI_2880x576i50_16x9 = 26,
    HDMI_2880x288p50_4x3 = 27,
    HDMI_2880x288p50_16x9 = 28,
    HDMI_1440x576p_4x3 = 29,
    HDMI_1440x576p_16x9 = 30,
    HDMI_1920x1080p50_16x9 = 31,
    HDMI_1920x1080p24_16x9 = 32,
    HDMI_1920x1080p25_16x9 = 33,
    HDMI_1920x1080p30_16x9 = 34,
    HDMI_2880x480p60_4x3 = 35,
    HDMI_2880x480p60_16x9 = 36,
    HDMI_2880x576p50_4x3 = 37,
    HDMI_2880x576p50_16x9 = 38,
    HDMI_1920x1080i_t1250_50_16x9 = 39,
    HDMI_1920x1080i100_16x9 = 40,
    HDMI_1280x720p100_16x9 = 41,
    HDMI_720x576p100_4x3 = 42,
    HDMI_720x576p100_16x9 = 43,
    HDMI_720x576i100_4x3 = 44,
    HDMI_720x576i100_16x9 = 45,
    HDMI_1920x1080i120_16x9 = 46,
    HDMI_1280x720p120_16x9 = 47,
    HDMI_720x480p120_4x3 = 48,
    HDMI_720x480p120_16x9 = 49,
    HDMI_720X480i120_4x3 = 50,
    HDMI_720X480i120_16x9 = 51,
    HDMI_720x576p200_4x3 = 52,
    HDMI_720x576p200_16x9 = 53,
    HDMI_720x576i200_4x3 = 54,
    HDMI_720x576i200_16x9 = 55,
    HDMI_720x480p240_4x3 = 56,
    HDMI_720x480p240_16x9 = 57,
    HDMI_720x480i240_4x3 = 58,
    HDMI_720x480i240_16x9 = 59,
// Refet to CEA 861-F
    HDMI_1280x720p24_16x9 = 60,
    HDMI_1280x720p25_16x9 = 61,
    HDMI_1280x720p30_16x9 = 62,
    HDMI_1920x1080p120_16x9 = 63,
    HDMI_1920x1080p100_16x9 = 64,
    HDMI_1280x720p24_64x27 = 65,
    HDMI_1280x720p25_64x27 = 66,
    HDMI_1280x720p30_64x27 = 67,
    HDMI_1280x720p50_64x27 = 68,
    HDMI_1280x720p60_64x27 = 69,
    HDMI_1280x720p100_64x27 = 70,
    HDMI_1280x720p120_64x27 = 71,
    HDMI_1920x1080p24_64x27 = 72,
    HDMI_1920x1080p25_64x27 = 73,
    HDMI_1920x1080p30_64x27 = 74,
    HDMI_1920x1080p50_64x27 = 75,
    HDMI_1920x1080p60_64x27 = 76,
    HDMI_1920x1080p100_64x27 = 77,
    HDMI_1920x1080p120_64x27 = 78,
    HDMI_1680x720p24_64x27 = 79,
    HDMI_1680x720p25_64x27 = 80,
    HDMI_1680x720p30_64x27 = 81,
    HDMI_1680x720p50_64x27 = 82,
    HDMI_1680x720p60_64x27 = 83,
    HDMI_1680x720p100_64x27 = 84,
    HDMI_1680x720p120_64x27 = 85,
    HDMI_2560x1080p24_64x27 = 86,
    HDMI_2560x1080p25_64x27 = 87,
    HDMI_2560x1080p30_64x27 = 88,
    HDMI_2560x1080p50_64x27 = 89,
    HDMI_2560x1080p60_64x27 = 90,
    HDMI_2560x1080p100_64x27 = 91,
    HDMI_2560x1080p120_64x27 = 92,
    HDMI_3840x2160p24_16x9 = 93,
    HDMI_3840x2160p25_16x9 = 94,
    HDMI_3840x2160p30_16x9 = 95,
    HDMI_3840x2160p50_16x9 = 96,
    HDMI_3840x2160p60_16x9 = 97,
    HDMI_4096x2160p24_256x135 = 98,
    HDMI_4096x2160p25_256x135 = 99,
    HDMI_4096x2160p30_256x135 = 100,
    HDMI_4096x2160p50_256x135 = 101,
    HDMI_4096x2160p60_256x135 = 102,
    HDMI_3840x2160p24_64x27 = 103,
    HDMI_3840x2160p25_64x27 = 104,
    HDMI_3840x2160p30_64x27 = 105,
    HDMI_3840x2160p50_64x27 = 106,
    HDMI_3840x2160p60_64x27 = 107,
    HDMI_RESERVED = 108,
    HDMI_3840x1080p120hz = 109,
    HDMI_3840x1080p100hz,
    HDMI_3840x540p240hz,
    HDMI_3840x540p200hz,
} HDMI_Video_Codes_t;

// Compliance with old definitions
#define HDMI_640x480p60         HDMI_640x480p60_4x3
#define HDMI_480p60             HDMI_720x480p60_4x3
#define HDMI_480p60_16x9        HDMI_720x480p60_16x9
#define HDMI_720p60             HDMI_1280x720p60_16x9
#define HDMI_1080i60            HDMI_1920x1080i60_16x9
#define HDMI_480i60             HDMI_720x480i60_4x3
#define HDMI_480i60_16x9        HDMI_720x480i60_16x9
#define HDMI_480i60_16x9_rpt    HDMI_2880x480i60_16x9
#define HDMI_1440x480p60        HDMI_1440x480p60_4x3
#define HDMI_1440x480p60_16x9   HDMI_1440x480p60_16x9
#define HDMI_1080p60            HDMI_1920x1080p60_16x9
#define HDMI_576p50             HDMI_720x576p50_4x3
#define HDMI_576p50_16x9        HDMI_720x576p50_16x9
#define HDMI_720p50             HDMI_1280x720p50_16x9
#define HDMI_1080i50            HDMI_1920x1080i50_16x9
#define HDMI_576i50             HDMI_720x576i50_4x3
#define HDMI_576i50_16x9        HDMI_720x576i50_16x9
#define HDMI_576i50_16x9_rpt    HDMI_2880x576i50_16x9
#define HDMI_1080p50            HDMI_1920x1080p50_16x9
#define HDMI_1080p24            HDMI_1920x1080p24_16x9
#define HDMI_1080p25            HDMI_1920x1080p25_16x9
#define HDMI_1080p30            HDMI_1920x1080p30_16x9
#define HDMI_480p60_16x9_rpt    HDMI_2880x480p60_16x9
#define HDMI_576p50_16x9_rpt    HDMI_2880x576p50_16x9
#define HDMI_4k2k_24            HDMI_3840x2160p24_16x9
#define HDMI_4k2k_25            HDMI_3840x2160p25_16x9
#define HDMI_4k2k_30            HDMI_3840x2160p30_16x9
#define HDMI_4k2k_50            HDMI_3840x2160p50_16x9
#define HDMI_4k2k_60            HDMI_3840x2160p60_16x9
#define HDMI_4k2k_smpte_24      HDMI_4096x2160p24_256x135
#define HDMI_4k2k_smpte_50      HDMI_4096x2160p50_256x135
#define HDMI_4k2k_smpte_60      HDMI_4096x2160p60_256x135

// CEA TIMING STRUCT DEFINITION
struct hdmi_cea_timing {
    unsigned int pixel_freq;            // Unit: 1000
    unsigned int h_freq;              // Unit: Hz
    unsigned int v_freq;              // Unit: 0.001 Hz
    unsigned int vsync_polarity : 1;    // 1: positive  0: negative
    unsigned int hsync_polarity : 1;
    unsigned short h_active;
    unsigned short h_total;
    unsigned short h_blank;
    unsigned short h_front;
    unsigned short h_sync;
    unsigned short h_back;
    unsigned short v_active;
    unsigned short v_total;
    unsigned short v_blank;
    unsigned short v_front;
    unsigned short v_sync;
    unsigned short v_back;
    unsigned short v_sync_ln;
};

// get hdmi cea timing
// t: struct hdmi_cea_timing *
#define GET_TIMING(name)      (t->name)

struct hdmi_format_para {
    HDMI_Video_Codes_t vic;
    unsigned char * name;
    unsigned int pixel_repetition_factor;
    unsigned int progress_mode : 1;         // 0: Interlace mode  1: Progressive Mode
    unsigned int scrambler_en : 1;
    unsigned int tmds_clk_div40 : 1;
    unsigned int tmds_clk;            // Unit: 1000
    struct hdmi_cea_timing timing;
};

// HDMI Packet Type Definitions
#define PT_NULL_PKT                 0x00
#define PT_AUD_CLK_REGENERATION     0x01
#define PT_AUD_SAMPLE               0x02
#define PT_GENERAL_CONTROL          0x03
#define PT_ACP                      0x04
#define PT_ISRC1                    0x05
#define PT_ISRC2                    0x06
#define PT_ONE_BIT_AUD_SAMPLE       0x07
#define PT_DST_AUD                  0x08
#define PT_HBR_AUD_STREAM           0x09
#define PT_GAMUT_METADATA           0x0A
#define PT_3D_AUD_SAMPLE            0x0B
#define PT_ONE_BIT_3D_AUD_SAMPLE    0x0C
#define PT_AUD_METADATA             0x0D
#define PT_MULTI_SREAM_AUD_SAMPLE   0x0E
#define PT_ONE_BIT_MULTI_SREAM_AUD_SAMPLE   0x0F
// Infoframe Packet
#define PT_IF_VENDOR_SEPCIFIC       0x81
#define PT_IF_AVI                   0x82
#define PT_IF_SPD                   0x83
#define PT_IF_AUD                   0x84
#define PT_IF_MPEG_SOURCE           0x85

// Old definitions
#define TYPE_AVI_INFOFRAMES       0x82
#define AVI_INFOFRAMES_VERSION    0x02
#define AVI_INFOFRAMES_LENGTH     0x0D

enum hdmi_color_depth {
    HDMI_COLOR_DEPTH_24B = 4,
    HDMI_COLOR_DEPTH_30B = 5,
    HDMI_COLOR_DEPTH_36B = 6,
    HDMI_COLOR_DEPTH_48B = 7,
};

enum hdmi_color_format {
    HDMI_COLOR_FORMAT_RGB,
    HDMI_COLOR_FORMAT_444,
    HDMI_COLOR_FORMAT_422,
    HDMI_COLOR_FORMAT_420,
};

enum hdmi_color_range {
    HDMI_COLOR_RANGE_LIM,
    HDMI_COLOR_RANGE_FUL,
};

enum hdmi_audio_packet {
    HDMI_AUDIO_PACKET_SMP = 0x02,
    HDMI_AUDIO_PACKET_1BT = 0x07,
    HDMI_AUDIO_PACKET_DST = 0x08,
    HDMI_AUDIO_PACKET_HBR = 0x09,
};

typedef enum
{
    COLOR_SPACE_RGB444 = 0,
    COLOR_SPACE_YUV422 = 1,
    COLOR_SPACE_YUV444 = 2,
    COLOR_SPACE_YUV420 = 3,
    COLOR_SPACE_RESERVED,
}color_space_type_t;

typedef enum
{
    ASPECT_RATIO_SAME_AS_SOURCE = 0x8,
    TV_ASPECT_RATIO_4_3 = 0x9,
    TV_ASPECT_RATIO_16_9 = 0xA,
    TV_ASPECT_RATIO_14_9 = 0xB,
    TV_ASPECT_RATIO_MAX
} hdmi_aspect_ratio_t;

typedef enum
{
    COLOR_24BIT = 0,
    COLOR_30BIT,
    COLOR_36BIT,
    COLOR_48BIT
} hdmi_color_depth_t;


struct hdmi_format_para * hdmi_get_fmt_paras(HDMI_Video_Codes_t vic);
void check_detail_fmt(void);


// HDMI Audio Parmeters
typedef enum
{
    CT_REFER_TO_STREAM = 0,
    CT_PCM,
    CT_AC_3,
    CT_MPEG1,
    CT_MP3,
    CT_MPEG2,
    CT_AAC,
    CT_DTS,
    CT_ATRAC,
    CT_ONE_BIT_AUDIO,
    CT_DOLBY_D,
    CT_DTS_HD,
    CT_MAT,
    CT_DST,
    CT_WMA,
    CT_MAX,
} audio_type_t;

typedef enum
{
    CC_REFER_TO_STREAM = 0,
    CC_2CH,
    CC_3CH,
    CC_4CH,
    CC_5CH,
    CC_6CH,
    CC_7CH,
    CC_8CH,
    CC_MAX_CH
} audio_channel_t;

typedef enum
{
    AF_SPDIF = 0,
    AF_I2S,
    AF_DSD,
    AF_HBR,
    AT_MAX
} audio_format_t;

typedef enum {
    SS_REFER_TO_STREAM = 0,
    SS_16BITS,
    SS_20BITS,
    SS_24BITS,
    SS_MAX
}audio_sample_size_t;

struct size_map_ss {
    unsigned int sample_bits;
    audio_sample_size_t ss;
};

//FL-- Front Left
//FC --Front Center
//FR --Front Right
//FLC-- Front Left Center
//FRC-- Front RiQhtCenter
//RL-- Rear Left
//RC --Rear Center
//RR-- Rear Right
//RLC-- Rear Left Center
//RRC --Rear RiQhtCenter
//LFE-- Low Frequency Effect
typedef enum {
    CA_FR_FL = 0,
    CA_LFE_FR_FL,
    CA_FC_FR_FL,
    CA_FC_LFE_FR_FL,

    CA_RC_FR_FL,
    CA_RC_LFE_FR_FL,
    CA_RC_FC_FR_FL,
    CA_RC_FC_LFE_FR_FL,

    CA_RR_RL_FR_FL,
    CA_RR_RL_LFE_FR_FL,
    CA_RR_RL_FC_FR_FL,
    CA_RR_RL_FC_LFE_FR_FL,

    CA_RC_RR_RL_FR_FL,
    CA_RC_RR_RL_LFE_FR_FL,
    CA_RC_RR_RL_FC_FR_FL,
    CA_RC_RR_RL_FC_LFE_FR_FL,

    CA_RRC_RC_RR_RL_FR_FL,
    CA_RRC_RC_RR_RL_LFE_FR_FL,
    CA_RRC_RC_RR_RL_FC_FR_FL,
    CA_RRC_RC_RR_RL_FC_LFE_FR_FL,

    CA_FRC_RLC_FR_FL,
    CA_FRC_RLC_LFE_FR_FL,
    CA_FRC_RLC_FC_FR_FL,
    CA_FRC_RLC_FC_LFE_FR_FL,

    CA_FRC_RLC_RC_FR_FL,
    CA_FRC_RLC_RC_LFE_FR_FL,
    CA_FRC_RLC_RC_FC_FR_FL,
    CA_FRC_RLC_RC_FC_LFE_FR_FL,

    CA_FRC_RLC_RR_RL_FR_FL,
    CA_FRC_RLC_RR_RL_LFE_FR_FL,
    CA_FRC_RLC_RR_RL_FC_FR_FL,
    CA_FRC_RLC_RR_RL_FC_LFE_FR_FL,
}speak_location_t;

typedef enum {
	LSV_0DB = 0,
        LSV_1DB,
        LSV_2DB,
        LSV_3DB,
        LSV_4DB,
        LSV_5DB,
        LSV_6DB,
        LSV_7DB,
        LSV_8DB,
        LSV_9DB,
        LSV_10DB,
        LSV_11DB,
        LSV_12DB,
        LSV_13DB,
        LSV_14DB,
        LSV_15DB,
}audio_down_mix_t;

typedef enum
{
	 STATE_AUDIO__MUTED         =  0,
	 STATE_AUDIO__REQUEST_AUDIO = 1,
	 STATE_AUDIO__AUDIO_READY   = 2,
	 STATE_AUDIO__ON            = 3,
}hdmi_rx_audio_state_t;

//Sampling Freq Fs:0 - Refer to Stream Header; 1 - 32KHz; 2 - 44.1KHz; 3 - 48KHz; 4 - 88.2KHz...
typedef enum {
    FS_REFER_TO_STREAM = 0,
    FS_32K   = 1,
    FS_44K1  = 2,
    FS_48K   = 3,
    FS_88K2  = 4,
    FS_96K   = 5,
    FS_176K4 = 6,
    FS_192K  = 7,
    FS_MAX,
}audio_fs_t;

struct rate_map_fs {
    unsigned int rate;
    audio_fs_t fs;
};

typedef struct
{
    audio_type_t type ;     //!< Signal decoding type -- TvAudioType
    audio_format_t format;
     audio_channel_t channels ; //!< active audio channels bit mask.
    audio_fs_t fs;     //!< Signal sample rate in Hz
    audio_sample_size_t ss;
    speak_location_t speak_loc;
    audio_down_mix_t lsv;
    unsigned N_value;
    unsigned CTS;
} Hdmi_rx_audio_info_t;

#define AUDIO_PARA_MAX_NUM       7
struct hdmi_audio_fs_fmt_n_cts {
    struct {
        unsigned int tmds_clk;
        unsigned int n;
        unsigned int cts;
    }array[AUDIO_PARA_MAX_NUM];
    unsigned int def_n;
};

#endif
