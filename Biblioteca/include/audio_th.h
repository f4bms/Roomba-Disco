#ifndef AUDIO_TH_H
#define AUDIO_TH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_DETENIDO = 0,
    AUDIO_REPRODUCIENDO,
    AUDIO_PAUSADO
} audio_estado_t;

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

/* AUDIO_DETENIDO también cuando la pista terminó sola. */
audio_estado_t audio_get_state(void);

/* volumen en [0, 100]. */
int audio_set_volume(int volumen);
int audio_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif
