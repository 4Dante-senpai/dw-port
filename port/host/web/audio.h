#ifndef DW1_WEB_AUDIO_H
#define DW1_WEB_AUDIO_H
void plat_game_init(void);
void *web_audio_address(void);
void *web_audio_delivery_address(void);
void web_audio_enable(int enabled);
void web_audio_replay(int replay);
void web_audio_ramp(int ramp);
#endif
