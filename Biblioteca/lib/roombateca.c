#include "roombateca.h"

#include <stddef.h>

static const char *CANCIONES[] = {
    "audio/alerta.mp3",
    "audio/MrTaxiCut.mp3",
};

#define CANTIDAD_CANCIONES (sizeof(CANCIONES) / sizeof(CANCIONES[0]))

void roombateca_reproducir(int cancion) {
    if (cancion < 0 || (size_t)cancion >= CANTIDAD_CANCIONES) {
        return;
    }
    trigger_notification_audio(CANCIONES[cancion]);
}
