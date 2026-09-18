 #ifndef ROOMBATECA_CONTROL_H
 #define ROOMBATECA_CONTROL_H

 #ifdef __cplusplus
 extern "C" {
 #endif

 int roombateca_control_init(void);
 void roombateca_control_cleanup(void);
 int roombateca_set_motion(const char *direction, int speed);

 #ifdef __cplusplus
 }
 #endif

 #endif
