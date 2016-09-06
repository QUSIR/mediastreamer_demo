#ifndef VIDEOSTREAM_H
#define VIDEOSTREAM_H

#include "msticker.h"
#include "msfilter.h"

typedef struct {
	MSTicker *ticker;
	RtpSession *session;
	MSFilter *source;
	MSFilter *rtpSend;
}VideoStream;

VideoStream* video_stream_new();
int video_stream_start(VideoStream *stream, int localPort, const char* remoteIp, int remoteRtpPort, int remoteRtcpPort);
void video_stream_stop(VideoStream* stream);

#endif // VIDEOSTREAM_H
