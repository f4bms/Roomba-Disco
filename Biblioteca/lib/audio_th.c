/* ~~~~~~~Control de audio ~~~~~~~.
 *
 * Hay dos canales independientes, pensados para mezclarse de audio 
 * Requiere dmix habilitado de ALSA en el config file de Yocto
 * 
 * Tiene uno que es el que tiene la m[usica usa un poceso de mpg123 continuo:
 * Tiene lo esperado de un manejo de audio: 
 *   ~audio_play(pista): inicializa una pista con load
 *   ~audio_pause()/audio_resume(): b[asicamente como funcionaria el boton de pausa
 *   ~audio_stop(): pausa la musica.
 *   ~audio_set_volume(#volumen): Para bajar o subir el volumen
 *
 * Tiene el otro que son las notificaciones en donde se tope un obstaculo o haya algun cambio
 * Estas se abren con un hilo aparte para no interrumpir.
 *
 * Se usa fork()+execlp() en vez de system() para evitar el shell y mantenerse independiente.
 */
 
//version
#define _POSIX_C_SOURCE 200809L

#include "audio_th.h"
//para los errores
#include <errno.h>
//para abrir cosas
#include <fcntl.h>
//hilos
#include <pthread.h>
//envia se;ales de kill 
#include <signal.h>
//para las operaciones at[omicas en este caso como hay varios hilos para algunas lecturas
#include <stdatomic.h>
//manejo de procesos
#include <sys/wait.h>
//para el sys de posix, lo del fork y asi
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define MPG123_BIN "/usr/bin/mpg123"
#define LINEA_MAX 256
#define NOTIFICACION_PATH_MAX 256

//~~~~~Canal de musica~~~~~~

static pthread_mutex_t musica_mutex = PTHREAD_MUTEX_INITIALIZER; //un mutex clasico
static pid_t musica_pid = -1; //proceso hijo, es -1 cuando no hay proceso
static int musica_stdin_fd = -1; //descriptor de escritura , -1 es que no est[a abierto el proceso
static pthread_t lector_hilo; //id del hilo
static int lector_iniciado = 0; //bandera para que no muera el join de los threads

static atomic_int estado_actual = AUDIO_STOP;
static atomic_int volumen_actual = 100;

//Inicializa el mpg123 con los pipes
static int iniciar_proceso_mpg123(pid_t *pid_out, int *stdin_fd_out, int *stdout_fd_out) {
    int in_pipe[2]; //lectura del mpg123 ser[ia el equivalente a su "stdin"
    int out_pipe[2]; //salida del mpg123 su "stdout"
    pid_t pid; //proceso

    if (access(MPG123_BIN, X_OK) != 0) {
        return -errno;
    }

    if (pipe(in_pipe) != 0) {
        return -errno;
    }
    if (pipe(out_pipe) != 0) {
        int err = -errno;
        close(in_pipe[0]);
        close(in_pipe[1]);
        return err;
    }
    pid = fork(); //duplica el proceso
    if (pid < 0) {
        int err = -errno;
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return err;
    }
    if (pid == 0) {
        //hijo se convierte en el mpg123 remoto
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        //en caso de que falle captura el error
        execlp(MPG123_BIN, MPG123_BIN, "-o", "alsa", "-R", (char *)NULL);
        _exit(127);
    }
    //Acomoda de nuevo los punteros 
    close(in_pipe[0]);
    close(out_pipe[1]);
    *pid_out = pid;
    *stdin_fd_out = in_pipe[1];
    *stdout_fd_out = out_pipe[0];
    return 0;
}

//esto es para enviar el comando al reproductor ya inicializado, aqui se arma
static int enviar_comando(const char *comando) {
    char buffer[NOTIFICACION_PATH_MAX + 16];
    int largo = snprintf(buffer, sizeof(buffer), "%s\n", comando);
    int rv = 0;

    if (largo < 0 || (size_t)largo >= sizeof(buffer)) {
        return -ENAMETOOLONG;
    }
    //aca usamos el mutex para garantizar la escritura por los pipes sea atomica
    pthread_mutex_lock(&musica_mutex);
    if (musica_stdin_fd < 0) {
        rv = -ENODEV;
    } else if (write(musica_stdin_fd, buffer, (size_t)largo) != largo) {
        rv = -errno; 
    }
    pthread_mutex_unlock(&musica_mutex);
    return rv;
}

typedef struct {
    int stdout_fd;
} lector_arg_t;

