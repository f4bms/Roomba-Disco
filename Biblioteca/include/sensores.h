#ifndef SENSORES_H
#define SENSORES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENSOR_FRONTAL = 0,
    SENSOR_TRASERO,
    SENSOR_CANTIDAD
} sensor_id_t;

/* Rango útil del HC-SR04. */
#define SENSOR_DISTANCIA_MIN_CM   2.0f
#define SENSOR_DISTANCIA_MAX_CM 400.0f

/* Tiempo mínimo entre dos disparos consecutivos, sean del mismo sensor o
 * de sensores distintos, para que no se lean ecos del disparo anterior. */
#define SENSOR_INTERVALO_MIN_MS 60

/* Configura TRIG/ECHO de ambos sensores. Devuelve 0 o -errno. */
int sensores_init(void);

void sensores_cleanup(void);

/* Dispara el sensor y mide. Bloquea hasta ~SENSOR_INTERVALO_MIN_MS si hubo
 * un disparo reciente, más la duración del eco (≤ ~25 ms).
 * Seguro entre hilos. Devuelve 0 con la distancia en *distancia_cm,
 * -ETIMEDOUT si no hubo eco (nada en rango), -EIO si el sensor no
 * respondió al disparo (¿desconectado?), -ENODEV sin init, u otro -errno. */
int sensor_medir(sensor_id_t id, float *distancia_cm);

#ifdef __cplusplus
}
#endif

#endif
