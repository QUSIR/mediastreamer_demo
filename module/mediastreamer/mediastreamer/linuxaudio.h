#ifndef linuxvideo_h
#define linuxvideo_h

#include "msfilter.h"
#include "msticker.h"

#include "Service/Audio/linuxvoice.h"

typedef struct {
//	unsigned int	bits;
//	unsigned int	rate;
//	unsigned int	nchannels;
//	int		started;
//	ms_thread_t     thread_id;
//	ms_mutex_t		mutex;
//	int	buff_size; /*buffer size in bytes*/
//	unsigned char*	read_buff;
//	MSBufferizer 	rb;
//	int	read_chunk_size;
//	int framesize;
//	int outgran_ms;
//	int min_avail;
//	int64_t start_time;
////	int64_t read_samples;
//	MSTickerSynchronizer *ticker_synchronizer;
//	snd_pcm_t* read_handle;
	unsigned char* read_buff;
	ms_mutex_t mutex;
	snd_pcm_t* handle;
	MSBufferizer 	*rb;
	int isRunning;
	ms_thread_t threadid;
	int frames;
	int64_t start_time;
}ms_linux_sound_read_data;

typedef struct {
//	unsigned char* write_buff;
	MSBufferizer	*bufferizer;
	ms_mutex_t mutex;
	ms_cond_t cond;
	snd_pcm_t* handle;
	int isRunning;
	int isSleeping;
	ms_thread_t threadid;
	int frames;
}ms_linux_sound_write_data;

extern MSFilterDesc ms_linux_sound_read_desc;
extern MSFilterDesc ms_linux_sound_write_desc;

#endif
