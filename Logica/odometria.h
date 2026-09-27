#ifndef ODOMETRIA_H
#define ODOMETRIA_H

#include "encoders.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double x_mm;
    double y_mm;
    double theta_rad;
} odometria_pose_t;

typedef struct {
    double distancia_ruedas_mm;
} odometria_config_t;

typedef struct {
    odometria_config_t config;
    odometria_pose_t pose;
    double distancia_izquierda_anterior_mm;
    double distancia_derecha_anterior_mm;
    int tiene_lectura_anterior;
} odometria_t;

int odometria_init(odometria_t *odometria, odometria_config_t config);
void odometria_reset(odometria_t *odometria);
int odometria_actualizar(odometria_t *odometria,
                         const encoder_lectura_t *izquierdo,
                         const encoder_lectura_t *derecho);
const odometria_pose_t *odometria_obtener_pose(const odometria_t *odometria);

#ifdef __cplusplus
}
#endif

#endif