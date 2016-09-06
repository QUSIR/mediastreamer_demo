/*
mediastreamer2 library - modular sound and video processing and streaming
Copyright (C) 2006  Simon MORLAT (simon.morlat@linphone.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/


#ifndef MEDIASTREAM_H
#define MEDIASTREAM_H

#include "msfilter.h"
#include "msticker.h"
#include "mssndcard.h"
#include "mswebcam.h"
#include "msvideo.h"
#include "bitratecontrol.h"
#include "qualityindicator.h"
#include "ortp/ortp.h"
#include "ortp/event.h"
#include "msicamrec.h"


#define PAYLOAD_TYPE_FLAG_CAN_RECV	PAYLOAD_TYPE_USER_FLAG_1
#define PAYLOAD_TYPE_FLAG_CAN_SEND	PAYLOAD_TYPE_USER_FLAG_2

#define TEE_LINK_CALL		0
#define	TEE_LINK_RECS		1


typedef enum EchoLimiterType{
	ELInactive,
	ELControlMic,
	ELControlFull
} EchoLimiterType;

struct _AudioStream
{
	MSTicker *ticker;
	RtpSession *session;
	MSFilter *soundread;
	MSFilter *soundwrite;
	MSFilter *encoder;
	MSFilter *decoder;
	MSFilter *rtprecv;
	MSFilter *rtpsend;
	MSFilter *dtmfgen;
	MSFilter *dtmfgen_rtp;
	MSFilter *ec;/*echo canceler*/
	MSFilter *volsend,*volrecv; /*MSVolumes*/
	MSFilter *read_resampler;
	MSFilter *write_resampler;
	MSFilter *equalizer;
	uint64_t last_packet_count;
	time_t last_packet_time;
	EchoLimiterType el_type; /*use echo limiter: two MSVolume, measured input level controlling local output level*/
	OrtpEvQueue *evq;
	MSAudioBitrateController *rc;
	MSQualityIndicator *qi;
	time_t start_time;
	bool_t play_dtmfs;
	bool_t use_gc;
	bool_t use_agc;
	bool_t eq_active;
	bool_t use_ng;/*noise gate*/
	bool_t use_rc;
	bool_t is_beginning;
	MSFilter *tee;
	MSFilter *rec;
};

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _AudioStream AudioStream;

struct _RingStream
{
	MSTicker *ticker;
	MSFilter *source;
	MSFilter *gendtmf;
	MSFilter *write_resampler;
	MSFilter *sndwrite;
};

typedef struct _RingStream RingStream;



/* start a thread that does sampling->encoding->rtp_sending|rtp_receiving->decoding->playing */
AudioStream *audio_stream_start (RtpProfile * prof, int locport, const char *remip,
				 int remport, int payload_type, int jitt_comp, bool_t echo_cancel);

AudioStream *audio_stream_start_with_sndcards(RtpProfile * prof, int locport, const char *remip4, int remport, int payload_type, int jitt_comp, MSSndCard *playcard, MSSndCard *captcard, bool_t echocancel);

int audio_stream_start_with_files (AudioStream * stream, RtpProfile * prof,
					    const char *remip, int remport, int rem_rtcp_port,
					    int pt, int jitt_comp,
					    const char * infile,  const char * outfile);

int audio_stream_start_full(AudioStream *stream, RtpProfile *profile, const char *remip,int remport,
	int rem_rtcp_port, int payload,int jitt_comp, const char *infile, const char *outfile,
	MSSndCard *playcard, MSSndCard *captcard, bool_t use_ec);

void audio_stream_play(AudioStream *st, const char *name);
void audio_stream_record(AudioStream *st, const char *name);

void audio_stream_set_rtcp_information(AudioStream *st, const char *cname, const char *tool);

void audio_stream_play_received_dtmfs(AudioStream *st, bool_t yesno);

/* those two function do the same as audio_stream_start() but in two steps
this is useful to make sure that sockets are open before sending an invite;
or to start to stream only after receiving an ack.*/
AudioStream *audio_stream_new(int locport, bool_t ipv6);
int audio_stream_start_now(AudioStream * stream, RtpProfile * prof,  const char *remip, int remport, int rem_rtcp_port, int payload_type, int jitt_comp,MSSndCard *playcard, MSSndCard *captcard, bool_t echo_cancel);
void audio_stream_set_relay_session_id(AudioStream *stream, const char *relay_session_id);
/*returns true if we are still receiving some data from remote end in the last timeout seconds*/
bool_t audio_stream_alive(AudioStream * stream, int timeout);

