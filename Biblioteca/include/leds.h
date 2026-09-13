#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ledSet(int led_id, bool state);

#ifdef __cplusplus
}
#endif

#endif
