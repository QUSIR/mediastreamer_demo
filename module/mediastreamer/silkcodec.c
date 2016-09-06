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

#include "msfilter.h"
#include "msticker.h"

#include "SKP_Silk_SDK_API.h"

// Define codec specific settings
#define MAX_BYTES_PER_FRAME     250 // Equals peak bitrate of 100 kbps 
#define MAX_INPUT_FRAMES        5
#define MAX_LBRR_DELAY          2
#define MAX_FRAME_LENGTH        480
#define FRAME_LENGTH_MS         20
#define MAX_API_FS_KHZ          48

typedef struct _SilkEncState {
	MSBufferizer	*bz;
	int				ptime;
	uint32_t		ts;
	uint8_t			* enc_readbuf;	
	void			*psEnc;
	SKP_SILK_SDK_EncControlStruct encControl;
}SilkEncState;

bool_t Encoder_Open(SilkEncState * s)
{
	SKP_int32 encSizeBytes;
	SKP_int32 ret;

	if (s->psEnc != NULL)
		return FALSE;

	// Create Encoder
	ret = SKP_Silk_SDK_Get_Encoder_Size(&encSizeBytes);
	if (ret)
	{
		printf ("[SILK] Get_Encoder_Size() returned %d\r\n", ret);
		return FALSE;
	}

	s->psEnc = malloc (encSizeBytes);
	if (s->psEnc == NULL)
	{
		printf ("[SILK] Encoder_Open() malloc %d bytes failed\r\n", encSizeBytes);
		return FALSE;
	}

	return TRUE;
}

bool_t Encoder_Close(SilkEncState * s)
{
	if (s->psEnc != NULL)
	{
		free (s->psEnc);
		s->psEnc = NULL;
	}
	return TRUE;
}


//=============================================================================
// SampleRate -- 8000 or 16000 Hz
// BitRate    -- 5000 ~~ 100000 bps
// FramesPerPayload -- 1 ~~ 5 frames per payload
//=============================================================================
bool_t Encoder_Start (SilkEncState * s,int SampleRate, int BitRate, int FramesPerPayload)
{
	SKP_int32 ret;

	if (s->psEnc == NULL)
		return FALSE;

	// Check parameter
	if (SampleRate != 8000   && SampleRate != 16000)      return FALSE;
	if (BitRate < 5000       || BitRate > 100000)         return FALSE;
	if (FramesPerPayload < 1 || FramesPerPayload > 5)     return FALSE;

	// Reset Encoder
	ret = SKP_Silk_SDK_InitEncoder (s->psEnc, &s->encControl);
	if (ret)
	{
		printf ("[SILK] Encoder_Start() returned %d\r\n", ret);
		return FALSE;
	}

    // Set Encoder parameters
	s->encControl.API_sampleRate        = SampleRate;
	s->encControl.maxInternalSampleRate = SampleRate;
	s->encControl.packetSize            = FramesPerPayload * SampleRate * 20 / 1000;
	s->encControl.packetLossPercentage  = 0;
	s->encControl.useInBandFEC          = FALSE;
	s->encControl.useDTX                = FALSE;
	s->encControl.complexity            = 0;
	s->encControl.bitRate               = BitRate;

	return TRUE;
}


bool_t Encoder_Stop(SilkEncState * s)
{
	if (s->psEnc == NULL)
		return FALSE;
	return TRUE;
}


bool_t Encoder_Reset(SilkEncState * s)
{
	SKP_int32 ret;
	SKP_SILK_SDK_EncControlStruct tmpControl;

	if (s->psEnc == NULL)
		return FALSE;

	tmpControl = s->encControl;
	if (s->encControl.packetSize == 0 || s->encControl.maxInternalSampleRate == 0 ||
		s->encControl.bitRate    == 0 || s->encControl.API_sampleRate == 0 )
		return FALSE;

	// Reset Encoder
	ret = SKP_Silk_SDK_InitEncoder (s->psEnc, &s->encControl);
	if (ret)
	{
		printf ("[SILK] Encoder_Reset() returned %d\r\n", ret);
		return FALSE;
	}

	s->encControl = tmpControl;
	return TRUE;
}


bool_t Encoder_Run (SilkEncState * s,short *SampleBuffer, int SampleNumber, uint8_t *Payload, int *pnBytes)
{
	SKP_int32 ret;
	SKP_int16 nBytes;
	int i;
	// Check parameter
	if (SampleBuffer == NULL || Payload == NULL || pnBytes == NULL)
		return FALSE;
	if (SampleNumber != s->encControl.packetSize)
		return FALSE;
	if (*pnBytes < (MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES))
		return FALSE;

	*pnBytes = 0;
	if (s->psEnc == NULL)
		return FALSE;

	// Silk Encoder
	SampleNumber = s->encControl.API_sampleRate / 50;
	for ( i = 0; i < s->encControl.packetSize / SampleNumber; i++)
	{
		nBytes = MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES;
		ret = SKP_Silk_SDK_Encode (s->psEnc, &s->encControl, SampleBuffer, SampleNumber, Payload, &nBytes);
		if (ret)
		{
			printf ("[SILK] Encoder_Run() returned %d\r\n", ret);
			return FALSE;
		}
		SampleBuffer += SampleNumber;
	}

	*pnBytes = nBytes;
	return TRUE;
}


