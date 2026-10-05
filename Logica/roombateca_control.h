 #ifndef ROOMBATECA_CONTROL_H
 #define ROOMBATECA_CONTROL_H

 #ifdef __cplusplus
 extern "C" {
 #endif

 #include <stdbool.h>
 #include "audio_th.h"
 #include "encoders.h"
 #include "sensores.h"

 int roombateca_control_init(void);
 void roombateca_control_cleanup(void);
 int roombateca_set_motion(const char *direction, int speed);
 void roombateca_set_mode_leds(const char *mode);

int roombateca_read_sensors(float distances[SENSOR_CANTIDAD], bool valid[SENSOR_CANTIDAD]);
int roombateca_read_encoders(encoder_lectura_t readings[ENCODER_CANTIDAD]);
int roombateca_audio_play_track(int track);
int roombateca_audio_pause(void);
int roombateca_audio_resume(void);
int roombateca_audio_stop(void);
int roombateca_audio_set_volume(int volume);
audio_estado_t roombateca_audio_get_state(void);
int roombateca_audio_get_volume(void);
int roombateca_audio_obstacle_alert(void);
bool roombateca_audio_available(void);
void roombateca_audio_notify_mode(const char *mode);

 #ifdef __cplusplus
 }
 #endif

 #endif
