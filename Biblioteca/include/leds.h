#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_ENCENDIDO = 0,  /* verde */
    LED_ALERTA,         /* rojo */
    LED_MANUAL,         /* amarillo */
    LED_AUTONOMO,       /* azul */
    LED_CANTIDAD
} led_id_t;

/* Configura los pines de los LEDs y los deja apagados. Devuelve 0 o -errno. */
int leds_init(void);

/* Apaga todos los LEDs y libera sus pines. */
void leds_cleanup(void);

int led_set(led_id_t led, bool encendido);

/* Devuelve 1 si está encendido, 0 si no, o -errno. */
int led_get(led_id_t led);

#ifdef __cplusplus
}
#endif

#endif
