#ifndef AUDIO_TH_H
#define AUDIO_TH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa el subsistema de audio. Devuelve 0 en éxito, -1 en error. */
int audio_control_init(void);

/* Detiene la reproducción y libera los recursos de audio. */
void audio_control_cleanup(void);

/* Reproduce el MP3 en path en un hilo aparte (no bloquea al llamador);
 * usado para las notificaciones sonoras de los 4 eventos. */
void trigger_notification_audio(const char *path);

/* Control de reproducción para la interfaz web. */
int audio_play(const char *path);
int audio_pause(void);
int audio_stop(void);

/* volumen en [0, 100]. */
int audio_set_volume(int volumen);

/* Por definir: forma de listar las pistas disponibles para la interfaz
 * (¿la resuelve el servidor leyendo el directorio, o esta biblioteca?). */

#ifdef __cplusplus
}
#endif

#endif
