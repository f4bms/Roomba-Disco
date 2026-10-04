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
 * Se usan rutas absolutas y no relativas 
 */
#ifndef AUDIO_DIR
#define AUDIO_DIR "/usr/share/roomba-disco/audio/"
#endif

// Sonidos de notificacion
#define AUDIO_INICIO_SYS    AUDIO_DIR "arranque.mp3"
#define AUDIO_AUTO_MODE     AUDIO_DIR "pirin.mp3"
#define AUDIO_ALERTA        AUDIO_DIR "alerta.mp3"
#define AUDIO_MANUAL_MODE   AUDIO_DIR "ding.mp3"
#define AUDIO_TRACK_1       AUDIO_DIR "1.mp3"
#define AUDIO_TRACK_2       AUDIO_DIR "2.mp3"
#define AUDIO_TRACK_3       AUDIO_DIR "3.mp3"

// Inicializa el sys de audio. 
int audio_control_init(void);

//Detiene sonidos y libera los recursos
void audio_control_cleanup(void);

//* Sonidos de notificación
void trigger_notification_audio(const char *path);
int play_notification_wait(const char *path);

// Canal de música: una pista a la vez 
//con audio_play(cancion) reemplaza la actual, ahorita solo hay una pista
int audio_play(const char *path);
int audio_pause(void);
int audio_resume(void);
int audio_stop(void);
audio_estado_t audio_get_state(void);

//volumen: [0, 100]
int audio_set_volume(int volumen);
int audio_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif
