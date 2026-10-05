/* Aplicación de verificación de motores.h.
 *
 * Sin argumentos corre una secuencia: cada motor adelante y atrás, inversión
 * de sentido, freno dinámico y lectura de motor_*_get.
 * Con argumentos deja velocidades fijas, para medir en bornes y calibrar:
 *   test_motores <izq> <der> <segundos>      (velocidades en [-100, 100])
 *
 * Correr con las ruedas en el aire.
 *
 * Compilar enlazando contra libroombateca, p.ej.:
 *   $CC test/test_motores.c -Iinclude -L<dir-con-libroombateca.so> \
 *       -lroombateca -o test_motores */

#include "motores.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t detener = 0;

static void al_recibir_senal(int sig) {
    (void)sig;
    detener = 1;
}

/* Espera en pasos cortos para cortar rápido con Ctrl+C. */
static int esperar(double segundos) {
    for (int i = 0; i < (int)(segundos * 10) && !detener; i++) {
        usleep(100000);
    }
    return detener ? -1 : 0;
}

static int aplicar(int izq, int der, const char *descripcion) {
    printf("%-34s izq=%4d der=%4d", descripcion, izq, der);
    int rv = motor_izquierdo_set(izq);
    if (rv == 0) {
        rv = motor_derecho_set(der);
    }
    if (rv < 0) {
        printf("  ERROR: %s\n", strerror(-rv));
        return rv;
    }
    printf("  -> get izq=%4d der=%4d\n", motor_izquierdo_get(), motor_derecho_get());
    return 0;
}

static int verificar_rechazo(void) {
    int rv = motor_izquierdo_set(MOTOR_VELOCIDAD_MAX + 1);
    printf("%-34s %s\n", "Velocidad fuera de rango (101)",
           rv == -EINVAL ? "rechazada con EINVAL (ok)" : "NO se rechazó (falla)");
    return rv == -EINVAL ? 0 : -1;
}

static int secuencia(void) {
    const struct {
        int izq, der;
        double segundos;
        const char *descripcion;
    } pasos[] = {
        {  30,   0, 2.0, "Izquierdo adelante, lento" },
        { 100,   0, 2.0, "Izquierdo adelante, máximo" },
        { -60,   0, 2.0, "Izquierdo atrás (inversión)" },
        {   0,   0, 1.0, "Rueda libre" },
        {   0,  30, 2.0, "Derecho adelante, lento" },
        {   0, 100, 2.0, "Derecho adelante, máximo" },
        {   0, -60, 2.0, "Derecho atrás (inversión)" },
        {   0,   0, 1.0, "Rueda libre" },
        {  60,  60, 2.0, "Ambos adelante" },
        { -60, -60, 2.0, "Ambos atrás" },
        { -50,  50, 2.0, "Giro en el lugar a la izquierda" },
        {  80,  40, 2.0, "Arco a la derecha" },
    };

    for (size_t i = 0; i < sizeof(pasos) / sizeof(pasos[0]); i++) {
        if (aplicar(pasos[i].izq, pasos[i].der, pasos[i].descripcion) < 0 ||
            esperar(pasos[i].segundos) < 0) {
            return -1;
        }
    }

    if (aplicar(80, 80, "Ambos adelante antes de frenar") < 0 || esperar(1.5) < 0) {
        return -1;
    }
    int rv = motores_frenar();
    printf("%-34s %s  -> get izq=%4d der=%4d\n", "Freno dinámico",
           rv == 0 ? "ok" : strerror(-rv), motor_izquierdo_get(), motor_derecho_get());
    if (rv < 0 || esperar(1.5) < 0) {
        return -1;
    }

    if (aplicar(40, 40, "Salir del freno con *_set") < 0 || esperar(1.5) < 0) {
        return -1;
    }
    aplicar(0, 0, "Rueda libre");
    return verificar_rechazo();
}

static int leer_entero(const char *texto, int *valor) {
    char *fin;
    errno = 0;
    long v = strtol(texto, &fin, 10);
    if (errno != 0 || *texto == '\0' || *fin != '\0') {
        return -1;
    }
    *valor = (int)v;
    return 0;
}

int main(int argc, char *argv[]) {
    int izq = 0, der = 0, segundos = 0;
    int manual = argc == 4;

    if (argc != 1 && !manual) {
        fprintf(stderr, "Uso: %s [<izq> <der> <segundos>]\n", argv[0]);
        return 2;
    }
    if (manual && (leer_entero(argv[1], &izq) < 0 || leer_entero(argv[2], &der) < 0 ||
                   leer_entero(argv[3], &segundos) < 0 || segundos <= 0)) {
        fprintf(stderr, "Argumentos inválidos\n");
        return 2;
    }

    struct sigaction sa = { .sa_handler = al_recibir_senal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Levantá el robot: las ruedas van a girar. Enter para seguir, Ctrl+C para salir.\n");
    if (getchar() == EOF || detener) {
        return 0;
    }

    int rv = motor_control_init();
    if (rv < 0) {
        fprintf(stderr, "motor_control_init: %s\n", strerror(-rv));
        return 1;
    }

    int resultado;
    if (manual) {
        resultado = aplicar(izq, der, "Velocidad fija");
        if (resultado == 0) {
            esperar(segundos);
        }
    } else {
        resultado = secuencia();
    }

    motor_control_cleanup();
    if (detener) {
        printf("\nInterrumpido, motores detenidos.\n");
    }
    return resultado == 0 ? 0 : 1;
}
