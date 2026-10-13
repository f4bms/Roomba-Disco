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
#define MAPA_LIBRE_OBSERVADA 3
// Paso con la aspiradora encendida. Solo para la Logica (el modo AUTO lo usa para saber que falta limpiar)
// A Cliente se le reporta como MAPA_VISITADA
#define MAPA_ASPIRADA 4
#define MAPA_DIMENSION_MAXIMA 100  /* celdas de 100 mm: hasta 10 m por lado */

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
// Marca como aspirada la franja de la boquilla, perpendicular al rumbo, centrada.
int mapa_marcar_aspirada(mapa_t *mapa, const odometria_pose_t *pose, double ancho_mm);
// Marca un obstaculo
int mapa_marcar_obstaculo(mapa_t *mapa, double x_mm, double y_mm);
// Conversion punto <-> celda. mapa_celda_de_punto devuelve != 0 si el punto cae fuera del mapa
// mapa_punto_de_celda da el centro de la celda.
int mapa_celda_de_punto(const mapa_t *mapa, double x_mm, double y_mm, int *cell_x, int *cell_y);
void mapa_punto_de_celda(const mapa_t *mapa, int cell_x, int cell_y, double *x_mm, double *y_mm);
// Valor de una celda; fuera del mapa devuelve MAPA_DESCONOCIDA
unsigned char mapa_valor(const mapa_t *mapa, int cell_x, int cell_y);
const unsigned char *mapa_obtener_celdas(const mapa_t *mapa);
size_t mapa_cantidad_celdas(const mapa_t *mapa);

#ifdef __cplusplus
}
#endif

#endif