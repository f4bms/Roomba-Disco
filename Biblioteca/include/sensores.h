#ifndef SENSORES_H
#define SENSORES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENSOR_FRONTAL = 0,
    SENSOR_IZQUIERDO,
    SENSOR_DERECHO,
    SENSOR_CANTIDAD
} sensor_id_t;

/* Rango útil del HC-SR04. */
#define SENSOR_DISTANCIA_MIN_CM   2.0f
#define SENSOR_DISTANCIA_MAX_CM 400.0f

/* Tiempo mínimo entre disparos de cualquier par de sensores. */
#define SENSOR_INTERVALO_MIN_MS 60

/* Configura TRIG/ECHO de los tres sensores. Devuelve 0 o -errno. */
int sensores_init(void);

void sensores_cleanup(void);

/* Dispara el sensor y mide. Bloquea hasta ~SENSOR_INTERVALO_MIN_MS si otro
 * sensor disparó hace poco, más la duración del eco (≤ ~25 ms).
 * Seguro entre hilos. Devuelve 0 con la distancia en *distancia_cm,
 * -ETIMEDOUT si no hubo eco (nada en rango), u otro -errno. */
int sensor_medir(sensor_id_t id, float *distancia_cm);

#ifdef __cplusplus
}
#endif

#endif
