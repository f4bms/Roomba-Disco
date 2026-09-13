/* Implementación temporal: reproduce vía mpg123 en un proceso hijo, en un
 * hilo aparte para no bloquear al llamador. Trae la lógica que ya se había
 * probado en la rama yocto. */

#include "audio_th.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static void *reproducir_audio(void *arg) {
    char *path = (char *)arg;
    char comando[512];

    snprintf(comando, sizeof(comando), "mpg123 -q %s > /dev/null 2>&1", path);
    system(comando);
    free(path);
    return NULL;
}

void trigger_notification_audio(const char *path) {
    pthread_t hilo;
    char *copia = malloc(256);

    if (copia == NULL) {
        return;
    }
    snprintf(copia, 256, "%s", path);

    if (pthread_create(&hilo, NULL, reproducir_audio, copia) != 0) {
        perror("[audio] no se pudo crear el hilo de notificación");
        free(copia);
        return;
    }
    pthread_detach(hilo);
}
