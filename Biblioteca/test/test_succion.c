/* Aplicación de verificación de succion.h.
 *
 * Sin argumentos corre una secuencia de potencias (30, 60, 100) y el rechazo
 * de valores fuera de rango. Con argumentos deja una potencia fija, para
 * medir en bornes del motor:
 *   test_succion <potencia 0-100> <segundos>
 *
 * Compilar enlazando contra libroombateca, p.ej.:
 *   $CC test/test_succion.c -Iinclude -L<dir-con-libroombateca.so> \
 *       -lroombateca -o test_succion */

#include "succion.h"

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

static int esperar(double segundos) {
    for (int i = 0; i < (int)(segundos * 10) && !detener; i++) {
        usleep(100000);
    }
    return detener ? -1 : 0;
}

static int aplicar(int potencia, double segundos) {
    int rv = succion_set(potencia);
    printf("potencia=%3d", potencia);
    if (rv < 0) {
        printf("  ERROR: %s\n", strerror(-rv));
        return rv;
    }
    printf("  -> get=%3d  (%.0f s)\n", succion_get(), segundos);
    fflush(stdout);
    return esperar(segundos);
}

int main(int argc, char **argv) {
    signal(SIGINT, al_recibir_senal);
    signal(SIGTERM, al_recibir_senal);

    int rv = succion_init();
    if (rv < 0) {
        fprintf(stderr, "succion_init: %s\n", strerror(-rv));
        return 1;
    }

    if (argc == 3) {
        aplicar(atoi(argv[1]), atof(argv[2]));
    } else {
        rv = succion_set(SUCCION_POTENCIA_MAX + 1);
        printf("Potencia fuera de rango (101): %s\n",
               rv == -EINVAL ? "rechazada con EINVAL (ok)" : "NO se rechazó (falla)");
        int potencias[] = { 30, 60, 100, 0 };
        for (unsigned i = 0; i < sizeof potencias / sizeof potencias[0]; i++) {
            if (aplicar(potencias[i], potencias[i] ? 4 : 1) < 0) {
                break;
            }
        }
    }

    succion_cleanup();
    printf("Succión apagada\n");
    return 0;
}
