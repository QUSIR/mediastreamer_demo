#ifndef __MS_ICAMREC_H__
#define	__MS_ICAMREC_H__

#include "msfilter.h"
#include "msvideo.h"


#define	ICAM_AUDIO_FRAME_TYPE		0
#define	ICAM_VIDEO_FRAME_TYPE		1
#define	ICAM_RECORD_FILE_DIR		"icamrecfiles"
#define ICAM_ALARM_REC_SUB_DIR		"Alarm"
#define ICAM_VIDEO_REC_SUB_DIR		"Video"
#define	ICAM_ALARM_REC_TYPE			1
#define	ICAM_VIDEO_REC_TYPE			2
#define ICAM_DIR_ACCESS_MODE 		0755 //(S_IRWXU | S_IRWXG | S_IRWXO)
#define	UDPMAXSIZE					1400
#define	WRAN_REC_FILE_TIMES_LEN		(30)



#define MS_ICAM_AUDIO_REC_SET_FUNC		MS_FILTER_METHOD(MS_ICAM_AUDIO_REC_ID, 0, void*)
#define MS_ICAM_VIDEO_REC_SET_FUNC		MS_FILTER_METHOD(MS_ICAM_VIDEO_REC_ID, 0, void*)

#define MS_ICAM_EXT_INPUT_PUT_DATA		MS_FILTER_METHOD(MS_ICAM_EXT_INPUT_ID, 0, void*)
#define	MS_ICAM_EXT_INPUT_QUEUE_EMPTY	MS_FILTER_METHOD_NO_ARG(MS_ICAM_EXT_INPUT_ID, 1)





#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SilkData
{
	mblk_t *im;
	uint32_t curr_frame_time_ms;
	struct _SilkData *next;
}SilkData;

typedef struct _H264Data
{
	mblk_t *im;
	uint32_t curr_frame_time_ms;
	struct _H264Data *next;
}H264Data;

typedef struct _IndexInfo
{
	uint32_t offset;
	uint16_t timestamp;
	uint16_t framesize;
}IndexInfo;		//索引数据结构

typedef struct _IcamDayTime
{ 
	uint16_t year; 
	uint16_t month; 
	uint16_t day; 
	uint16_t hour; 
	uint16_t minute; 
	uint16_t second; 
}IcamDayTime;

typedef struct _RecFileHead
{
	uint32_t checknum;				//起始标志0xabcd1234
	uint32_t play_times;			//时长
	uint32_t index_space;			//索引起始位置
	uint32_t index_sum;				//索引数量
	IcamDayTime rec_time;			//报警时间或录像时间
	uint32_t alarm_tick;
	uint16_t video_sample_rate;		//视频录像的比特率
	uint16_t video_code;			//视频编码类型(2)
	uint16_t video_width;			//视频宽度
	uint16_t video_height;			//视频高度
	
	uint16_t audio_code;			//音频编码类型(5)
	uint16_t audio_sample_rate;		//音频采样频率
	uint16_t audio_channel;			//音频频道数量
	uint16_t audio_bits_per_sample;	//音频比特率
}RecFileHead;	//文件起始数据说明 

typedef	struct  _FrameHead
{
	uint32_t check;			//起始标志0xabcd1234
	uint32_t time;			//播放时间
	uint8_t  key;			//键值(暂时没有使用)
	uint8_t  type;			//帧的类型(1:视频帧，2:音频帧)
	uint16_t length;		//帧的长度
}FrameHead;		//的起始


typedef void (*icam_rec_write_func)(const unsigned char *, int , int, uint64_t);

int icam_rec_data_init(const char *save_path, int type);
void icam_rec_data_uninit();
void icam_rec_write_file(const unsigned char *data, int size, int type, uint64_t frame_play_time);
#ifdef __cplusplus
}
#endif

#endif
