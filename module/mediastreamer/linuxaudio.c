#include "mediastreamer/linuxaudio.h"

#include "mssndcard.h"
#include "msfilter.h"
#include "msticker.h"
#include "Service/Audio/linuxvoice.h"

//#include <sys/time.h>
#include <sys/resource.h>
//
#include <cpu-features.h>
//#include "devices.h"
#include <stdio.h>
#include <errno.h>

#include"linuxaudio.h"

static void set_high_prio(void){
	/*
		This pthread based code does nothing on linux. The linux kernel has
		sched_get_priority_max(SCHED_OTHER)=sched_get_priority_max(SCHED_OTHER)=0.
		As long as we can't use SCHED_RR or SCHED_FIFO, the only way to increase priority of a calling thread
		is to use setpriority().
	*/
	if (setpriority(PRIO_PROCESS,0,-20)==-1){
		ms_warning("msandroid set_high_prio() failed: %s",strerror(errno));
	}
}

MSFilterMethod ms_linux_sound_read_methods[]={
	{	0				, NULL		}
};



int create_path(const char *path)
{
	char *start;
	mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	if (path[0] == '/')
		start = strchr(path + 1, '/');
	else
		start = strchr(path, '/');

	while (start) {
		char *buffer = strdup(path);
		buffer[start-path] = 0x00;

		if (mkdir(buffer, mode) == -1 && errno != EEXIST) {
			fprintf(stderr, "Problem creating directory %s", buffer);
			perror(" ");
			free(buffer);
			return -1;
		}
		free(buffer);
		start = strchr(start + 1, '/');
	}
	return 0;
}

int open_voice(char *name){

	remove(name);
		//创建音频存储文件
		
	int bd =open(name, O_WRONLY | O_CREAT, 0644);
	if (bd == -1) {
		if (errno != ENOENT)
			return -1;
		if (create_path(name) == 0)
			bd = open(name, O_WRONLY | O_CREAT, 0644);
	}
// safe_open(name);
		if (bd < 0) {
			printf("openfile error\n");
			return -1;
		}


		return bd;
}
int save_voice(int bd,u_char *data,long long leng){

		//printf("data leng is %lld\n",leng);
		if (write(bd, data, leng) != leng) {
			printf("save_voice file error\n");
			return -1;
		}
}



static uint64_t get_cur_time_ms(){
	struct timespec time1;
	clock_gettime(CLOCK_MONOTONIC, &time1);
	return (time1.tv_sec*1000LL)+((time1.tv_nsec)/1000000LL);
}

static void* ms_linux_read_cb(ms_linux_sound_read_data* d) {
	mblk_t *m;
	int nframes;
	int nread;
	set_high_prio();
//	printf("nframe=%d\n",d->frames);
	printf("liang d->frames is %d\n",d->frames);
 	int fb=open_voice("ms_linux_read_cb.pcm");
	int temp;
	//temp=d->frames/8/2;
	temp=d->frames;
	printf("liang temp is %d\n",temp);
	while (d->isRunning &&
	       (nframes=sound_read(d->handle,d->read_buff,d->frames))==d->frames) {
		//save_voice(fb,d->read_buff,nframes);
//		(nframes=sound_read(d->handle,d->read_buff,160))>0) {
//		printf("ken debug: nread=%d time=%lld\n",nread,get_cur_time_ms());
		//nread=nframes*2;
		nread=nframes;
		printf("liang nread is %d\n",nread);
		//m->b_wptr=d->read_buff;
		m = allocb(nread,0);
		memcpy((void*)m->b_wptr,(void*)d->read_buff,nread);

		save_voice(fb,m->b_wptr,nread);

		m->b_wptr += nread;
//		ms_mutex_lock(&d->mutex);
		ms_bufferizer_put(d->rb,m);
//		ms_mutex_unlock(&d->mutex);
//		usleep(10000);
	}
	ms_thread_exit(NULL);
	return 0;
}

