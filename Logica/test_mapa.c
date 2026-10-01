#include "mapa.h"

#include <assert.h>
#include <stddef.h>

static unsigned char cell(const mapa_t *mapa, int x, int y) {
    return mapa_obtener_celdas(mapa)[(size_t)y * (size_t)mapa->config.width + (size_t)x];
}

int main(void) {
    mapa_t mapa = {0};
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
    assert(cell(&mapa, 5, 3) == MAPA_VISITADA);
    assert(cell(&mapa, 6, 3) == MAPA_VISITADA);
    assert(cell(&mapa, 7, 3) == MAPA_OBSTACULO);

    pose.x_mm = 100.0;
    assert(mapa_actualizar_pose(&mapa, &pose) == 0);
    assert(cell(&mapa, 5, 3) == MAPA_VISITADA);
    assert(mapa_actualizar_pose(&mapa, &(odometria_pose_t){1000.0, 0.0, 0.0}) != 0);
    mapa_reset(&mapa);
    assert(cell(&mapa, 4, 3) == MAPA_DESCONOCIDA);
    mapa_cleanup(&mapa);
    return 0;
}