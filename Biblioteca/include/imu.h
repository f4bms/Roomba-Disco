#ifndef IMU_H
#define IMU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float acel_g[3];       /* X, Y, Z */
    float giro_dps[3];     /* X, Y, Z, sin sesgo tras imu_calibrar_giro */
    float temperatura_c;
    uint64_t timestamp_ns; /* CLOCK_MONOTONIC */
} imu_lectura_t;

/* Abre el bus, verifica el sensor y lo configura. Devuelve 0 o -errno
 * (-ENODEV si no responde en la dirección esperada). */
int imu_init(void);

void imu_cleanup(void);

/* Seguro entre hilos. Devuelve 0 o -errno. */
int imu_leer(imu_lectura_t *lectura);

/* Promedia `muestras` lecturas del giroscopio y guarda el sesgo. El robot
 * debe estar quieto. Bloquea ~muestras × 10 ms. Devuelve 0 o -errno. */
int imu_calibrar_giro(int muestras);

#ifdef __cplusplus
}
#endif

#endif