static void sound_read_preprocess(MSFilter *f){
	f->data=ms_new0(ms_linux_sound_read_data,1);
	ms_linux_sound_read_data *d=(ms_linux_sound_read_data*)f->data;
	d->handle=open_capture(8000,1,16,&(d->frames));
//	d->frames=1000;
	if(d->handle==NULL){
		printf("linuxaudio.c sound_read_preprocess - fail to open capture\n");
		return;
	}
	printf("liang d->frames is %d\n",d->frames);
	d->read_buff=(unsigned char*)calloc(d->frames,sizeof(unsigned char));
//	d->read_buff=(unsigned char*)calloc(160*4,sizeof(unsigned char));
	if(d->read_buff==NULL){
		printf("linuxaudio.c sound_read_preprocess - fail to calloc buffer\n");
		return;
	}
//	if(sound_dev_prepare(d->handle)!=0){
//		ms_error("linuxaudio.c sound_read_preprocess - fail to sound_dev_prepare");
//		return;
//	}
	ms_mutex_init(&d->mutex,NULL);
	d->rb=ms_bufferizer_new();
	if(d->rb==NULL){
		ms_error("linuxaudio.c sound_read_preprocess - fail to ms_bufferizer_new");
		return;
	}
	d->start_time=0;
	d->isRunning=1;
	int rc;
	rc = ms_thread_create(&d->threadid, 0, (void*(*)(void*))ms_linux_read_cb, d);
	if (rc){
		ms_error("cannot create read thread return code  is [%i]", rc);
		d->isRunning=0;
	}
}

static void sound_read_postprocess(MSFilter *f){
	ms_linux_sound_read_data *d=(ms_linux_sound_read_data*)f->data;
	d->isRunning=0;
	if (d->threadid !=0) ms_thread_join(d->threadid,0);
	d->threadid=0;
	if(d->rb){
		ms_bufferizer_flush(d->rb);
		ms_bufferizer_destroy(d->rb);
		d->rb=NULL;
	}

	if(d->read_buff) free(d->read_buff);
	if(d->handle)
		close_sound_dev(d->handle);
	ms_mutex_destroy(&d->mutex);

	ms_free(f->data);
}


static void sound_read_process(MSFilter *f){
	ms_linux_sound_read_data *d=(ms_linux_sound_read_data*)f->data;
	if(!d->isRunning) return;
//	uint64_t currentTime=get_cur_time_ms();
//	if(currentTime-d->start_time<30) return;
//	d->start_time=currentTime;
//	if(f->ticker->time % 30 !=0) return;
	int avail;
	int nbytes=d->frames;
	mblk_t *om;
//	ms_mutex_lock(&d->mutex);
	avail=ms_bufferizer_get_avail(d->rb);
//	printf("ken debug, avail=%d time=%lld\n",avail,get_cur_time_ms());
	if(avail>0){
//		printf("ken debug, avail=%d time=%d\n",avail,f->ticker->time);
		if(avail>16000){ //drop the data which is 2 second delay
			ms_bufferizer_flush(d->rb);
			return;
		}
		int size=avail>nbytes?nbytes:avail;
		om=allocb(size,0);
//		ms_mutex_lock(&d->mutex);
		ms_bufferizer_read(d->rb,om->b_wptr,size);
//		ms_mutex_unlock(&d->mutex);
		om->b_wptr+=size;
		ms_queue_put(f->outputs[0],om);
//		avail-=nbytes;
	}
//	ms_mutex_unlock(&d->mutex);
}

MSFilterDesc ms_linux_sound_read_desc={
///*.id=*/MS_FILTER_PLUGIN_ID,
	MS_LINUX_SOUND_READ_ID,
/*.name=*/"MSLinuxSoundRead",
/*.text=*/N_("Sound capture filter for linux"),
/*.category=*/MS_FILTER_OTHER,
/*.enc_fmt*/NULL,
/*.ninputs=*/0,
/*.noutputs=*/1,
/*.init*/NULL,
/*.preprocess=*/sound_read_preprocess,
/*.process=*/sound_read_process,
/*.postprocess=*/sound_read_postprocess,
/*.uninit*/NULL,
/*.methods=*/ms_linux_sound_read_methods
};

static void* ms_linux_write_cb(ms_linux_sound_write_data* d) {
	int bufferSize;
	int max_size=d->frames*2;
	int write_size=d->frames;
	int ret;
	unsigned char writebuffer[1024*4];
	set_high_prio();
	ms_mutex_lock(&d->mutex);
	ms_bufferizer_flush(d->bufferizer);
	ms_mutex_unlock(&d->mutex);
	while(d->isRunning){
//		printf("ms_linux_write_cb is running, rate=%d\n",d->frames);
		while((bufferSize = ms_bufferizer_get_avail(d->bufferizer)) >= max_size){
			if(bufferSize>16000){ //drop the data which is 2 second delay
				ms_warning("we are late, flushing 16000 bytes");
				ms_mutex_lock(&d->mutex);
				ms_bufferizer_skip_bytes(d->bufferizer,16000);
				ms_mutex_unlock(&d->mutex);
				continue;
			}
			ms_mutex_lock(&d->mutex);
			ms_bufferizer_read(d->bufferizer, writebuffer, max_size);
			ms_mutex_unlock(&d->mutex);
			while((ret=sound_write(d->handle,writebuffer,write_size))<0){
				if(ret==-EPIPE){
					sound_dev_prepare(d->handle);
					continue;
				}else{
					break;
				}
			}
		}
		if (d->isRunning) {
			d->isSleeping=1;
			ms_mutex_lock(&d->mutex);
			ms_cond_wait(&d->cond,&d->mutex);
			ms_mutex_unlock(&d->mutex);
			d->isSleeping=0;
		}
	}
	ms_thread_exit(NULL);
	return NULL;
}



