#ifndef AUDIOSTREAM_H
#define AUDIOSTREAM_H

#include "msticker.h"
#include "msfilter.h"

typedef struct {
	MSTicker *ticker;
	RtpSession *session;
	MSFilter *source;
	MSFilter *volumeSend;
	MSFilter *encoder;
	MSFilter *rtpSend;
	MSFilter *rtpRecv;
	MSFilter *decoder;
	MSFilter *volumeRecv;
	MSFilter *dest;
	OrtpEvQueue *eventQueue;
}AudioStream;

AudioStream* audio_stream_new();
int audio_stream_start(AudioStream *stream, int localPort, const char* remoteIp, int remoteRtpPort, int remoteRtcpPort);
void audio_stream_stop(AudioStream* stream);

#endif // AUDIOSTREAM_H