/*execute background tasks related to audio processing*/
void audio_stream_iterate(AudioStream *stream);

/*enable echo-limiter dispositve: one MSVolume in input branch controls a MSVolume in the output branch*/
void audio_stream_enable_echo_limiter(AudioStream *stream, EchoLimiterType type);

/*enable gain control, to be done before start() */
void audio_stream_enable_gain_control(AudioStream *stream, bool_t val);

/*enable automatic gain control, to be done before start() */
void audio_stream_enable_automatic_gain_control(AudioStream *stream, bool_t val);

/*to be done before start */
void audio_stream_set_echo_canceller_params(AudioStream *st, int tail_len_ms, int delay_ms, int framesize);

/*enable adaptive rate control */
void audio_stream_enable_adaptive_bitrate_control(AudioStream *st, bool_t enabled);


void audio_stream_set_mic_vol_gain(AudioStream *stream, float gain, int port);

void audio_stream_get_mic_gain(AudioStream *stream, float *gain);

/* enable/disable rtp stream */ 
void audio_stream_mute_rtp(AudioStream *stream, bool_t val);

/*enable noise gate, must be done before start()*/
void audio_stream_enable_noise_gate(AudioStream *stream, bool_t val);

/*enable parametric equalizer in the stream that goes to the speaker*/
void audio_stream_enable_equalizer(AudioStream *stream, bool_t enabled);

void audio_stream_equalizer_set_gain(AudioStream *stream, int frequency, float gain, int freq_width);

/* stop the audio streaming thread and free everything*/
void audio_stream_stop (AudioStream * stream);

RingStream *ring_start (const char * file, int interval, MSSndCard *sndcard);
RingStream *ring_start_with_cb(const char * file, int interval, MSSndCard *sndcard, MSFilterNotifyFunc func, void * user_data);
void ring_stop (RingStream * stream);


/* send a dtmf */
int audio_stream_send_dtmf (AudioStream * stream, char dtmf);

void audio_stream_set_default_card(int cardindex);

/* retrieve RTP statistics*/
void audio_stream_get_local_rtp_stats(AudioStream *stream, rtp_stats_t *stats);

/* returns a realtime indicator of the stream quality between 0 and 5 */
float audio_stream_get_quality_rating(AudioStream *stream);

/* returns the quality rating as an average since the start of the streaming session.*/
float audio_stream_get_average_quality_rating(AudioStream *stream);


/*****************
  Video Support
 *****************/

typedef void (*VideoStreamRenderCallback)(void *user_pointer, const MSPicture *local_view, const MSPicture *remote_view);
typedef void (*VideoStreamEventCallback)(void *user_pointer, const MSFilter *f, const unsigned int event_id, const void *args);



typedef enum _VideoStreamDir{
	VideoStreamSendRecv,
	VideoStreamSendOnly,
	VideoStreamRecvOnly,
	VideoStreamInactive,
}VideoStreamDir;

struct _VideoStream
{
	MSTicker *ticker;
	RtpSession *session;
	MSFilter *source;
	MSFilter *pixconv;
	MSFilter *sizeconv;
	MSFilter *tee;
	MSFilter *output;
	MSFilter *encoder;
	MSFilter *decoder;
	MSFilter *rtprecv;
	MSFilter *rtpsend;
	OrtpEvQueue *evq;
	MSVideoSize sent_vsize;
	int corner; /*for selfview*/
	
	VideoStreamDir dir;
	bool_t adapt_bitrate;
	
	MSFilter *rec;
	MSFilter *input_sizeconv;
};

typedef struct _VideoStream VideoStream;


