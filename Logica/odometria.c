#include "odometria.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>

#define PI 3.14159265358979323846

static double normalizar_angulo(double theta_rad) {
    while (theta_rad > PI) theta_rad -= 2.0 * PI;
    while (theta_rad < -PI) theta_rad += 2.0 * PI;
    return theta_rad;
}

int odometria_init(odometria_t *odometria, odometria_config_t config) {
    if (odometria == NULL || config.distancia_ruedas_mm <= 0.0) return -EINVAL;

    odometria->config = config;
    odometria_reset(odometria);
    return 0;
}

void odometria_reset(odometria_t *odometria) {
    if (odometria == NULL) return;
    odometria->pose.x_mm = 0.0;
    odometria->pose.y_mm = 0.0;
    odometria->pose.theta_rad = 0.0;
    odometria->distancia_izquierda_anterior_mm = 0.0;
    odometria->distancia_derecha_anterior_mm = 0.0;
    odometria->tiene_lectura_anterior = 0;
}

int odometria_actualizar(odometria_t *odometria,
                         const encoder_lectura_t *izquierdo,
                         const encoder_lectura_t *derecho) {
    double delta_izquierdo_mm;
    double delta_derecho_mm;
    double avance_mm;
    double cambio_theta_rad;
    double theta_medio_rad;

    if (odometria == NULL || izquierdo == NULL || derecho == NULL) return -EINVAL;
    if (!odometria->tiene_lectura_anterior) {
        odometria->distancia_izquierda_anterior_mm = izquierdo->distancia_mm;
        odometria->distancia_derecha_anterior_mm = derecho->distancia_mm;
        odometria->tiene_lectura_anterior = 1;
        return 0;
    }

    delta_izquierdo_mm = izquierdo->distancia_mm - odometria->distancia_izquierda_anterior_mm;
    delta_derecho_mm = derecho->distancia_mm - odometria->distancia_derecha_anterior_mm;
    odometria->distancia_izquierda_anterior_mm = izquierdo->distancia_mm;
    odometria->distancia_derecha_anterior_mm = derecho->distancia_mm;

    avance_mm = (delta_izquierdo_mm + delta_derecho_mm) / 2.0;
    cambio_theta_rad = (delta_derecho_mm - delta_izquierdo_mm)
        / odometria->config.distancia_ruedas_mm;
    theta_medio_rad = odometria->pose.theta_rad + cambio_theta_rad / 2.0;

    odometria->pose.x_mm += avance_mm * cos(theta_medio_rad);
    odometria->pose.y_mm += avance_mm * sin(theta_medio_rad);
    odometria->pose.theta_rad = normalizar_angulo(
        odometria->pose.theta_rad + cambio_theta_rad);
    return 0;
}

const odometria_pose_t *odometria_obtener_pose(const odometria_t *odometria) {
    return odometria == NULL ? NULL : &odometria->pose;
}