/* Implementación temporal (sin GPIO real) para probar el contrato de
 * motores.h antes de tener el mapa de pines cerrado. */

#include "motores.h"

#include <stdio.h>

static int velocidad_izquierda = 0;
static int velocidad_derecha = 0;
static int inicializado = 0;

static int velocidad_valida(int velocidad) {
    return velocidad >= -100 && velocidad <= 100;
}

int motor_control_init(void) {
    velocidad_izquierda = 0;
    velocidad_derecha = 0;
    inicializado = 1;
    printf("[motores] init\n");
    return 0;
}

void motor_control_cleanup(void) {
    velocidad_izquierda = 0;
    velocidad_derecha = 0;
    inicializado = 0;
    printf("[motores] cleanup\n");
}

int motor_izquierdo_set(int velocidad) {
    if (!inicializado || !velocidad_valida(velocidad)) {
        return -1;
    }
    velocidad_izquierda = velocidad;
    printf("[motores] izquierdo = %d\n", velocidad);
    return 0;
}

int motor_derecho_set(int velocidad) {
    if (!inicializado || !velocidad_valida(velocidad)) {
        return -1;
    }
    velocidad_derecha = velocidad;
    printf("[motores] derecho = %d\n", velocidad);
    return 0;
}