static void sound_write_preprocess(MSFilter *f){
//	printf("sound_write_preprocess\n");
	f->data=ms_new0(ms_linux_sound_write_data,1);
	ms_linux_sound_write_data *d=(ms_linux_sound_write_data*)f->data;
	d->handle=open_playback(8000,1,16,&(d->frames));
	if(d->handle==NULL){
		printf("linuxaudio.c sound_write_init - fail to open playback\n");
		return;
	}
//	d->write_buff=(unsigned char*)calloc(d->frames*4,sizeof(unsigned char));
//	if(d->write_buff==NULL){
//			printf("linuxaudio.c sound_write_init - fail to calloc write buffer\n");
//			return;
//	}
	if(sound_dev_prepare(d->handle)!=0){
		ms_error("linuxaudio.c sound_write_init - fail to sound_dev_prepare");
		return;
	}
	ms_mutex_init(&d->mutex,NULL);
	ms_cond_init(&d->cond,NULL);
	d->bufferizer=ms_bufferizer_new();

	d->isRunning=1;
	int rc=0;
	rc = ms_thread_create(&d->threadid, 0, (void*(*)(void*))ms_linux_write_cb, d);
	if (rc){
		ms_error("cannot create write thread return code  is [%i]", rc);
		d->isRunning=0;
		return;
	}

//	printf("sound_write_preprocess done!\n");
}



static void sound_write_postprocess(MSFilter *f){
	ms_linux_sound_write_data *d=(ms_linux_sound_write_data*)f->data;
	d->isRunning=0;
	ms_mutex_lock(&d->mutex);
	ms_cond_signal(&d->cond);
	ms_mutex_unlock(&d->mutex);
	ms_thread_join(d->threadid,0);
	ms_mutex_lock(&d->mutex);
	if(d->bufferizer){
		ms_bufferizer_flush(d->bufferizer);
		ms_bufferizer_destroy(d->bufferizer);
	}
	ms_mutex_unlock(&d->mutex);
//	if(d->write_buff) free(d->write_buff);
	if(d->handle)
		close_sound_dev(d->handle);
	ms_mutex_destroy(&d->mutex);
	ms_cond_destroy(&d->cond);
	ms_free(f->data);
//	printf("sound_write_postprocess done!\n");
}

static void sound_write_process(MSFilter *f){
//	printf("sound_write_process\n");
	ms_linux_sound_write_data *d=(ms_linux_sound_write_data*)f->data;
	mblk_t *m;
	while((m=ms_queue_get(f->inputs[0]))!=NULL){
//		printf("ms_queue_get is not NULL\n");
		if (d->isRunning){
			ms_mutex_lock(&d->mutex);
			ms_bufferizer_put(d->bufferizer,m);
			if (d->isSleeping)
				ms_cond_signal(&d->cond);
//			d->last_sample_date=f->ticker->time;
			ms_mutex_unlock(&d->mutex);
		}else freemsg(m);
	}
//	printf("sound_write_process done!\n");
}

MSFilterMethod ms_linux_sound_write_methods[]={
	{	0				, NULL		}
};

MSFilterDesc ms_linux_sound_write_desc={
///*.id=*/MS_FILTER_PLUGIN_ID,
	MS_LINUX_SOUND_WRITE_ID,
/*.name=*/"MSLinuxSoundWrite",
/*.text=*/N_("Sound write filter for linux"),
/*.category=*/MS_FILTER_OTHER,
/*.enc_fmt*/NULL,
/*.ninputs=*/1,
/*.noutputs=*/0,
/*.init*/NULL,
/*.preprocess=*/sound_write_preprocess,
/*.process=*/sound_write_process,
/*.postprocess=*/sound_write_postprocess,
/*.uninit*/NULL,
/*.methods=*/ms_linux_sound_write_methods
};

//MSFilter *ms_linux_sound_read_new(MSSndCard *card){
//	printf("ms_linux_sound_read_new\n");
//	MSFilter *f=ms_filter_new_from_desc(&ms_linux_sound_read_desc);
//	f->data=calloc(1,sizeof(ms_linux_sound_read_data));
//	return f;
//}

MS_FILTER_DESC_EXPORT(ms_linux_sound_read_desc)
MS_FILTER_DESC_EXPORT(ms_linux_sound_write_desc)
