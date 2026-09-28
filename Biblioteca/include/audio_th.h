#ifndef AUDIO_TH_H
#define AUDIO_TH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_STOP = 0,
    AUDIO_PLAY,
    AUDIO_PAUSA
} audio_estado_t;

/* Rutas absolutas hacia los .mp3 instalados por libroombateca_1.0.bb en
 * ${datadir}/roomba-disco/audio/ (ver FILES:${PN} en esa receta), es
 * decir /usr/share/roomba-disco/audio/ en el target.
 *
 * Se usan rutas absolutas y no relativas porque ni logica.service ni
 * servidor.service definen WorkingDirectory: una ruta como
 * "audio/alerta.mp3" se buscaria relativa al cwd que systemd les
 * asigne por defecto, no a esta carpeta.
 */
#define AUDIO_DIR "/usr/share/roomba-disco/audio/"

/* Sonidos de notificacion obligatorios (seccion "Retroalimentacion
 * sonora" del enunciado): inicio del sistema, inicio del modo autonomo,
 * obstaculo detectado, cambio a modo manual. */
#define AUDIO_INICIO_SYS    AUDIO_DIR "arranque.mp3"
#define AUDIO_AUTO_MODE     AUDIO_DIR "pirin.mp3"
#define AUDIO_ALERTA        AUDIO_DIR "alerta.mp3"
#define AUDIO_MANUAL_MODE   AUDIO_DIR "ding.mp3"

/* Sobra en el set actual de 6 archivos; libre para "fin de ciclo"
 * (requerimiento opcional) o cualquier evento que agreguen despues. */
#define AUDIO_LIBRE         AUDIO_DIR "blub.mp3"

/* Musica de fondo (playlist de una sola pista por ahora). */
#define AUDIO_MUSICA        AUDIO_DIR "MrTaxiCut.mp3"

/* Inicializa el subsistema de audio. Devuelve 0 en éxito, <0 en error. */
int audio_control_init(void);

/* Detiene la reproducción y libera los recursos de audio. */
void audio_control_cleanup(void);

/* Reproduce el MP3 en path en un hilo aparte (no bloquea al llamador),
 * mezclado sobre la música si la hay. Para sonidos cortos. */
void trigger_notification_audio(const char *path);

/* Canal de música: una pista a la vez; audio_play reemplaza la actual.
 * Devuelven 0 o -errno. */
int audio_play(const char *path);
int audio_pause(void);
int audio_resume(void);
int audio_stop(void);

/* AUDIO_STOP también cuando la pista terminó sola. */
audio_estado_t audio_get_state(void);

/* volumen en [0, 100]. */
int audio_set_volume(int volumen);
int audio_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif
