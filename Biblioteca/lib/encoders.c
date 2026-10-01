#define _POSIX_C_SOURCE 200809L

#include "encoders.h"

#include "motores.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VELOCIDAD_SIMULADA_MAX_MM_S 500.0
#define PULSOS_SIMULADOS_POR_MM 1.0

static encoder_lectura_t lecturas[ENCODER_CANTIDAD];
static struct timespec ultima_actualizacion;
static int inicializado = 0;
static pthread_mutex_t encoders_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t tiempo_a_ns(struct timespec tiempo) {
    return (uint64_t)tiempo.tv_sec * 1000000000ULL + (uint64_t)tiempo.tv_nsec;
}

static void actualizar_lecturas(void) {
    struct timespec ahora;
    double intervalo_s;
    int velocidades[ENCODER_CANTIDAD];
    int encoder;

    clock_gettime(CLOCK_MONOTONIC, &ahora);
    intervalo_s = (double)(tiempo_a_ns(ahora) - tiempo_a_ns(ultima_actualizacion)) / 1000000000.0;
    if (intervalo_s < 0.0) intervalo_s = 0.0;
    ultima_actualizacion = ahora;
    velocidades[ENCODER_IZQUIERDO] = motor_izquierdo_get();
    velocidades[ENCODER_DERECHO] = motor_derecho_get();

    for (encoder = 0; encoder < ENCODER_CANTIDAD; ++encoder) {
        double velocidad_mm_s = velocidades[encoder] * VELOCIDAD_SIMULADA_MAX_MM_S / 100.0;
        lecturas[encoder].velocidad_mm_s = velocidad_mm_s;
        lecturas[encoder].distancia_mm += velocidad_mm_s * intervalo_s;
        lecturas[encoder].pulsos = (int64_t)(lecturas[encoder].distancia_mm * PULSOS_SIMULADOS_POR_MM);
        lecturas[encoder].ultimo_pulso_ns = velocidad_mm_s == 0.0
            ? lecturas[encoder].ultimo_pulso_ns
            : tiempo_a_ns(ahora);
    }
}

int encoders_init(void) {
    pthread_mutex_lock(&encoders_mutex);
    memset(lecturas, 0, sizeof(lecturas));
    clock_gettime(CLOCK_MONOTONIC, &ultima_actualizacion);
    inicializado = 1;
    pthread_mutex_unlock(&encoders_mutex);
    printf("[encoders] encoders_init\n");
    return 0;
}

void encoders_cleanup(void) {
    pthread_mutex_lock(&encoders_mutex);
    inicializado = 0;
    pthread_mutex_unlock(&encoders_mutex);
    printf("[encoders] encoders_cleanup\n");
}

int encoder_leer(encoder_id_t id, encoder_lectura_t *lectura) {
    if (lectura == NULL || id < 0 || id >= ENCODER_CANTIDAD) return -EINVAL;
    pthread_mutex_lock(&encoders_mutex);
    if (!inicializado) {
        pthread_mutex_unlock(&encoders_mutex);
        return -1;
    }
    actualizar_lecturas();
    *lectura = lecturas[id];
    pthread_mutex_unlock(&encoders_mutex);
    printf("[encoders] encoder_leer: encoder=%d\n", id);
    return 0;
}

int encoders_reset(void) {
    pthread_mutex_lock(&encoders_mutex);
    if (!inicializado) {
        pthread_mutex_unlock(&encoders_mutex);
        return -1;
    }
    actualizar_lecturas();
    memset(lecturas, 0, sizeof(lecturas));
    pthread_mutex_unlock(&encoders_mutex);
    printf("[encoders] encoders_reset\n");
    return 0;
}