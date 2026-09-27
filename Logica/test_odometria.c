#include "odometria.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

static encoder_lectura_t lectura(double distancia_mm) {
    encoder_lectura_t resultado = {0};
    resultado.distancia_mm = distancia_mm;
    return resultado;
}

static void assert_close(double actual, double esperado) {
    assert(fabs(actual - esperado) < 1e-9);
}

int main(void) {
    odometria_t odometria;
    odometria_config_t config = {.distancia_ruedas_mm = 200.0};
    const odometria_pose_t *pose;
    encoder_lectura_t izquierdo;
    encoder_lectura_t derecho;

    assert(odometria_init(&odometria, config) == 0);
    izquierdo = lectura(0.0);
    derecho = lectura(0.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    izquierdo = lectura(100.0);
    derecho = lectura(100.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    pose = odometria_obtener_pose(&odometria);
    assert_close(pose->x_mm, 100.0);
    assert_close(pose->y_mm, 0.0);
    assert_close(pose->theta_rad, 0.0);

    odometria_reset(&odometria);
    izquierdo = lectura(100.0);
    derecho = lectura(-100.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    izquierdo = lectura(200.0);
    derecho = lectura(-200.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    pose = odometria_obtener_pose(&odometria);
    assert_close(pose->x_mm, 0.0);
    assert_close(pose->y_mm, 0.0);
    assert_close(pose->theta_rad, -1.0);

    odometria_reset(&odometria);
    izquierdo = lectura(0.0);
    derecho = lectura(0.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    izquierdo = lectura(-50.0);
    derecho = lectura(-50.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    pose = odometria_obtener_pose(&odometria);
    assert_close(pose->x_mm, -50.0);
    assert_close(pose->y_mm, 0.0);
    assert_close(pose->theta_rad, 0.0);

    odometria_reset(&odometria);
    izquierdo = lectura(0.0);
    derecho = lectura(0.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    izquierdo = lectura(0.0);
    derecho = lectura(100.0);
    assert(odometria_actualizar(&odometria, &izquierdo, &derecho) == 0);
    pose = odometria_obtener_pose(&odometria);
    assert_close(pose->x_mm, 50.0 * cos(0.25));
    assert_close(pose->y_mm, 50.0 * sin(0.25));
    assert_close(pose->theta_rad, 0.5);

    assert(odometria_actualizar(NULL, &izquierdo, &derecho) != 0);
    assert(odometria_actualizar(&odometria, NULL, &derecho) != 0);
    assert(odometria_init(&odometria, (odometria_config_t){0.0}) != 0);
    return 0;
}