static void enc_init(MSFilter *f){
	SilkEncState *s=(SilkEncState *)ms_new(SilkEncState,1);
	s->bz=ms_bufferizer_new();
	s->ptime=40;	
	s->ts=0;
	s->enc_readbuf = (uint8_t *)malloc(s->ptime * 8 * sizeof(uint16_t));
	if(s->enc_readbuf == NULL)
		return;
	
	s->psEnc = NULL;
	memset(&s->encControl, 0, sizeof(SKP_SILK_SDK_EncControlStruct));	

	if(!Encoder_Open(s))
		printf("silk coder Encoder_Open error\n");

	if(!Encoder_Start(s,8000,8000,s->ptime/20))
		printf("silk coder Encoder_Start error\n");

	f->data=s;
}

static void enc_uninit(MSFilter *f){
	SilkEncState *s=(SilkEncState*)f->data;

	Encoder_Stop(s);
	Encoder_Close(s);	

	if(s->enc_readbuf)
	{
		free(s->enc_readbuf);
		s->enc_readbuf = NULL;
	}

	ms_bufferizer_destroy(s->bz);
	ms_free(s);
}

static void enc_preprocess(MSFilter *f)
{
}

static uint8_t enc_outbuf[250*5];

static void enc_process(MSFilter *f){
	SilkEncState *s=(SilkEncState*)f->data;
	mblk_t *im;
	int nBytes;
	int buff_size = s->ptime * 8 * sizeof(uint16_t);
	
	while((im=ms_queue_get(f->inputs[0]))!=NULL){
		ms_bufferizer_put(s->bz,im);
	}

	while(ms_bufferizer_get_avail(s->bz) >= buff_size) {

		ms_bufferizer_read(s->bz,s->enc_readbuf,buff_size);

		nBytes = 250*5;
		if(Encoder_Run (s,(short*)s->enc_readbuf, s->ptime * 8, &enc_outbuf[0], &nBytes))
		{
			mblk_t *om = allocb(nBytes,0);
			memcpy(om->b_wptr,enc_outbuf,nBytes);
			om->b_wptr += nBytes;

			mblk_set_timestamp_info(om,s->ts);
			ms_queue_put(f->outputs[0],om);
		}
		s->ts += buff_size/sizeof(uint16_t);
	}
}

static void enc_postprocess(MSFilter *f){
}

static MSFilterMethod enc_methods[]={
	{	0				,	NULL		}
};

MSFilterDesc ms_silk_enc_desc={
	MS_SILK_ENCODER_ID,
	"SilkEnc",
	"Silk encoder",
	MS_FILTER_ENCODER,
	"silk",
	1,
	1,
	enc_init,
	enc_preprocess,
	enc_process,
	enc_postprocess,
	enc_uninit,
	enc_methods
};



typedef struct SilkDecState{
	void * psDec;	
	SKP_SILK_SDK_DecControlStruct decControl;	
} SilkDecState;


bool_t Decoder_Open(SilkDecState * s)
{
	SKP_int32 decSizeBytes;
	SKP_int32 ret;

	if (s->psDec != NULL)
		return FALSE;

	// Create Decoder
	ret = SKP_Silk_SDK_Get_Decoder_Size (&decSizeBytes);
	if (ret)
	{
		printf ("[SILK] Get_Decoder_Size() returned %d\r\n", ret);
		return FALSE;
	}
	
	s->psDec = malloc (decSizeBytes);
	if (s->psDec == NULL)
	{
		printf ("[SILK] Decoder_Open() malloc %d bytes failed\r\n", decSizeBytes);
		return FALSE;
	}

	return TRUE;
}


bool_t Decoder_Close(SilkDecState * s)
{
	if (s->psDec != NULL)
	{
		free (s->psDec);
		s->psDec = NULL;
	}
	return TRUE;
}

//=============================================================================
// SampleRate -- 8000 or 16000 Hz
//=============================================================================
bool_t Decoder_Start(SilkDecState * s,int SampleRate)
{
	SKP_int32 ret;
	if (s->psDec == NULL)
		return FALSE;

	// Check parameter
	if (SampleRate != 8000 && SampleRate != 16000)
		return FALSE;

	// Reset Decoder
	ret = SKP_Silk_SDK_InitDecoder(s->psDec);
	if (ret)
	{
		printf ("[SILK] Decoder_Start() returned %d\r\n", ret);
		return FALSE;
	}

    // Set Decoder parameters
	s->decControl.API_sampleRate  = SampleRate;
	s->decControl.frameSize       = 0;
	s->decControl.framesPerPacket = 0;
	s->decControl.inBandFECOffset = 0;
	s->decControl.moreInternalDecoderFrames = 0;

	return TRUE;
}


