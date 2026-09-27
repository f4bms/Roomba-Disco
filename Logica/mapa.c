#include "mapa.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int mundo_a_celda(const mapa_t *mapa, double x_mm, double y_mm,
                         int *cell_x, int *cell_y) {
    int x;
    int y;

    if (mapa == NULL || cell_x == NULL || cell_y == NULL) return -EINVAL;
    x = mapa->config.origin_x + (int)floor(x_mm / mapa->config.resolution_mm);
    y = mapa->config.origin_y + (int)floor(y_mm / mapa->config.resolution_mm);
    if (x < 0 || x >= mapa->config.width || y < 0 || y >= mapa->config.height) {
        return -ERANGE;
    }
    *cell_x = x;
    *cell_y = y;
    return 0;
}

static unsigned char *celda(mapa_t *mapa, int x, int y) {
    return &mapa->cells[(size_t)y * (size_t)mapa->config.width + (size_t)x];
}

int mapa_init(mapa_t *mapa, mapa_config_t config) {
    size_t count;

    if (mapa == NULL || config.width <= 0 || config.height <= 0
            || config.resolution_mm <= 0.0) return -EINVAL;
    count = (size_t)config.width * (size_t)config.height;
    mapa->cells = calloc(count, sizeof(*mapa->cells));
    if (mapa->cells == NULL) return -ENOMEM;
    mapa->config = config;
    return 0;
}

void mapa_cleanup(mapa_t *mapa) {
    if (mapa == NULL) return;
    free(mapa->cells);
    mapa->cells = NULL;
}

void mapa_reset(mapa_t *mapa) {
    if (mapa == NULL || mapa->cells == NULL) return;
    memset(mapa->cells, MAPA_DESCONOCIDA,
           mapa_cantidad_celdas(mapa) * sizeof(*mapa->cells));
}

int mapa_actualizar_pose(mapa_t *mapa, const odometria_pose_t *pose) {
    int x;
    int y;

    if (pose == NULL) return -EINVAL;
    if (mundo_a_celda(mapa, pose->x_mm, pose->y_mm, &x, &y) != 0) return -ERANGE;
    if (*celda(mapa, x, y) != MAPA_OBSTACULO) *celda(mapa, x, y) = MAPA_VISITADA;
    return 0;
}

int mapa_observar(mapa_t *mapa,
                  const odometria_pose_t *pose,
                  double angulo_sensor_rad,
                  double distancia_mm,
                  int obstaculo) {
    double step_mm;
    double x_mm;
    double y_mm;
    int previous_x = -1;
    int previous_y = -1;
    int x;
    int y;

    if (mapa == NULL || pose == NULL || distancia_mm < 0.0) return -EINVAL;
    step_mm = mapa->config.resolution_mm / 2.0;
    for (double traveled = 0.0; traveled <= distancia_mm; traveled += step_mm) {
        x_mm = pose->x_mm + traveled * cos(pose->theta_rad + angulo_sensor_rad);
        y_mm = pose->y_mm + traveled * sin(pose->theta_rad + angulo_sensor_rad);
        if (mundo_a_celda(mapa, x_mm, y_mm, &x, &y) != 0) break;
        if (x == previous_x && y == previous_y) continue;
        previous_x = x;
        previous_y = y;
        if (traveled < distancia_mm || !obstaculo) {
            if (*celda(mapa, x, y) != MAPA_OBSTACULO) *celda(mapa, x, y) = MAPA_VISITADA;
        } else {
            *celda(mapa, x, y) = MAPA_OBSTACULO;
        }
    }
    return 0;
}

const unsigned char *mapa_obtener_celdas(const mapa_t *mapa) {
    return mapa == NULL ? NULL : mapa->cells;
}

size_t mapa_cantidad_celdas(const mapa_t *mapa) {
    return mapa == NULL ? 0 : (size_t)mapa->config.width * (size_t)mapa->config.height;
}