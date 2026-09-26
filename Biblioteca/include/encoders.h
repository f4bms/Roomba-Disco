#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENCODER_IZQUIERDO = 0,
    ENCODER_DERECHO,
    ENCODER_CANTIDAD
} encoder_id_t;

typedef struct {
    int64_t pulsos;          /* acumulados desde el último reset, con signo */
    double distancia_mm;     /* pulsos convertidos, con signo */
    double velocidad_mm_s;   /* con signo; 0 si la rueda está quieta */
    uint64_t ultimo_pulso_ns;/* CLOCK_MONOTONIC; 0 si aún no hubo pulsos */
} encoder_lectura_t;

/* Configura ambos encoders y arranca el hilo de conteo. Requiere
 * motor_control_init() antes (el signo sale de los motores).
 * Devuelve 0 o -errno. */
int encoders_init(void);

void encoders_cleanup(void);

/* Seguro entre hilos. Devuelve 0 o -errno. */
int encoder_leer(encoder_id_t id, encoder_lectura_t *lectura);

/* Pone a cero los contadores de ambas ruedas. */
int encoders_reset(void);

#ifdef __cplusplus
}
#endif

#endif
