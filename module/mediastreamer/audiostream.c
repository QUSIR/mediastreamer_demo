#include "mediastreamer/audiostream.h"

#include "rtpsession.h"
#include "mediastreamer/msrtp.h"
#include "mediastreamer/mssndcard.h"
#include "mediastreamer/msvolume.h"

#include <stdio.h>

AudioStream* audio_stream_new() {
	AudioStream *stream = (AudioStream *)ms_new0 (AudioStream, 1);
	return stream;
}

void audio_stream_free(AudioStream *stream) {
	if (stream->session!=NULL) {
		rtp_session_unregister_event_queue(stream->session,stream->eventQueue);
		rtp_session_destroy(stream->session);
	}
	if (stream->eventQueue) ortp_ev_queue_destroy(stream->eventQueue);
	if(stream->source!=NULL)
		ms_filter_destroy(stream->source);
	if(stream->encoder!=NULL)
		ms_filter_destroy(stream->encoder);
	if(stream->rtpSend!=NULL)
		ms_filter_destroy(stream->rtpSend);
	if(stream->ticker!=NULL)
		ms_ticker_destroy(stream->ticker);
	ms_free(stream);
}

#define payload_type_set_number2(pt,n)	(pt)->user_data=(void*)((long)n);
static void dp_set_payload_type(PayloadType *const_pt, int number, const char *recv_fmtp)
{
	payload_type_set_number2(const_pt, number);
//	if (recv_fmtp != NULL)
//		payload_type_set_recv_fmtp(const_pt,recv_fmtp);
	rtp_profile_set_payload(&av_profile,number,const_pt);
}

