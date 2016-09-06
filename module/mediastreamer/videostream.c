#include "videostream.h"

#include "rtpsession.h"
#include "mediastreamer/linuxvideo.h"
#include "mediastreamer/msrtp.h"

VideoStream* video_stream_new() {
	VideoStream *stream = (VideoStream *)ms_new0 (VideoStream, 1);
	stream->source=NULL;
	stream->rtpSend=NULL;
	stream->session=NULL;
	stream->ticker=NULL;
	return stream;
}

void video_stream_free(VideoStream *stream) {
	if(stream->source!=NULL)
		ms_filter_destroy(stream->source);
	if(stream->rtpSend!=NULL)
		ms_filter_destroy(stream->rtpSend);
	if (stream->session!=NULL)
		rtp_session_destroy(stream->session);
	if(stream->ticker!=NULL)
		ms_ticker_destroy(stream->ticker);
	ms_free(stream);
}


int video_stream_start(VideoStream *stream, int localPort,const char* remoteIp, int remoteRtpPort, int remoteRtcpPort) {
	/* set rtp session */
	if(stream==NULL)
		return -1;
	stream->session=rtp_session_new(RTP_SESSION_SENDRECV);
	rtp_session_set_scheduling_mode(stream->session,0);
	rtp_session_set_blocking_mode(stream->session,0);
	rtp_session_set_payload_type(stream->session, 102);
	rtp_session_enable_adaptive_jitter_compensation(stream->session,TRUE);
	rtp_session_set_symmetric_rtp(stream->session,TRUE);
	rtp_session_set_local_addr(stream->session, "0.0.0.0", localPort);
	rtp_session_set_ssrc_changed_threshold(stream->session,0);
	rtp_session_set_rtcp_report_interval(stream->session, 1000);
	rtp_session_set_remote_addr_full(stream->session, remoteIp, remoteRtpPort, remoteRtcpPort);

	/* filter */
	stream->source =ms_filter_new(MS_LINUX_VIDEO_CAPTURE_ID);
	if(stream->source==NULL){
		return -3;
	}
	stream->rtpSend=ms_filter_new(MS_RTP_SEND_ID);
	if(stream->rtpSend==NULL){
		return -4;
	}
	ms_filter_call_method(stream->rtpSend, MS_RTP_SEND_SET_SESSION, stream->session);

	/* link filter */
	ms_filter_link(stream->source, 0, stream->rtpSend, 0);

	/* attach */
	stream->ticker = ms_ticker_new();
	ms_ticker_set_name(stream->ticker,"Video MSTicker");
	if(stream->source!=NULL)
		ms_ticker_attach(stream->ticker, stream->source);
	return 0;
}

void video_stream_stop(VideoStream *stream) {
	/* detach */
	if(stream->source!=NULL)
		ms_ticker_detach(stream->ticker, stream->source);
	/* unlink filter */
	if(stream->source!=NULL && stream->rtpSend!=NULL)
		ms_filter_unlink(stream->source, 0, stream->rtpSend, 0);
	/* destroy filter */
	video_stream_free(stream);
}
