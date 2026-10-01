#ifndef MAPA_H
#define MAPA_H

#include <stddef.h>

#include "odometria.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAPA_DESCONOCIDA 0
#define MAPA_VISITADA 1
#define MAPA_OBSTACULO 2

typedef struct {
    int width;
    int height;
    double resolution_mm;
    int origin_x;
    int origin_y;
} mapa_config_t;

typedef struct {
    mapa_config_t config;
    unsigned char *cells;
} mapa_t;

int mapa_init(mapa_t *mapa, mapa_config_t config);
void mapa_cleanup(mapa_t *mapa);
void mapa_reset(mapa_t *mapa);
int mapa_actualizar_pose(mapa_t *mapa, const odometria_pose_t *pose);
int mapa_observar(mapa_t *mapa,
                  const odometria_pose_t *pose,
                  double angulo_sensor_rad,
                  double distancia_mm,
                  int obstaculo);
const unsigned char *mapa_obtener_celdas(const mapa_t *mapa);
size_t mapa_cantidad_celdas(const mapa_t *mapa);

#ifdef __cplusplus
}
#endif

#endif