// Pasa las líneas "@P 0/1/2" del mpg123 al audio_estado_cosas
static void *lector_estado(void *arg_ptr) {
    lector_arg_t *arg = (lector_arg_t *)arg_ptr;
    FILE *stream = fdopen(arg->stdout_fd, "r");
    char linea[LINEA_MAX];

    free(arg);
    if (stream == NULL) {
        return NULL;
    }

    while (fgets(linea, sizeof(linea), stream) != NULL) {
        int codigo;
        if (sscanf(linea, "@P %d", &codigo) == 1) {
            switch (codigo) {
                case 0: atomic_store(&estado_actual, AUDIO_STOP); break;
                case 1: atomic_store(&estado_actual, AUDIO_PAUSA); break;
                case 2: atomic_store(&estado_actual, AUDIO_PLAY); break;
                default: break;
            }
        }
    }
    atomic_store(&estado_actual, AUDIO_STOP);
    fclose(stream); 
    return NULL;
}
//inicializa el canal del musica en stop
int audio_control_init(void) {
    pid_t pid = -1;
    int stdin_fd = -1;
    int stdout_fd = -1;
    int rv;
    lector_arg_t *arg;
    //si falla un mpg123 devuelve un error y no termina el proceso
    signal(SIGPIPE, SIG_IGN);

    pthread_mutex_lock(&musica_mutex);
    if (musica_pid > 0) {
        pthread_mutex_unlock(&musica_mutex);
        return 0; // ya estaba encendida la bandera
    }
    pthread_mutex_unlock(&musica_mutex);

    rv = iniciar_proceso_mpg123(&pid, &stdin_fd, &stdout_fd);
    if (rv != 0) {
        return rv;
    }

    arg = malloc(sizeof(*arg));
    if (arg == NULL) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        close(stdin_fd);
        close(stdout_fd);
        return -ENOMEM;
    }
    arg->stdout_fd = stdout_fd;

    pthread_mutex_lock(&musica_mutex);
    musica_pid = pid;
    musica_stdin_fd = stdin_fd;
    pthread_mutex_unlock(&musica_mutex);
    atomic_store(&estado_actual, AUDIO_STOP);

    int rc = pthread_create(&lector_hilo, NULL, lector_estado, arg); 
    if (rc != 0) { 
        //pthread_create devuelve el error, sin usar errno 
        free(arg); 
        //el hilo no lo agarra y cierra de una
        close(stdout_fd);
        audio_control_cleanup(); 
        return -rc; 
    }
    lector_iniciado = 1;
    //Maneja el volumen, default en 100
    audio_set_volume(atomic_load(&volumen_actual));
    return 0;
}
//para limpiar memoria
void audio_control_cleanup(void) {
    pid_t pid;

    pthread_mutex_lock(&musica_mutex);
    pid = musica_pid;
    musica_pid = -1;
    if (musica_stdin_fd >= 0) {
        close(musica_stdin_fd);
        musica_stdin_fd = -1;
    }
    pthread_mutex_unlock(&musica_mutex);

    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0); 
    }

    if (lector_iniciado) {
        pthread_join(lector_hilo, NULL);
        lector_iniciado = 0;
    }

    atomic_store(&estado_actual, AUDIO_STOP);
}
//inicia una cancion
int audio_play(const char *path) {
    char comando[NOTIFICACION_PATH_MAX + 8];
    if (path == NULL) {
        return -EINVAL;
    }
    if (snprintf(comando, sizeof(comando), "LOAD %s", path) >= (int)sizeof(comando)) {
        return -ENAMETOOLONG;
    }
    return enviar_comando(comando);
}
//pausa la musica
int audio_pause(void) {
    if (atomic_load(&estado_actual) != AUDIO_PLAY) {
        return 0; 
    }
    return enviar_comando("PAUSE");
}
//continua la musica
int audio_resume(void) {
    if (atomic_load(&estado_actual) != AUDIO_PAUSA) {
        return 0; 
    }
    return enviar_comando("PAUSE"); /* PAUSE alterna: pausado -> play */
}
//para la musica
int audio_stop(void) {
    return enviar_comando("STOP");
}
//revisa el estado en que esta la musica
audio_estado_t audio_get_state(void) {
    return (audio_estado_t)atomic_load(&estado_actual);
}
//para setear el volumen, va de 0 a 100
int audio_set_volume(int volumen) {
    char comando[32];
    int rv;
    if (volumen < 0 || volumen > 100) {
        return -EINVAL;
    }
    snprintf(comando, sizeof(comando), "VOLUME %d", volumen);
    rv = enviar_comando(comando);
    if (rv == 0) {
        atomic_store(&volumen_actual, volumen);
    }
    return rv;
}
//pregunta el volumen actual
int audio_get_volume(void) {
    return atomic_load(&volumen_actual);
}

//~~~~~Parte de las notificaciones~~~~~

typedef struct {
    char path[NOTIFICACION_PATH_MAX];
} notificacion_arg_t;
//genera el proceso del sonido
static void *reproducir_notificacion(void *arg_ptr) {
    notificacion_arg_t *datos = (notificacion_arg_t *)arg_ptr;
    pid_t pid = fork();

    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp(MPG123_BIN, MPG123_BIN, "-o", "alsa", "-q", datos->path, (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
    //si fork() falla, nada mas no pasa nada
    free(datos);
    return NULL;
}

void trigger_notification_audio(const char *path) {
    pthread_t hilo;
    notificacion_arg_t *datos;

    if (path == NULL) {
        return;
    }
    datos = malloc(sizeof(*datos));
    if (datos == NULL) {
        return;
    }
    if (snprintf(datos->path, sizeof(datos->path), "%s", path) >= (int)sizeof(datos->path)) {
        free(datos);
        return;
    }
    if (pthread_create(&hilo, NULL, reproducir_notificacion, datos) != 0) {
        perror("[AUDIO] Error al crear hilo de notificacion");
        free(datos);
        return;
    }
    pthread_detach(hilo);
}

int play_notification_wait(const char *path) {
    pid_t pid;

    if (path == NULL) return -EINVAL;
    if (access(MPG123_BIN, X_OK) != 0) return -errno;
    pid = fork();
    if (pid < 0) return -errno;
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp(MPG123_BIN, MPG123_BIN, "-o", "alsa", "-q", path, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, NULL, 0) < 0) return -errno;
    return 0;
}
