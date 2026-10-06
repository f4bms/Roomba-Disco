#include "mapa.h"

#include <assert.h>
#include <stddef.h>

static unsigned char cell(const mapa_t *mapa, int x, int y) {
    return mapa_obtener_celdas(mapa)[(size_t)y * (size_t)mapa->config.width + (size_t)x];
}

int main(void) {
    mapa_t mapa = {0};
    mapa_t mapa_trasero = {0};
    mapa_config_t config = {
        .width = 8,
        .height = 6,
        .resolution_mm = 100.0,
        .origin_x = 4,
        .origin_y = 3,
    };
    odometria_pose_t pose = {0.0, 0.0, 0.0};

    assert(mapa_init(&mapa, config) == 0);
    assert(mapa_actualizar_pose(&mapa, &pose) == 0);
    assert(cell(&mapa, 4, 3) == MAPA_VISITADA);
    assert(mapa_observar(&mapa, &pose, 0.0, 300.0, 1) == 0);
    assert(cell(&mapa, 5, 3) == MAPA_LIBRE_OBSERVADA);
    assert(cell(&mapa, 6, 3) == MAPA_LIBRE_OBSERVADA);
    assert(cell(&mapa, 7, 3) == MAPA_OBSTACULO);

    pose.x_mm = 100.0;
    assert(mapa_actualizar_pose(&mapa, &pose) == 0);
    assert(cell(&mapa, 5, 3) == MAPA_VISITADA);
    pose.x_mm = 1000.0;
    assert(mapa_actualizar_pose(&mapa, &pose) == 0);
    assert(mapa.config.width == 15);
    assert(cell(&mapa, 14, 3) == MAPA_VISITADA);
    pose.x_mm = MAPA_DIMENSION_MAXIMA * config.resolution_mm;
    assert(mapa_actualizar_pose(&mapa, &pose) != 0);
    pose.x_mm = -500.0;
    assert(mapa_actualizar_pose(&mapa, &pose) == 0);
    assert(mapa.config.width == 16);
    assert(cell(&mapa, 0, 3) == MAPA_VISITADA);
    mapa_reset(&mapa);
    assert(cell(&mapa, 4, 3) == MAPA_DESCONOCIDA);
    mapa_cleanup(&mapa);

    pose.x_mm = 0.0;
    assert(mapa_init(&mapa_trasero, config) == 0);
    assert(mapa_observar(&mapa_trasero, &pose, 3.14159265358979323846, 150.0, 1) == 0);
    assert(cell(&mapa_trasero, 2, 3) == MAPA_OBSTACULO);
    mapa_cleanup(&mapa_trasero);
    return 0;
}