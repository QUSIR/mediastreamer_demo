#include "linuxvideo.h"

#include "mscommon.h"
#include "mediastream.h"
#include "allfilters.h"
#include <stdio.h>
#include <time.h>

#include "Service/Camera/camera.h"

const int MAX_RTP_PKG_SIZE = 1400;

typedef struct {
	int startcodeprefix_len;      //! 4 for parameter sets and first slice in picture, 3 for everything else (suggested)
	unsigned len;                 //! Length of the NAL unit (Excluding the start code, which does not belong to the NALU)
	unsigned max_size;            //! Nal Unit Buffer size
	int forbidden_bit;            //! should be always FALSE
	int nal_reference_idc;        //! NALU_PRIORITY_xxxx
	int nal_unit_type;            //! NALU_TYPE_xxxx
	char *buf;                    //! contains the first byte followed by the EBSP
	unsigned short lost_packets;  //! true, if packet loss is detected
} NALU_t;

typedef struct {
	//byte 0
	unsigned char TYPE:5;
	unsigned char NRI:2;
	unsigned char F:1;

} NALU_HEADER; /**//* 1 BYTES */

typedef struct {
	//byte 0
	unsigned char TYPE:5;
	unsigned char NRI:2;
	unsigned char F:1;


} FU_INDICATOR; /**//* 1 BYTES */

typedef struct {
	//byte 0
	unsigned char TYPE:5;
	unsigned char R:1;
	unsigned char E:1;
	unsigned char S:1;
} FU_HEADER; /**//* 1 BYTES */


static ms_mutex_t captureLock;

static NALU_t *AllocNALU(int size){
	NALU_t *n;
	if ((n = (NALU_t*)calloc (1, sizeof (NALU_t))) == NULL){
		printf("linuxvideo.c AllocNALU: fail to calloc NALU_t\n");
		return NULL;
	}
	n->max_size=size;
	if ((n->buf = (char*)calloc (size, sizeof (char))) == NULL){
		free (n);
		printf("linuxvideo.c AllocNALU: fail to calloc buffer\n");
		return NULL;
	}
	return n;
}

static void FreeNALU(NALU_t *n){
	if (n){
		if (n->buf){
			free(n->buf);
			n->buf=NULL;
		}
		free (n);
	}
}

static int FindStartCode (unsigned char *Buf, int zeros_in_startcode){
	int info;
	int i;

	info = 1;
	for (i = 0; i < zeros_in_startcode; i++){
		if(Buf[i] != 0){
			info = 0;
		}
	}

	if(Buf[i] != 1){
		info = 0;
	}
	return info;
}

static int GetNextNal(NALU_t *nalu, unsigned char *srcbuf, int len, int istartPos)
{
	int pos = istartPos;
	int StartCodeFound = 0;
	int info2 = 0;
	int info3 = 0;
	int irewind = 0;
	unsigned char *Buf;

	if ((Buf = (unsigned char*)calloc (nalu->max_size , sizeof(char))) == NULL)
		return -1;

	while(len > pos && (Buf[pos++]=srcbuf[pos])==0);//起始标识为 00 00 00 01或者00 00 01 //这里如果POS不是0的话，下面Buf复制时需要加上istartPos
	if (Buf[pos - 1] == 0x1){
		nalu->startcodeprefix_len = pos - istartPos;
	}else{
		return -1;
	}

	while (!StartCodeFound){
		if (len <= pos){
			nalu->len = pos - nalu->startcodeprefix_len - istartPos;
			memcpy (nalu->buf, &Buf[istartPos + nalu->startcodeprefix_len], nalu->len);
			nalu->forbidden_bit = nalu->buf[0] & 0x80; //1 bit
			nalu->nal_reference_idc = nalu->buf[0] & 0x60; // 2 bit
			nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;// 5 bit
			free(Buf);
			return pos-1;
		}
		Buf[pos++] = srcbuf[pos];
		info3 = FindStartCode(&Buf[pos-4], 3);
		if(info3 != 1){
			info2 = FindStartCode(&Buf[pos-3], 2);
		}
		if ((0x65 == srcbuf[pos] || 0x41 == srcbuf[pos]) && (srcbuf[istartPos + 4] != 0x67 && srcbuf[istartPos + 4] != 0x68))
		{
			memcpy(Buf + pos, srcbuf + pos, len - pos);
			pos = len;
		}
		StartCodeFound = (info2 == 1 || info3 == 1);
	}
	irewind = (info3 == 1)? -4 : -3;

	nalu->len = (pos+irewind) - nalu->startcodeprefix_len - istartPos;
	memcpy (nalu->buf, &Buf[istartPos + nalu->startcodeprefix_len], nalu->len);//拷贝一个完整NALU，不拷贝起始前缀0x000001或0x00000001
	nalu->forbidden_bit	= nalu->buf[0] & 0x80;	//1 bit
	nalu->nal_reference_idc = nalu->buf[0] & 0x60;	// 2 bit
	nalu->nal_unit_type	= nalu->buf[0] & 0x1f;	// 5 bit

	free(Buf);

	return (pos + irewind);//返回两个开始字符之间间隔的字节数，即包含有前缀的NALU的长度
}