bool_t Decoder_Stop(SilkDecState * s)
{
	if (s->psDec == NULL)
		return FALSE;
	return TRUE;
}


bool_t Decoder_Reset(SilkDecState * s)
{
	SKP_int32 ret;
	if (s->psDec == NULL)
		return FALSE;

	// Reset Decoder
	ret = SKP_Silk_SDK_InitDecoder(s->psDec);
	if (ret)
	{
		printf ("[SILK] Decoder_Reset() returned %d\r\n", ret);
		return FALSE;
	}

	return TRUE;
}


bool_t Decoder_Run (SilkDecState * s,bool_t bPacketLost, uint8_t *PayloadBuf, int PayloadSize, short *PcmBuf, int *pnSamples)
{
	SKP_int32  ret;
	SKP_int16 *SamplePtr;
	SKP_int16  SampleCnt;
	SKP_int16  nSamples;
	int  frames, i;

	// Check parameter
	if (PayloadBuf == NULL || PcmBuf == NULL || pnSamples == NULL)
		return FALSE;
	if (PayloadSize < 0)
		return FALSE;

	*pnSamples = 0;
	if (s->psDec == NULL)
		return FALSE;

	SamplePtr = PcmBuf;
	SampleCnt = 0;

	if (bPacketLost == FALSE)
	{
		// No Loss: Decode all frames in the packet
		frames = 0;
		do
		{
			// Decode 20 ms
			ret = SKP_Silk_SDK_Decode (s->psDec, &s->decControl, 0, PayloadBuf, PayloadSize, SamplePtr, &nSamples);
			if (ret)
			{
				printf ("[SILK] Decoder_Run() returned %d\r\n", ret);
				return FALSE;
			}
			
			frames ++;
			SamplePtr += nSamples;
			SampleCnt += nSamples;

			// Hack for corrupt stream that could generate too many frames
			if (frames > MAX_INPUT_FRAMES)
			{
				frames = 0;
				SamplePtr = PcmBuf;
				SampleCnt = 0;
			}
			// Until last 20 ms frame of packet has been decoded
		} while (s->decControl.moreInternalDecoderFrames);
	}
	else
	{
		// Loss: Decode enough frames to cover one packet duration
		for (i = 0; i < s->decControl.framesPerPacket; i++)
		{
			// Generate 20 ms
			ret = SKP_Silk_SDK_Decode (s->psDec, &s->decControl, 1, PayloadBuf, PayloadSize, SamplePtr, &nSamples);
			if (ret)
			{
				printf ("[SILK] Decoder_Run() returned %d\r\n", ret);
				return FALSE;
			}
			SamplePtr += nSamples;
			SampleCnt += nSamples;
		}
	}

	*pnSamples = SampleCnt;
	return TRUE;
}


static void dec_init(MSFilter *f){
	SilkDecState *s=(SilkDecState *)ms_new(SilkDecState,1);
	
	s->psDec = NULL;
	memset(&s->decControl, 0, sizeof(SKP_SILK_SDK_DecControlStruct));
	
	if(!Decoder_Open(s))
	{
		printf("silk decoder Decoder_Open error\n");
		return;
	}

	if(!Decoder_Start(s,8000))
	{
		printf("silk decoder Decoder_Open error\n");
		return;
	}

	f->data=s;
}

static void dec_uninit(MSFilter *f){
	SilkDecState *s=(SilkDecState*)f->data;
    if (s==NULL)
		return;

	Decoder_Stop(s);
	Decoder_Close(s);

	ms_free(s);
}

static void dec_preprocess(MSFilter *f){
}

static void dec_postprocess(MSFilter *f){
}

static void dec_process(MSFilter *f){
	SilkDecState *s=(SilkDecState*)f->data;
	mblk_t *im;
	mblk_t *om;
	char tmpBuf[1600];
	int frsz = 0;
	int   nSamples;

	while((im=ms_queue_get(f->inputs[0]))!=NULL){
		//ms_error("SilkDecState:%d\r\n", msgdsize(im));
				
		Decoder_Run(s,FALSE,im->b_rptr,msgdsize(im),(short*)tmpBuf,&nSamples);
		frsz = nSamples*2;
		om=allocb(frsz,0);
		memcpy(om->b_rptr, tmpBuf, frsz);
		om->b_wptr += frsz;

		ms_queue_put(f->outputs[0],om);

		freemsg(im);
	}
}

static MSFilterMethod dec_methods[]={
	{	0				,	NULL		}
};

MSFilterDesc ms_silk_dec_desc={
	MS_SILK_DECODER_ID,
	"SilkDec",
	"Silk decoder",
	MS_FILTER_DECODER,
	"silk",
	1,
	1,
	dec_init,
	dec_preprocess,
	dec_process,
	dec_postprocess,
	dec_uninit,
	dec_methods
};

MS_FILTER_DESC_EXPORT(ms_silk_dec_desc)
MS_FILTER_DESC_EXPORT(ms_silk_enc_desc)


