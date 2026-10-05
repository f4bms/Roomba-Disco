/* Aplicación de verificación de encoders.h.
 *
 * Imprime pulsos, distancia y velocidad de ambas ruedas cada 200 ms:
 *   test_encoders                 girar las ruedas a mano, hasta Ctrl+C
 *   test_encoders -m <vel> [<s>]  mueve ambos motores a <vel> (-100..100)
 *                                 durante <s> segundos (5 por defecto)
 * Sin -m no se inician los motores y todo pulso cuenta hacia adelante.
 * Para calibrar: marcar la rueda, dar 10 vueltas exactas a mano y comparar
 * los pulsos con 10 × 2 × ranuras.
 *
 * Compilar enlazando contra libroombateca, p.ej.:
 *   $CC test/test_encoders.c -Iinclude -L<dir-con-libroombateca.so> \
 *       -lroombateca -o test_encoders */

#include "encoders.h"
#include "motores.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t detener = 0;

static void al_recibir_senal(int sig) {
    (void)sig;
    detener = 1;
}

static double segundos_desde(const struct timespec *inicio) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - inicio->tv_sec) + (t.tv_nsec - inicio->tv_nsec) / 1e9;
}

static int uso(const char *prog) {
    fprintf(stderr, "Uso: %s [-m <velocidad> [<segundos>]]\n", prog);
    return 2;
}

int main(int argc, char *argv[]) {
    int con_motores = 0;
    long velocidad = 0;
    double duracion = 0;

    if (argc > 1) {
        char *fin;
        if (argc < 3 || argc > 4 || strcmp(argv[1], "-m") != 0) {
            return uso(argv[0]);
        }
        velocidad = strtol(argv[2], &fin, 10);
        if (*fin || velocidad < -MOTOR_VELOCIDAD_MAX || velocidad > MOTOR_VELOCIDAD_MAX) {
            return uso(argv[0]);
        }
        duracion = argc == 4 ? strtod(argv[3], &fin) : 5.0;
        if (argc == 4 && (*fin || duracion <= 0)) {
            return uso(argv[0]);
        }
        con_motores = 1;
    }

    struct sigaction sa = { .sa_handler = al_recibir_senal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int rv;
    if (con_motores && (rv = motor_control_init()) < 0) {
        fprintf(stderr, "motor_control_init: %s\n", strerror(-rv));
        return 1;
    }
    rv = encoders_init();
    if (rv < 0) {
        fprintf(stderr, "encoders_init: %s\n", strerror(-rv));
        motor_control_cleanup();
        return 1;
    }

    if (con_motores) {
        motor_izquierdo_set((int)velocidad);
        motor_derecho_set((int)velocidad);
        printf("Motores a %ld durante %.1f s\n", velocidad, duracion);
    } else {
        printf("Girá las ruedas a mano; Ctrl+C para terminar\n");
    }
    printf("%7s  %10s %10s %9s  %10s %10s %9s\n", "t [s]",
           "izq pulsos", "izq mm", "izq mm/s", "der pulsos", "der mm", "der mm/s");

    struct timespec inicio;
    clock_gettime(CLOCK_MONOTONIC, &inicio);
    struct timespec periodo = { .tv_sec = 0, .tv_nsec = 200 * 1000000L };

    while (!detener && (!con_motores || segundos_desde(&inicio) < duracion)) {
        encoder_lectura_t l[ENCODER_CANTIDAD];
        for (int i = 0; i < ENCODER_CANTIDAD; i++) {
            rv = encoder_leer((encoder_id_t)i, &l[i]);
            if (rv < 0) {
                fprintf(stderr, "encoder_leer(%d): %s\n", i, strerror(-rv));
                detener = 1;
            }
        }
        if (!detener) {
            printf("%7.1f  %10lld %10.1f %9.1f  %10lld %10.1f %9.1f\n",
                   segundos_desde(&inicio),
                   (long long)l[ENCODER_IZQUIERDO].pulsos,
                   l[ENCODER_IZQUIERDO].distancia_mm, l[ENCODER_IZQUIERDO].velocidad_mm_s,
                   (long long)l[ENCODER_DERECHO].pulsos,
                   l[ENCODER_DERECHO].distancia_mm, l[ENCODER_DERECHO].velocidad_mm_s);
            fflush(stdout);
        }
        nanosleep(&periodo, NULL);
    }

    if (con_motores) {
        motores_frenar();
        /* Deja que la rueda termine de girar para contar esos pulsos también. */
        sleep(1);
    }

    encoder_lectura_t fin[ENCODER_CANTIDAD];
    encoder_leer(ENCODER_IZQUIERDO, &fin[ENCODER_IZQUIERDO]);
    encoder_leer(ENCODER_DERECHO, &fin[ENCODER_DERECHO]);
    encoder_lectura_t basura;
    int rv_invalido = encoder_leer(ENCODER_CANTIDAD, &basura);

    encoders_cleanup();
    motor_control_cleanup();

    printf("\nTotal izq: %lld pulsos (%.1f mm)   der: %lld pulsos (%.1f mm)\n",
           (long long)fin[ENCODER_IZQUIERDO].pulsos, fin[ENCODER_IZQUIERDO].distancia_mm,
           (long long)fin[ENCODER_DERECHO].pulsos, fin[ENCODER_DERECHO].distancia_mm);
    printf("%-34s %s\n", "Encoder inválido",
           rv_invalido == -EINVAL ? "rechazado con EINVAL (ok)" : "NO se rechazó (falla)");
    return 0;
}