VideoStream *video_stream_new(int locport, bool_t use_ipv6);
void video_stream_set_direction(VideoStream *vs, VideoStreamDir dir);
void video_stream_set_relay_session_id(VideoStream *stream, const char *relay_session_id);
void video_stream_set_rtcp_information(VideoStream *st, const char *cname, const char *tool);
/* Calling video_stream_set_sent_video_size() or changing the bitrate value in the used PayloadType during a stream is running does nothing.
The following function allows to take into account new parameters by redrawing the sending graph*/
/*function to call periodically to handle various events */
void video_stream_iterate(VideoStream *stream);
void video_stream_send_vfu(VideoStream *stream);
void video_stream_stop(VideoStream * stream);
void video_stream_set_sent_video_size(VideoStream *stream, MSVideoSize vsize);
//void video_stream_set_show_video_rect(VideoStream *stream, RECT vrect);
void video_stream_set_device_rotation(VideoStream *stream, int orientation);
void video_stream_enable_self_view(VideoStream *stream, bool_t val);

/*provided for compatibility, use video_stream_set_direction() instead */
int video_stream_recv_only_start(VideoStream *videostream, RtpProfile *profile, const char *addr, int port, int used_pt, int jitt_comp);
int video_stream_send_only_start(VideoStream *videostream,
				RtpProfile *profile, const char *addr, int port, int rtcp_port, 
				int used_pt, int  jitt_comp, MSWebCam *device);
void video_stream_recv_only_stop(VideoStream *vs);
void video_stream_send_only_stop(VideoStream *vs);

void video_stream_get_local_rtp_stats(VideoStream *stream, rtp_stats_t *stats);


/**
 * Small API to display a local preview window.
**/

typedef VideoStream VideoPreview;

VideoPreview * video_preview_new();
#define video_preview_set_size(p,s) 							video_stream_set_sent_video_size(p,s)
#define video_preview_set_display_filter_name(p,dt)	video_stream_set_display_filter_name(p,dt)
void video_preview_stop(VideoPreview *stream);

bool_t ms_is_ipv6(const char *address);


int audio_stream_start_call(AudioStream *stream, RtpProfile *profile, const char *remip,int remport, int rem_rtcp_port, int payload,int jitt_comp, MSSndCard *playcard, MSSndCard *captcard, bool_t use_ec);
#ifndef _IS_PHONE_
int audio_stream_start_rec(AudioStream *stream, RtpProfile *profile, int payload,int jitt_comp, MSSndCard *captcard, bool_t use_ec, icam_rec_write_func func);
int audio_stream_call_add_rec(AudioStream * stream, icam_rec_write_func func);
int audio_stream_rec_add_call(AudioStream *stream, RtpProfile *profile, const char *remip,int remport, int rem_rtcp_port, int payload, MSSndCard *playcard, bool_t use_ec);
void audio_stream_stop_rec(AudioStream * stream);
void audio_stream_stop_rec_only(AudioStream * stream);
#endif
void audio_stream_stop_call(AudioStream * stream);
void audio_stream_stop_call_only(AudioStream * stream);

int video_stream_start_call (VideoStream *stream, RtpProfile *profile, const char *remip, int remport, int rem_rtcp_port, int payload, int jitt_comp, int window_id, MSFilter *output);
#ifndef _IS_PHONE_
int video_stream_start_rec (VideoStream *stream, RtpProfile *profile, int payload, int jitt_comp, icam_rec_write_func func);
int video_stream_call_add_rec(VideoStream *stream, icam_rec_write_func func);
int video_stream_rec_add_call(VideoStream *stream, const char *remip, int remport, int rem_rtcp_port, RtpProfile *profile, int payload, int window_id, MSFilter *output);
void video_stream_stop_rec_only(VideoStream *stream);
void video_stream_stop_rec(VideoStream *stream);
#endif
void video_stream_stop_call_only(VideoStream *stream);
void video_stream_stop_call(VideoStream *stream);

int  audio_stream_start_play_record(AudioStream *stream, RtpProfile *profile, int payload, MSSndCard *playcard);
int  video_stream_start_play_record(VideoStream *stream, MSFilterId decoder_id, int window_id, MSFilter *output);
void audio_stream_stop_play_record(AudioStream *stream);
void video_stream_stop_play_record(VideoStream *stream);


void put_yuv420sp_to_android_video(const uint8_t *yuv420sp, int len);
void androidSetVideoCaptureSize(int width, int height);
int video_stream_change_size(VideoStream *stream, MSVideoSize * size);

#ifdef __cplusplus
}
#endif


#endif
