#include "msfilter.h"

//extern MSFilterDesc ms_alaw_dec_desc;
//extern MSFilterDesc ms_alaw_enc_desc;
//extern MSFilterDesc ms_ulaw_dec_desc;
//extern MSFilterDesc ms_ulaw_enc_desc;
extern MSFilterDesc ms_rtp_send_desc;
extern MSFilterDesc ms_rtp_recv_desc;
//extern MSFilterDesc ms_dtmf_gen_desc;
extern MSFilterDesc ms_ice_desc;
/*extern MSFilterDesc ms_tee_desc;
extern MSFilterDesc ms_conf_desc*/;
//extern MSFilterDesc ms_join_desc;
extern MSFilterDesc ms_volume_desc;
//extern MSFilterDesc ms_void_sink_desc;
//extern MSFilterDesc ms_equalizer_desc;
//extern MSFilterDesc ms_channel_adapter_desc;
//extern MSFilterDesc ms_audio_mixer_desc;
//extern MSFilterDesc ms_itc_source_desc;
//extern MSFilterDesc ms_itc_sink_desc;
//extern MSFilterDesc ms_speex_dec_desc;
//extern MSFilterDesc ms_speex_enc_desc;
//extern MSFilterDesc ms_speex_ec_desc;
//extern MSFilterDesc ms_gsm_dec_desc;
//extern MSFilterDesc ms_gsm_enc_desc;
//extern MSFilterDesc ms_file_player_desc;
//extern MSFilterDesc ms_file_rec_desc;
//extern MSFilterDesc ms_resample_desc;
//extern MSFilterDesc ms_g722_enc_desc;
//extern MSFilterDesc ms_g722_dec_desc;
//extern MSFilterDesc ms_mpeg4_dec_desc;
//extern MSFilterDesc ms_mpeg4_enc_desc;
//extern MSFilterDesc ms_size_conv_desc;
//
//
//extern MSFilterDesc ms_h264_dec_desc;
//extern MSFilterDesc ms_h264_enc_desc;
//#ifdef ANDROID
//extern MSFilterDesc ms_android_video_capture_desc;
//#endif
extern MSFilterDesc ms_silk_dec_desc;
extern MSFilterDesc ms_silk_enc_desc;
//extern MSFilterDesc ms_resample_desc;
//extern MSFilterDesc ms_slience_desc;
//#ifndef _IS_PHONE_
//extern MSFilterDesc ms_icam_audio_rec_desc;
//extern MSFilterDesc ms_icam_video_rec_desc;
//#endif
//#ifdef ANDROID
//extern MSFilterDesc ms_android_opengl_display_desc;
//#endif
//extern MSFilterDesc ms_ext_input_desc;
//extern MSFilterDesc ms_icam_h264_dec_desc;
//extern MSFilterDesc ms_webrtc_aec_desc;
//#ifdef WIN32
//extern MSFilterDesc ms_dd_display_desc;
//#endif
//#ifdef __APPLE__
//extern MSFilterDesc ms_iosdisplay_desc;
//#endif
extern MSFilterDesc ms_linux_video_capture_desc;
extern MSFilterDesc ms_linux_sound_read_desc;
extern MSFilterDesc ms_linux_sound_write_desc;

MSFilterDesc * ms_filter_descs[]={
//&ms_alaw_dec_desc,
//&ms_alaw_enc_desc,
//&ms_ulaw_dec_desc,
//&ms_ulaw_enc_desc,
&ms_rtp_send_desc,
&ms_rtp_recv_desc,
//&ms_dtmf_gen_desc,
&ms_ice_desc,
//&ms_tee_desc,
//&ms_conf_desc,
//&ms_join_desc,
&ms_volume_desc,
//&ms_void_sink_desc,
//&ms_equalizer_desc,
//&ms_channel_adapter_desc,
//&ms_audio_mixer_desc,
//&ms_itc_source_desc,
//&ms_itc_sink_desc,
//&ms_speex_dec_desc,
//&ms_speex_enc_desc,
//&ms_speex_ec_desc,
//&ms_gsm_dec_desc,
//&ms_gsm_enc_desc,
//&ms_file_player_desc,
//&ms_file_rec_desc,
//&ms_resample_desc,
//&ms_g722_enc_desc,
//&ms_g722_dec_desc,
//&ms_mpeg4_dec_desc,
//&ms_mpeg4_enc_desc,
//&ms_size_conv_desc,
//&ms_h264_dec_desc,
//&ms_h264_enc_desc,
//#ifdef ANDROID
//&ms_android_video_capture_desc,
//#endif
&ms_silk_dec_desc,
&ms_silk_enc_desc,
//&ms_resample_desc,
//&ms_slience_desc,
//#ifndef _IS_PHONE_
//&ms_icam_audio_rec_desc,
//&ms_icam_video_rec_desc,
//#endif
//#ifdef ANDROID
//&ms_android_opengl_display_desc,
//#endif
//&ms_ext_input_desc,
//&ms_icam_h264_dec_desc,
//&ms_webrtc_aec_desc,
//#ifdef WIN32
//&ms_dd_display_desc,
//#endif
//#ifdef __APPLE__
//&ms_iosdisplay_desc,
//#endif
&ms_linux_video_capture_desc,
&ms_linux_sound_read_desc,
&ms_linux_sound_write_desc,
NULL
};

