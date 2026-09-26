#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_INPUT = 0,
    GPIO_OUTPUT = 1
} gpio_mode_t;

/* Opciones de pinModeEx; se combinan con |. */
#define GPIO_FLAG_ACTIVE_LOW   (1u << 0)
#define GPIO_FLAG_PULL_UP      (1u << 1)
#define GPIO_FLAG_PULL_DOWN    (1u << 2)
#define GPIO_FLAG_EDGE_RISING  (1u << 3)
#define GPIO_FLAG_EDGE_FALLING (1u << 4)
#define GPIO_FLAG_EDGE_BOTH    (GPIO_FLAG_EDGE_RISING | GPIO_FLAG_EDGE_FALLING)

typedef enum {
    GPIO_EDGE_RISING = 1,
    GPIO_EDGE_FALLING = 2
} gpio_edge_t;

typedef struct {
    gpio_edge_t tipo;
    uint64_t timestamp_ns;  /* CLOCK_MONOTONIC, puesto por el kernel */
} gpio_edge_event_t;

/* Configura un pin como entrada o salida. Debe llamarse antes de
 * digitalWrite/digitalRead sobre ese pin. Devuelve 0 en éxito, -1 en error. */
int pinMode(int pin, gpio_mode_t mode);

/* Como pinMode, con opciones GPIO_FLAG_*. debounce_us > 0 activa el
 * filtro de rebotes del kernel (solo entradas). Si el pin ya estaba
 * configurado, se reconfigura. Devuelve 0 o -errno. */
int pinModeEx(int pin, gpio_mode_t mode, unsigned int flags,
              unsigned int debounce_us);

/* Escribe 0 o 1 en un pin ya configurado como salida. Con
 * GPIO_FLAG_ACTIVE_LOW, 1 deja la línea física en bajo. */
int digitalWrite(int pin, int value);

/* Lee el estado de un pin ya configurado (entrada o salida). Devuelve 0/1,
 * o -1 en error. */
int digitalRead(int pin);

/* Espera un flanco en un pin configurado con GPIO_FLAG_EDGE_*.
 * timeout_ns < 0 espera indefinidamente; 0 no bloquea.
 * Devuelve 1 si llegó un evento (en *evento), 0 si venció el timeout,
 * o -errno. */
int waitEdge(int pin, int64_t timeout_ns, gpio_edge_event_t *evento);

/* Descriptor para poll()/select() sobre los eventos de un pin configurado
 * con GPIO_FLAG_EDGE_*. No se debe cerrar. Devuelve el fd o -errno. */
int edgeFd(int pin);

/* Alterna un pin a la frecuencia dada (Hz) durante duration segundos. Si el
 * pin no fue configurado antes, lo configura como salida. Bloqueante. */
int blink(int pin, double freq, double duration);

/* Libera un pin. El kernel no garantiza el estado de la línea después
 * (puede conservar el último valor): dejarlo en estado seguro antes. */
void pinRelease(int pin);

/* Libera todas las líneas solicitadas y cierra el chip GPIO. */
void gpio_control_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
