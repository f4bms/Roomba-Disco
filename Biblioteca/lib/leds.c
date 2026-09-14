#include "leds.h"

#include <stdio.h>

void ledSet(int led_id, bool state) {
	printf("[PRUEBA] LED %d cambiado a %d\n", led_id, state);
}