static void video_capture_init(MSFilter *f){
	ms_mutex_init(&captureLock, NULL);
	if(!preview_init()){
		printf("linuxvideo.c video_capture_init - fail to init preview\n");
	}
}

static void video_capture_uninit(MSFilter *f){
	preview_uninit();
	ms_mutex_destroy(&captureLock);
}

//static uint64_t get_cur_time_ms(){
//	struct timespec time1;
//	clock_gettime(CLOCK_MONOTONIC, &time1);
//	return (time1.tv_sec*1000LL)+((time1.tv_nsec)/1000000LL);
//}

//static uint64_t staticTime=0;

static void video_capture_process(MSFilter *f){
//	uint64_t currentTime=get_cur_time_ms();
//	if(currentTime-staticTime<50) return;
//	staticTime=currentTime;
	if(f->ticker->time % 50 !=0) return;
	unsigned char *data = NULL;
	int length = 0;
	uint32_t ts = (uint32_t)(f->ticker->time*90LL);
	ms_mutex_lock(&captureLock);
	preview_get_frame(&data,&length);
//	printf("video_capture_process data length=%d f->ticker->time=%d\n",length,(int)f->ticker->time);
	if (length > 0 && data != NULL){
		int iDatalen = 0;
		NALU_t *nalu;
		nalu = AllocNALU(length + (7 * sizeof(int)));//20480);

		while (iDatalen < length){
			iDatalen = GetNextNal(nalu, data, length, iDatalen);
//			printf("video_capture_process - iDatalen=%d\n",iDatalen);
			if (iDatalen <= 0)
				break;

			if(nalu->len <= MAX_RTP_PKG_SIZE){
				NALU_HEADER *pNalu_header;
				mblk_t *im = allocb(nalu->len + sizeof(NALU_HEADER), 0);
				if(NULL == im){
					printf("allocb mblk_t faile in h264_buff_len <= MAX_RTP_PKG_SIZE == 0");
					return;
				}
				pNalu_header = (NALU_HEADER*)im->b_wptr;
				pNalu_header->F = nalu->forbidden_bit;
				pNalu_header->NRI = nalu->nal_reference_idc >> 5;
				pNalu_header->TYPE = nalu->nal_unit_type;
				im->b_wptr += sizeof(NALU_HEADER);

				memcpy(im->b_wptr, nalu->buf + 1, nalu->len - 1);
				im->b_wptr += nalu->len - 1;

				mblk_set_marker_info(im, 1);
				mblk_set_timestamp_info(im, ts);

				ms_queue_put(f->outputs[0], im);
			}else if(nalu->len > MAX_RTP_PKG_SIZE){
				//得到该nalu需要用多少长度为1400字节的RTP包来发送
				int index		= 0;
				int iCountRtpPkg	= 0;
				int iLastRtpPkgSize = 0;
				int iCurRtpPkg		= 0;							//用于指示当前发送的是第几个分片RTP包

				iCountRtpPkg		= nalu->len / MAX_RTP_PKG_SIZE;	//需要iCountRtpPkg个1400字节的RTP包
				if(iCountRtpPkg>5) break;
				iLastRtpPkgSize		= nalu->len % MAX_RTP_PKG_SIZE;	//最后一个RTP包的需要装载的字节数

				while(iCurRtpPkg <= iCountRtpPkg){
					if(0 == iCurRtpPkg)//发送一个需要分片的NALU的第一个分片，置FU HEADER的S位
					{
						FU_INDICATOR	*pFu_Indicator;
						FU_HEADER	*pFu_Header;
						mblk_t 		*im = allocb(sizeof(FU_INDICATOR) + sizeof(FU_HEADER) + MAX_RTP_PKG_SIZE, 0);

						if(NULL == im){
							printf("allocb mblk_t faile in curr_rtp_pkg == 0");
							return;
						}

						pFu_Indicator = (FU_INDICATOR*)im->b_wptr;
						pFu_Indicator->F = nalu->forbidden_bit;
						pFu_Indicator->NRI = nalu->nal_reference_idc >> 5;
						pFu_Indicator->TYPE = 28;
						im->b_wptr += sizeof(FU_INDICATOR);

						pFu_Header = (FU_HEADER*)im->b_wptr;
						pFu_Header->E = 0;
						pFu_Header->R = 0;
						pFu_Header->S = 1;
						pFu_Header->TYPE = nalu->nal_unit_type;
						im->b_wptr += sizeof(FU_HEADER);

						memcpy(im->b_wptr, nalu->buf + 1 + index, MAX_RTP_PKG_SIZE);//nalu->buf[0]存放67、68、65，这些不需要
						im->b_wptr += MAX_RTP_PKG_SIZE;

						index += MAX_RTP_PKG_SIZE;

						mblk_set_marker_info(im, 0);
						mblk_set_timestamp_info(im, ts);
						ms_queue_put(f->outputs[0], im);
					}
					//发送一个需要分片的NALU的非第一个分片，清零FU HEADER的S位，如果该分片是该NALU的最后一个分片，置FU HEADER的E位
					else if(iCurRtpPkg == iCountRtpPkg)//发送的是最后一个分片，注意最后一个分片的长度可能超过1400字节（当l>1386时）。
					{
						FU_INDICATOR	*pFu_Indicator;
						FU_HEADER		*pFu_Header;
						mblk_t 			*im = allocb(sizeof(FU_INDICATOR) + sizeof(FU_HEADER) + iLastRtpPkgSize, 0);

						if(NULL == im){
							printf("allocb mblk_t faile in curr_rtp_pkg == num_rtp_pkg");
							return;
						}

						pFu_Indicator = (FU_INDICATOR*)im->b_wptr;
						pFu_Indicator->F = nalu->forbidden_bit;
						pFu_Indicator->NRI = nalu->nal_reference_idc;
						pFu_Indicator->TYPE = 28;
						im->b_wptr += sizeof(FU_INDICATOR);

						pFu_Header = (FU_HEADER*)im->b_wptr;
						pFu_Header->E = 1;
						pFu_Header->R = 0;
						pFu_Header->S = 0;
						pFu_Header->TYPE = nalu->nal_unit_type;
						im->b_wptr += sizeof(FU_HEADER);

						memcpy(im->b_wptr, nalu->buf + 1 + index, iLastRtpPkgSize - 1);//nalu->buf[0]存放67、68、65，这些不需要，别忘记复制总长度需要减1
						im->b_wptr += iLastRtpPkgSize;

						index += iLastRtpPkgSize;

						mblk_set_marker_info(im, 1);
						mblk_set_timestamp_info(im, ts);
						ms_queue_put(f->outputs[0], im);
					}else if(iCurRtpPkg < iCountRtpPkg && 0 != iCurRtpPkg)//中间包
					{
						FU_INDICATOR	*pFu_Indicator;
						FU_HEADER		*pFu_Header;
						mblk_t 			*im = allocb(sizeof(FU_INDICATOR) + sizeof(FU_HEADER) + MAX_RTP_PKG_SIZE, 0);

						if(NULL == im){
							printf("allocb mblk_t faile in curr_rtp_pkg == num_rtp_pkg");
							return;
						}

						pFu_Indicator = (FU_INDICATOR*)im->b_wptr;
						pFu_Indicator->F = nalu->forbidden_bit;
						pFu_Indicator->NRI = nalu->nal_reference_idc;
						pFu_Indicator->TYPE = 28;
						im->b_wptr += sizeof(FU_INDICATOR);

						pFu_Header = (FU_HEADER*)im->b_wptr;
						pFu_Header->E = 0;
						pFu_Header->R = 0;
						pFu_Header->S = 0;
						pFu_Header->TYPE = nalu->nal_unit_type;
						im->b_wptr += sizeof(FU_HEADER);

						memcpy(im->b_wptr, nalu->buf + 1 + index, MAX_RTP_PKG_SIZE);
						im->b_wptr += MAX_RTP_PKG_SIZE;

						index += MAX_RTP_PKG_SIZE;

						mblk_set_marker_info(im, 0);
						mblk_set_timestamp_info(im, ts);
						ms_queue_put(f->outputs[0], im);
					}
					iCurRtpPkg++;
				}
			}
		}
		FreeNALU(nalu);
	}
	ms_mutex_unlock(&captureLock);
}

static MSFilterMethod video_capture_methods[]={
		{0,0}
};


#ifdef _MSC_VER

MSFilterDesc ms_linux_video_capture_desc={
	MS_LINUX_VIDEO_CAPTURE_ID,
	"MSLinuxVideoCapture",
	N_("Linux Video Capturer"),
	MS_FILTER_OTHER,
	NULL,
	0,
	1,
	video_capture_init,
	NULL,
	video_capture_process,
	NULL,
	video_capture_uninit,
	video_capture_methods,
	MS_FILTER_IS_PUMP
};

#else

MSFilterDesc ms_linux_video_capture_desc={
	.id=MS_LINUX_VIDEO_CAPTURE_ID,
	.name="MSLinuxVideoCapture",
	.text=N_("Linux Video Capturer"),
	.category=MS_FILTER_OTHER,
	.ninputs=0,
	.noutputs=1,
	.init=video_capture_init,
	.process=video_capture_process,
	.uninit=video_capture_uninit,
	.methods=video_capture_methods,
	.flags=MS_FILTER_IS_PUMP
};

#endif

MS_FILTER_DESC_EXPORT(ms_linux_video_capture_desc)