int audio_stream_start(AudioStream *stream, int localPort,const char* remoteIp, int remoteRtpPort, int remoteRtcpPort) {
	if(stream==NULL)
		return -1;
	stream->session=rtp_session_new(RTP_SESSION_SENDRECV);
	rtp_session_set_scheduling_mode(stream->session,0);
	rtp_session_set_blocking_mode(stream->session,0);
	rtp_session_set_payload_type(stream->session, 100);
	rtp_session_enable_adaptive_jitter_compensation(stream->session,TRUE);
	rtp_session_set_symmetric_rtp(stream->session,TRUE);
	rtp_session_set_local_addr(stream->session, "0.0.0.0", localPort);
	rtp_session_set_ssrc_changed_threshold(stream->session,0);
	rtp_session_set_rtcp_report_interval(stream->session, 1000);
	rtp_session_set_remote_addr_full(stream->session, remoteIp, remoteRtpPort, remoteRtcpPort);

	rtp_session_set_rtp_socket_send_buffer_size(stream->session, 65536);
	rtp_session_set_rtp_socket_recv_buffer_size(stream->session, 65536);
	rtp_session_set_recv_buf_size(stream->session,UDP_MAX_SIZE);
	rtp_session_set_jitter_compensation(stream->session,200);
	rtp_session_set_profile(stream->session,&av_profile);
	rtp_session_set_rtcp_report_interval(stream->session,2500);

	rtp_session_set_symmetric_rtp(stream->session,TRUE);
	stream->eventQueue=ortp_ev_queue_new();
	rtp_session_register_event_queue(stream->session,stream->eventQueue);

	dp_set_payload_type(&payload_type_silk,	100,"silk");
	/* filter */
	stream->source=ms_filter_new(MS_LINUX_SOUND_READ_ID);
	if(stream->source==NULL){
		return -2;
	}

	stream->volumeSend=ms_filter_new(MS_VOLUME_ID);
//	int enable=1;
//	ms_filter_call_method(stream->volumeSend,MS_VOLUME_ENABLE_NOISE_GATE,&enable);
	float gain=5.0;
	ms_filter_call_method(stream->volumeSend,MS_VOLUME_SET_GAIN,&gain);
//	ms_filter_call_method(stream->volumeSend, MS_VOLUME_ENABLE_AGC, &enable);

	PayloadType *pt;
	pt=rtp_profile_get_payload(&av_profile,100);
	if(pt==NULL){
		return -3;
	}
	stream->encoder=ms_filter_create_encoder(pt->mime_type);
	if(stream->encoder==NULL){
		ms_error("audiostream.c: No encoder available for payload 100:%s.", pt->mime_type);
		return -4;
	}
	if(pt->send_fmtp)
		ms_filter_call_method(stream->encoder, MS_FILTER_ADD_FMTP, (void*)pt->send_fmtp);

	stream->rtpSend=ms_filter_new(MS_RTP_SEND_ID);
	if(stream->rtpSend==NULL){
		return -5;
	}
	ms_filter_call_method(stream->rtpSend, MS_RTP_SEND_SET_SESSION, stream->session);

	stream->rtpRecv=ms_filter_new(MS_RTP_RECV_ID);
	if(stream->rtpRecv==NULL){
		return -6;
	}
	ms_filter_call_method(stream->rtpRecv,MS_RTP_RECV_SET_SESSION,stream->session);

	stream->decoder=ms_filter_create_decoder(pt->mime_type);
	if(stream->decoder==NULL){
		return -7;
	}

	stream->volumeRecv=ms_filter_new(MS_VOLUME_ID);
//	int enable=1;
//	ms_filter_call_method(stream->volumeSend,MS_VOLUME_ENABLE_NOISE_GATE,&enable);
	float gainRecv=0.8;
	ms_filter_call_method(stream->volumeRecv,MS_VOLUME_SET_GAIN,&gainRecv);
//	ms_filter_call_method(stream->volumeSend, MS_VOLUME_ENABLE_AGC, &enable);

	stream->dest=ms_filter_new(MS_LINUX_SOUND_WRITE_ID);
	if(stream->dest==NULL){
		return -8;
	}
	/* link filter */
	ms_filter_link(stream->source, 0, stream->volumeSend, 0);
	ms_filter_link(stream->volumeSend, 0, stream->encoder, 0);
	ms_filter_link(stream->encoder, 0, stream->rtpSend, 0);
	ms_filter_link(stream->rtpRecv, 0, stream->decoder, 0);
	ms_filter_link(stream->decoder, 0, stream->volumeRecv, 0);
	ms_filter_link(stream->volumeRecv, 0, stream->dest, 0);
	/* attach */
	stream->ticker = ms_ticker_new();
	if(stream->ticker==NULL){
		return -6;
	}
	ms_ticker_set_name(stream->ticker,"Audio MSTicker");
	ms_ticker_attach(stream->ticker, stream->source);
	ms_ticker_attach(stream->ticker,stream->rtpRecv);
	return 0;
}

void audio_stream_stop(AudioStream *stream) {
	/* detach */
	if(stream->source!=NULL)
		ms_ticker_detach(stream->ticker, stream->source);
	if(stream->rtpRecv!=NULL)
		ms_ticker_detach(stream->ticker,stream->rtpRecv);

	/* unlink filter */
	if(stream->source!=NULL && stream->volumeSend!=NULL)
		ms_filter_unlink(stream->source, 0, stream->volumeSend, 0);
	if(stream->volumeSend!=NULL && stream->encoder!=NULL)
		ms_filter_unlink(stream->volumeSend, 0, stream->encoder, 0);
	if(stream->encoder!=NULL && stream->rtpSend!=NULL)
		ms_filter_unlink(stream->encoder, 0, stream->rtpSend, 0);
	if(stream->rtpRecv!=NULL && stream->decoder!=NULL)
		ms_filter_unlink(stream->rtpRecv, 0, stream->decoder, 0);
	if(stream->decoder!=NULL && stream->volumeRecv!=NULL)
		ms_filter_unlink(stream->decoder, 0, stream->volumeRecv, 0);
	if(stream->volumeRecv!=NULL && stream->dest!=NULL)
		ms_filter_unlink(stream->volumeRecv, 0, stream->dest, 0);
	/* destroy filter */
	audio_stream_free(stream);
}

