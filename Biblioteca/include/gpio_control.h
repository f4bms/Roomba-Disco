#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_INPUT = 0,
    GPIO_OUTPUT = 1
} gpio_mode_t;

/* Configura un pin como entrada o salida. Debe llamarse antes de
 * digitalWrite/digitalRead sobre ese pin. Devuelve 0 en éxito, -1 en error. */
int pinMode(int pin, gpio_mode_t mode);

/* Escribe 0 o 1 en un pin ya configurado como salida. */
int digitalWrite(int pin, int value);

/* Lee el estado de un pin ya configurado (entrada o salida). Devuelve 0/1,
 * o -1 en error. */
int digitalRead(int pin);

/* Alterna un pin a la frecuencia dada (Hz) durante duration segundos. Si el
 * pin no fue configurado antes, lo configura como salida. Bloqueante. */
int blink(int pin, double freq, double duration);

/* Libera todas las líneas solicitadas y cierra el chip GPIO. */
void gpio_control_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
