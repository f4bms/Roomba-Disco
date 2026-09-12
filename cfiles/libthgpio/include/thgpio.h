#ifndef THGPIO_H
#define THGPIO_H

#include <stdbool.h>

// MOTORES
void setSpeed(int speed);
void move_left();
void move_right();

// SENSORES
float get_distance(int sensorID);

// LEDS
void ledSet(int led_id, bool state);

// AUDIO CONCURRENTE (PTHREADS)
void trigger_notification_audio(const char* path);

#endif
