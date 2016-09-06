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
}IndexInfo;		//�������ݽṹ

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
	uint32_t checknum;				//��ʼ��־0xabcd1234
	uint32_t play_times;			//ʱ��
	uint32_t index_space;			//������ʼλ��
	uint32_t index_sum;				//��������
	IcamDayTime rec_time;			//����ʱ���¼��ʱ��
	uint32_t alarm_tick;
	uint16_t video_sample_rate;		//��Ƶ¼��ı�����
	uint16_t video_code;			//��Ƶ��������(2)
	uint16_t video_width;			//��Ƶ���
	uint16_t video_height;			//��Ƶ�߶�
	
	uint16_t audio_code;			//��Ƶ��������(5)
	uint16_t audio_sample_rate;		//��Ƶ����Ƶ��
	uint16_t audio_channel;			//��ƵƵ������
	uint16_t audio_bits_per_sample;	//��Ƶ������
}RecFileHead;	//�ļ���ʼ����˵�� 

typedef	struct  _FrameHead
{
	uint32_t check;			//��ʼ��־0xabcd1234
	uint32_t time;			//����ʱ��
	uint8_t  key;			//��ֵ(��ʱû��ʹ��)
	uint8_t  type;			//֡������(1:��Ƶ֡��2:��Ƶ֡)
	uint16_t length;		//֡�ĳ���
}FrameHead;		//����ʼ


typedef void (*icam_rec_write_func)(const unsigned char *, int , int, uint64_t);

int icam_rec_data_init(const char *save_path, int type);
void icam_rec_data_uninit();
void icam_rec_write_file(const unsigned char *data, int size, int type, uint64_t frame_play_time);
#ifdef __cplusplus
}
#endif

#endif
