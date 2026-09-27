#define _POSIX_C_SOURCE 200809L

#include "encoders.h"
#include "motores.h"

#include <assert.h>
#include <math.h>
#include <time.h>

static void esperar_milisegundos(long milisegundos) {
    struct timespec espera = {
        .tv_sec = milisegundos / 1000,
        .tv_nsec = (milisegundos % 1000) * 1000000L,
    };
    nanosleep(&espera, NULL);
}

static encoder_lectura_t leer(encoder_id_t id) {
    encoder_lectura_t lectura;
    assert(encoder_leer(id, &lectura) == 0);
    return lectura;
}

int main(void) {
    encoder_lectura_t izquierda_inicial;
    encoder_lectura_t derecha_inicial;
    encoder_lectura_t izquierda_avance;
    encoder_lectura_t derecha_avance;
    encoder_lectura_t izquierda_giro;
    encoder_lectura_t derecha_giro;
    encoder_lectura_t izquierda_freno;
    encoder_lectura_t derecha_freno;

    assert(motor_control_init() == 0);
    assert(encoders_init() == 0);

    izquierda_inicial = leer(ENCODER_IZQUIERDO);
    derecha_inicial = leer(ENCODER_DERECHO);
    assert(motor_izquierdo_set(100) == 0);
    assert(motor_derecho_set(100) == 0);
    esperar_milisegundos(100);
    izquierda_avance = leer(ENCODER_IZQUIERDO);
    derecha_avance = leer(ENCODER_DERECHO);
    assert(izquierda_avance.distancia_mm - izquierda_inicial.distancia_mm > 20.0);
    assert(derecha_avance.distancia_mm - derecha_inicial.distancia_mm > 20.0);

    assert(motor_izquierdo_set(100) == 0);
    assert(motor_derecho_set(-100) == 0);
    esperar_milisegundos(100);
    izquierda_giro = leer(ENCODER_IZQUIERDO);
    derecha_giro = leer(ENCODER_DERECHO);
    assert(izquierda_giro.distancia_mm > izquierda_avance.distancia_mm);
    assert(derecha_giro.distancia_mm < derecha_avance.distancia_mm);

    assert(motores_frenar() == 0);
    esperar_milisegundos(100);
    izquierda_freno = leer(ENCODER_IZQUIERDO);
    derecha_freno = leer(ENCODER_DERECHO);
    assert(fabs(izquierda_freno.distancia_mm - izquierda_giro.distancia_mm) < 20.0);
    assert(fabs(derecha_freno.distancia_mm - derecha_giro.distancia_mm) < 20.0);

    encoders_cleanup();
    motor_control_cleanup();
    return 0;
}