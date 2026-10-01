/* Implementación temporal (sin GPIO real) para probar el contrato de
 * motores.h antes de tener el mapa de pines cerrado. */

#include "motores.h"

#include <pthread.h>
#include <stdio.h>

static int velocidad_izquierda = 0;
static int velocidad_derecha = 0;
static int inicializado = 0;
static pthread_mutex_t motores_mutex = PTHREAD_MUTEX_INITIALIZER;

static int velocidad_valida(int velocidad) {
    return velocidad >= -100 && velocidad <= 100;
}

int motor_control_init(void) {
     pthread_mutex_lock(&motores_mutex);
    velocidad_izquierda = 0;
    velocidad_derecha = 0;
    inicializado = 1;
     pthread_mutex_unlock(&motores_mutex);
    printf("[motores] init\n");
    return 0;
}

void motor_control_cleanup(void) {
     pthread_mutex_lock(&motores_mutex);
    velocidad_izquierda = 0;
    velocidad_derecha = 0;
    inicializado = 0;
     pthread_mutex_unlock(&motores_mutex);
    printf("[motores] cleanup\n");
}

int motor_izquierdo_set(int velocidad) {
    if (!inicializado || !velocidad_valida(velocidad)) {
        return -1;
    }
     pthread_mutex_lock(&motores_mutex);
    velocidad_izquierda = velocidad;
     pthread_mutex_unlock(&motores_mutex);
    printf("[motores] izquierdo = %d\n", velocidad);
    return 0;
}

int motor_derecho_set(int velocidad) {
    if (!inicializado || !velocidad_valida(velocidad)) {
        return -1;
    }
     pthread_mutex_lock(&motores_mutex);
    velocidad_derecha = velocidad;
     pthread_mutex_unlock(&motores_mutex);
    printf("[motores] derecho = %d\n", velocidad);
    return 0;
}

int motor_izquierdo_get(void) {
    int velocidad;

    pthread_mutex_lock(&motores_mutex);
    velocidad = inicializado ? velocidad_izquierda : 0;
    pthread_mutex_unlock(&motores_mutex);
    return velocidad;
}

int motor_derecho_get(void) {
    int velocidad;

    pthread_mutex_lock(&motores_mutex);
    velocidad = inicializado ? velocidad_derecha : 0;
    pthread_mutex_unlock(&motores_mutex);
    return velocidad;
}

int motores_frenar(void) {
    pthread_mutex_lock(&motores_mutex);
    if (!inicializado) {
        pthread_mutex_unlock(&motores_mutex);
        return -1;
    }
    velocidad_izquierda = 0;
    velocidad_derecha = 0;
    pthread_mutex_unlock(&motores_mutex);
    printf("[motores] freno\n");
    return 0;
}
