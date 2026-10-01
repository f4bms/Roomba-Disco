 #ifndef ROOMBATECA_CONTROL_H
 #define ROOMBATECA_CONTROL_H

 #ifdef __cplusplus
 extern "C" {
 #endif

 #include <stdbool.h>
 #include "encoders.h"
 #include "sensores.h"

 int roombateca_control_init(void);
 void roombateca_control_cleanup(void);
 int roombateca_set_motion(const char *direction, int speed);

int roombateca_read_sensors(float distances[SENSOR_CANTIDAD], bool valid[SENSOR_CANTIDAD]);
int roombateca_read_encoders(encoder_lectura_t readings[ENCODER_CANTIDAD]);

 #ifdef __cplusplus
 }
 #endif

 #endif
