#include "mapa.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int mundo_a_celda(const mapa_t *mapa, double x_mm, double y_mm, int *cell_x, int *cell_y) {
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

static int mundo_a_celda_sin_limite(const mapa_t *mapa, double x_mm, double y_mm, int *cell_x, int *cell_y) {
    if (mapa == NULL || cell_x == NULL || cell_y == NULL) return -EINVAL;
    *cell_x = mapa->config.origin_x + (int)floor(x_mm / mapa->config.resolution_mm);
    *cell_y = mapa->config.origin_y + (int)floor(y_mm / mapa->config.resolution_mm);
    return 0;
}

static int mapa_expandir(mapa_t *mapa, int target_x, int target_y) {
    unsigned char *expanded;
    int new_width = mapa->config.width;
    int new_height = mapa->config.height;
    int add_left;
    int add_top;
    size_t row;

    add_left = target_x < 0 ? -target_x : 0;
    add_top = target_y < 0 ? -target_y : 0;
    new_width += add_left;
    new_height += add_top;
    if (target_x >= new_width) new_width = target_x + 1;
    if (target_y >= new_height) new_height = target_y + 1;
    if (new_width > MAPA_DIMENSION_MAXIMA || new_height > MAPA_DIMENSION_MAXIMA) return -ERANGE;
    if (new_width == mapa->config.width && new_height == mapa->config.height
            && add_left == 0 && add_top == 0) return 0;

    expanded = calloc((size_t)new_width * (size_t)new_height, sizeof(*expanded));
    if (expanded == NULL) return -ENOMEM;
    for (row = 0; row < (size_t)mapa->config.height; ++row) {
        memcpy(expanded + (row + (size_t)add_top) * (size_t)new_width + (size_t)add_left,
               mapa->cells + row * (size_t)mapa->config.width,
               (size_t)mapa->config.width * sizeof(*expanded));
    }
    free(mapa->cells);
    mapa->cells = expanded;
    mapa->config.width = new_width;
    mapa->config.height = new_height;
    mapa->config.origin_x += add_left;
    mapa->config.origin_y += add_top;
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
    if (mundo_a_celda_sin_limite(mapa, pose->x_mm, pose->y_mm, &x, &y) != 0) return -EINVAL;
    if (mapa_expandir(mapa, x, y) != 0) return -ERANGE;
    if (mundo_a_celda(mapa, pose->x_mm, pose->y_mm, &x, &y) != 0) return -ERANGE;
    if (*celda(mapa, x, y) != MAPA_OBSTACULO) *celda(mapa, x, y) = MAPA_VISITADA;
    return 0;
}

int mapa_observar(mapa_t *mapa, const odometria_pose_t *pose, double angulo_sensor_rad, double distancia_mm, int obstaculo) {
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
            if (*celda(mapa, x, y) == MAPA_DESCONOCIDA) {
                *celda(mapa, x, y) = MAPA_LIBRE_OBSERVADA;
            }
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