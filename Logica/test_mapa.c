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
    assert(cell(&mapa, 6, 3) == MAPA_DESCONOCIDA);   /* margen antes del eco */
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

    /* eco sin obstaculo: libre hasta 100 mm antes del eco; la celda del eco sin marcar */
    {
        mapa_t m = {0};
        odometria_pose_t p = {0.0, 0.0, 0.0};
        assert(mapa_init(&m, config) == 0);
        assert(mapa_observar(&m, &p, 0.0, 350.0, 0) == 0);
        assert(cell(&m, 6, 3) == MAPA_LIBRE_OBSERVADA);   /* hasta 249 mm */
        assert(cell(&m, 7, 3) == MAPA_DESCONOCIDA);       /* eco a 350 mm */

        /* aspirada: franja de la boquilla perpendicular al rumbo (mirando a +x,
         * 120 mm de ancho cubren y = -60..60 -> celdas 2 y 3) */
        p.x_mm = 50.0;
        p.y_mm = 0.0;
        assert(mapa_marcar_aspirada(&m, &p, 120.0) == 0);
        assert(cell(&m, 4, 3) == MAPA_ASPIRADA);
        assert(cell(&m, 4, 2) == MAPA_ASPIRADA);
        assert(cell(&m, 4, 4) == MAPA_DESCONOCIDA);
        /* pasar de nuevo sin aspirar no la rebaja a visitada */
        assert(mapa_actualizar_pose(&m, &p) == 0);
        assert(cell(&m, 4, 3) == MAPA_ASPIRADA);
        /* un obstaculo marcado no se pisa al aspirar */
        assert(mapa_marcar_obstaculo(&m, 50.0, -50.0) == 0);
        assert(mapa_marcar_aspirada(&m, &p, 120.0) == 0);
        assert(cell(&m, 4, 2) == MAPA_OBSTACULO);

        /* consultas */
        {
            int cx, cy;
            double x, y;
            assert(mapa_celda_de_punto(&m, 150.0, 50.0, &cx, &cy) == 0 && cx == 5 && cy == 3);
            mapa_punto_de_celda(&m, 5, 3, &x, &y);
            assert(x == 150.0 && y == 50.0);
            assert(mapa_valor(&m, -1, 0) == MAPA_DESCONOCIDA);
            assert(mapa_valor(&m, 100, 0) == MAPA_DESCONOCIDA);
            assert(mapa_celda_de_punto(&m, 5000.0, 0.0, &cx, &cy) != 0);
        }
        mapa_cleanup(&m);
    }
    return 0;
}