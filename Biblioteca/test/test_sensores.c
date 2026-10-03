/* Aplicación de verificación de sensores.h.
 *
 * Mide en alternancia el sensor frontal y el trasero e imprime cada lectura,
 * hasta Ctrl+C o hasta completar las rondas pedidas:
 *   test_sensores [<rondas>]
 * Al salir resume, por sensor, lecturas válidas, sin eco y errores, y el
 * rango medido, para validar contra una cinta métrica.
 *
 * Compilar enlazando contra libroombateca, p.ej.:
 *   $CC test/test_sensores.c -Iinclude -L<dir-con-libroombateca.so> \
 *       -lroombateca -o test_sensores */

#include "sensores.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t detener = 0;

static void al_recibir_senal(int sig) {
    (void)sig;
    detener = 1;
}

typedef struct {
    const char *nombre;
    int validas, sin_eco, errores;
    float min_cm, max_cm, suma_cm;
} resumen_t;

static void registrar(resumen_t *r, int rv, float d) {
    if (rv == 0) {
        if (r->validas == 0 || d < r->min_cm) {
            r->min_cm = d;
        }
        if (r->validas == 0 || d > r->max_cm) {
            r->max_cm = d;
        }
        r->suma_cm += d;
        r->validas++;
        printf("  %-8s %7.1f cm", r->nombre, d);
    } else if (rv == -ETIMEDOUT) {
        r->sin_eco++;
        printf("  %-8s  sin eco  ", r->nombre);
    } else {
        r->errores++;
        printf("  %-8s %-9.9s", r->nombre, strerror(-rv));
    }
}

static void imprimir_resumen(const resumen_t *r) {
    printf("%-8s válidas=%d sin_eco=%d errores=%d", r->nombre, r->validas,
           r->sin_eco, r->errores);
    if (r->validas > 0) {
        printf("  min=%.1f max=%.1f prom=%.1f cm", r->min_cm, r->max_cm,
               r->suma_cm / r->validas);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    long rondas = 0;
    if (argc > 2 || (argc == 2 && (rondas = strtol(argv[1], NULL, 10)) <= 0)) {
        fprintf(stderr, "Uso: %s [<rondas>]\n", argv[0]);
        return 2;
    }

    struct sigaction sa = { .sa_handler = al_recibir_senal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int rv = sensores_init();
    if (rv < 0) {
        fprintf(stderr, "sensores_init: %s\n", strerror(-rv));
        return 1;
    }

    resumen_t resumen[SENSOR_CANTIDAD] = {
        [SENSOR_FRONTAL] = { .nombre = "frontal" },
        [SENSOR_TRASERO] = { .nombre = "trasero" },
    };

    for (long i = 0; !detener && (rondas == 0 || i < rondas); i++) {
        printf("%5ld", i + 1);
        for (int s = 0; s < SENSOR_CANTIDAD && !detener; s++) {
            float d = 0;
            rv = sensor_medir((sensor_id_t)s, &d);
            registrar(&resumen[s], rv, d);
        }
        printf("\n");
        fflush(stdout);
    }

    float d;
    rv = sensor_medir(SENSOR_CANTIDAD, &d);
    printf("\n%-34s %s\n", "Sensor inválido",
           rv == -EINVAL ? "rechazado con EINVAL (ok)" : "NO se rechazó (falla)");

    sensores_cleanup();
    for (int s = 0; s < SENSOR_CANTIDAD; s++) {
        imprimir_resumen(&resumen[s]);
    }
    return 0;
}
