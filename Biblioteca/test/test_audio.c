/* Prueba manual de audio_th ~~~ canal de música con notificaciones triguereadas ~~~ 
 *
 * -----Para probar en PC tener instalado mpg123.------
 *Compilamos desde ~/Biblioteca:
 *>>>gcc -std=c11 -Wall -Wextra -Iinclude -pthread lib/audio_th.c test/test_audio.c -o test_audio
 *Corremos igual desde ~/Biblioteca:
 *>>>./test_audio audio/MrTaxiCut.mp3 audio/alerta.mp3 audio/ding.mp3 audio/pirin.mp3 audio/arranque.mp3  
 *
 * Compilar para la rasp:
 * Primero si no hemos corrido el .sh para compilar con la imagen correcta de pocky, lo hacemos:
 *>>>sudo ./poky-glibc....sh
 *>>>pass
 *>>>Y
 *Ahora si, entramos al entorno sdk con:
 *>>>source /opt/poky/...
 *Compilamos en el entorno:
 *>>>$CC -std=c11 -Wall -Wextra -Iinclude -pthread lib/audio_th.c test/test_audio.c -o test_audio
 *
 *Pasamos a la rasp y probamos 
 */

#include "audio_th.h"

#include <stdio.h>
#include <unistd.h>

static const char *nombre_estado(audio_estado_t estado) {
    switch (estado) {
        case AUDIO_STOP: return "STOP";
        case AUDIO_PLAY: return "PLAY";
        case AUDIO_PAUSA: return "PAUSA";
        default: return "?";
    }
}

int main(int argc, char **argv) {
    /* Por defecto deberia usar las rutas reales para correr ya en la Rasp con los .mp3
     * dentro en libroombateca.
     */
    const char *cancion = argc > 1 ? argv[1] : AUDIO_MUSICA;
    const char *alerta = argc > 2 ? argv[2] : AUDIO_ALERTA;
    const char *auto_mode = argc > 3 ? argv[3] : AUDIO_AUTO_MODE;
    const char *manual_mode = argc > 4 ? argv[4] : AUDIO_MANUAL_MODE;
    const char *inicio_sys = argc > 5 ? argv[5] : AUDIO_INICIO_SYS;

    if (audio_control_init() != 0) {
        fprintf(stderr, "No se pudo iniciar la musica (mpg123 -R)\n");
        return 1;
    }

    printf("Estado inicial: %s\n", nombre_estado(audio_get_state()));

    printf("\n--- audio_play(%s) ---\n", cancion);
    if (audio_play(cancion) != 0) {
        fprintf(stderr, "audio_play fallo\n");
    }
    sleep(2);
    printf("Estado con play: %s\n", nombre_estado(audio_get_state()));

    printf("\n--- notificacion triguereada: trigger_notification_audio(%s) ---\n", alerta);
    trigger_notification_audio(alerta);
    sleep(2); /* deberia sonar mezclada sobre la cancion, si dmix esta activo */

    printf("\n--- audio_set_volume(30) ---\n");
    audio_set_volume(30);
    printf("Volumen actual: %d\n", audio_get_volume());
    sleep(2);
    
    printf("\n--- notificacion triguereada: trigger_notification_audio(%s) ---\n", auto_mode);
    trigger_notification_audio(auto_mode);
    sleep(3);

    printf("\n--- audio_pause() ---\n");
    audio_pause();
    sleep(1);
    printf("Estado con pause: %s (esperado: PAUSA)\n", nombre_estado(audio_get_state()));
    sleep(3);
    
    printf("\n--- audio_resume() ---\n");
    audio_resume();
    sleep(1);
    printf("Estado con resume: %s (esperado: PLAY)\n", nombre_estado(audio_get_state()));
    sleep(6);

    printf("\n--- audio_stop() ---\n");
    audio_stop();
    sleep(1);
    printf("Estado con stop: %s (esperado: STOP)\n", nombre_estado(audio_get_state()));
    
    printf("\n--- audio_resume() Prueba ---\n");
    audio_resume();
    sleep(1);
    printf("Estado con resume: %s (esperado: STOP)\n", nombre_estado(audio_get_state()));
    sleep(2);
    
    printf("\n--- audio_play() Prueba ---\n");
    audio_play(cancion);
    sleep(1);
    printf("Estado con play(cancion): %s (esperado: PLAY)\n", nombre_estado(audio_get_state()));
    sleep(3);
    
    printf("\n--- audio_set_volume(100) ---\n");
    audio_set_volume(100);
    printf("Volumen actual: %d\n", audio_get_volume());
    sleep(3);
    
    printf("\n--- notificacion triguereada: trigger_notification_audio(%s) ---\n", manual_mode);
    trigger_notification_audio(manual_mode);
    sleep(3);
    
    printf("\n--- audio_set_volume(80) ---\n");
    audio_set_volume(80);
    printf("Volumen actual: %d\n", audio_get_volume());
    sleep(5);
    
    printf("\n--- notificacion triguereada: trigger_notification_audio(%s) ---\n", inicio_sys);
    trigger_notification_audio(inicio_sys);
    sleep(7);
     
    printf("\n--- audio_stop() ---\n");
    audio_stop();
    sleep(1);
    printf("Estado con stop: %s (esperado: STOP)\n", nombre_estado(audio_get_state()));
    sleep(2);

    audio_control_cleanup();
    printf("\nListo terminamos la prueba.\n");
    return 0;
}
