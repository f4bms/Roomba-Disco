#include "encoders.h"

#include "gpio_control.h"
#include "motores.h"
#include "pinout.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

/* Por definir: contar las ranuras del disco y medir el diámetro real de la
 * rueda; con esos dos valores queda calibrada la distancia. */
#define ENCODER_RANURAS        20
#define ENCODER_DIAMETRO_MM    65.0

/* Se cuentan ambos flancos: dos por ranura. */
#define ENCODER_MM_POR_PULSO   (3.14159265358979 * ENCODER_DIAMETRO_MM / (2.0 * ENCODER_RANURAS))

/* Flancos más cercanos que esto al anterior aceptado son rebote del
 * comparador. A ~200 rpm hay un flanco cada ~7 ms. */
#define ENCODER_FILTRO_NS      (1000000ull)

/* Sin flancos durante este tiempo, la rueda se considera quieta. */
#define ENCODER_QUIETO_NS      (500000000ull)

typedef struct {
    int pin;
    int (*velocidad_motor)(void);
    int signo;                  /* último sentido comandado, ±1 */
    int64_t pulsos;
    gpio_edge_t ultimo_tipo;
    uint64_t t_ultimo_ns;       /* último flanco aceptado */
    uint64_t t_previo_ns[2];    /* los dos anteriores a ese */
} encoder_t;

static encoder_t encoders[ENCODER_CANTIDAD] = {
    [ENCODER_IZQUIERDO] = { .pin = PIN_ENCODER_IZQ, .velocidad_motor = motor_izquierdo_get },
    [ENCODER_DERECHO]   = { .pin = PIN_ENCODER_DER, .velocidad_motor = motor_derecho_get },
};

static pthread_mutex_t encoders_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t hilo;
static int fd_detener = -1;
static int inicializado = 0;

static uint64_t ahora_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* Con encoders_mutex tomado. El canal es único, así que el sentido sale
 * del motor; con velocidad 0 la rueda sigue por inercia en el último. */
static void registrar_flanco(encoder_t *e, const gpio_edge_event_t *ev) {
    /* Se descarta el flanco si:
     *  - es del mismo tipo que el anterior aceptado
     *  - llega antes de ENCODER_FILTRO_NS: rebote del LM393
     * El primer flanco tras el init (t_ultimo_ns == 0) siempre se acepta. */
    if (e->t_ultimo_ns != 0 &&
        (ev->tipo == e->ultimo_tipo ||
         ev->timestamp_ns - e->t_ultimo_ns < ENCODER_FILTRO_NS)) {
        return;
    }

    int v = e->velocidad_motor();
    if (v != 0) {
        e->signo = v > 0 ? 1 : -1;
    }

    e->pulsos += e->signo;
    e->ultimo_tipo = ev->tipo;
    /* Se guardan los tres últimos instantes para medir la velocidad sobre
     * dos flancos (ver velocidad_mm_s). */
    e->t_previo_ns[1] = e->t_previo_ns[0];
    e->t_previo_ns[0] = e->t_ultimo_ns;
    e->t_ultimo_ns = ev->timestamp_ns;
}

/* Hilo de conteo: duerme en poll() sobre los fd de eventos de ambos
 * encoders más un eventfd. El hilo no gasta CPU entre pulsos y
 * encoders_cleanup() lo despierta escribiendo en el eventfd para que
 * termine sin tener que cancelarlo. */
static void *contar(void *arg) {
    (void)arg;
    struct pollfd pfd[ENCODER_CANTIDAD + 1];
    for (int i = 0; i < ENCODER_CANTIDAD; i++) {
        pfd[i].fd = edgeFd(encoders[i].pin);
        pfd[i].events = POLLIN;
    }
    pfd[ENCODER_CANTIDAD].fd = fd_detener;
    pfd[ENCODER_CANTIDAD].events = POLLIN;

    for (;;) {
        if (poll(pfd, ENCODER_CANTIDAD + 1, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pfd[ENCODER_CANTIDAD].revents) {
            break;
        }
        for (int i = 0; i < ENCODER_CANTIDAD; i++) {
            if (!pfd[i].revents) {
                continue;
            }
            /* Puede haber varios flancos encolados: se vacían todos sin
             * bloquear (timeout 0) antes de volver a poll(). */
            gpio_edge_event_t ev;
            while (waitEdge(encoders[i].pin, 0, &ev) == 1) {
                pthread_mutex_lock(&encoders_mutex);
                registrar_flanco(&encoders[i], &ev);
                pthread_mutex_unlock(&encoders_mutex);
            }
        }
    }
    return NULL;
}

static void liberar_pines(void) {
    for (int i = 0; i < ENCODER_CANTIDAD; i++) {
        pinRelease(encoders[i].pin);
    }
}

int encoders_init(void) {
    pthread_mutex_lock(&encoders_mutex);
    if (inicializado) {
        pthread_mutex_unlock(&encoders_mutex);
        return 0;
    }

    int rv = 0;
    for (int i = 0; i < ENCODER_CANTIDAD && rv == 0; i++) {
        encoder_t *e = &encoders[i];
        e->signo = 1;
        e->pulsos = 0;
        e->t_ultimo_ns = e->t_previo_ns[0] = e->t_previo_ns[1] = 0;
        rv = pinModeEx(e->pin, GPIO_INPUT, GPIO_FLAG_EDGE_BOTH, 0);
    }

    if (rv == 0) {
        fd_detener = eventfd(0, EFD_CLOEXEC);
        rv = fd_detener < 0 ? -errno : 0;
    }
    if (rv == 0) {
        rv = -pthread_create(&hilo, NULL, contar, NULL);
        if (rv < 0) {
            close(fd_detener);
            fd_detener = -1;
        }
    }

    if (rv < 0) {
        liberar_pines();
    } else {
        inicializado = 1;
    }
    pthread_mutex_unlock(&encoders_mutex);
    return rv;
}

void encoders_cleanup(void) {
    pthread_mutex_lock(&encoders_mutex);
    int estaba = inicializado;
    inicializado = 0;
    pthread_mutex_unlock(&encoders_mutex);
    if (!estaba) {
        return;
    }

    /* Despierta el poll() del hilo; ve el eventfd listo y sale del bucle. */
    uint64_t uno = 1;
    (void)!write(fd_detener, &uno, sizeof uno);
    pthread_join(hilo, NULL);
    close(fd_detener);
    fd_detener = -1;
    liberar_pines();
}

/* Velocidad sobre la última ranura completa (dos flancos), porque las
 * ranuras y los dientes del disco no miden lo mismo. */
static double velocidad_mm_s(const encoder_t *e, uint64_t t) {
    if (e->t_ultimo_ns == 0 || e->t_previo_ns[1] == 0) {
        return 0.0;
    }
    uint64_t desde_ultimo = t > e->t_ultimo_ns ? t - e->t_ultimo_ns : 0;
    if (desde_ultimo > ENCODER_QUIETO_NS) {
        return 0.0;
    }
    /* periodo = tiempo entre el último flanco y el de dos antes, o sea una
     * ranura más un diente completos (= 2 pulsos). */
    uint64_t periodo = e->t_ultimo_ns - e->t_previo_ns[1];
    /* Si la rueda frena, el tiempo desde el último flanco acota la velocidad:
     * si ya pasó desde_ultimo sin flanco nuevo, cada flanco tarda al menos
     * eso y dos flancos al menos 2·desde_ultimo. Sin esta cota, una rueda
     * que se detiene seguiría reportando su última velocidad hasta
     * ENCODER_QUIETO_NS. */
    if (2 * desde_ultimo > periodo) {
        periodo = 2 * desde_ultimo;
    }
    return e->signo * 2.0 * ENCODER_MM_POR_PULSO * 1e9 / (double)periodo;
}

/* Con encoders_mutex tomado. */
static void copiar_lectura(const encoder_t *e, uint64_t t, encoder_lectura_t *lectura) {
    lectura->pulsos = e->pulsos;
    lectura->distancia_mm = (double)e->pulsos * ENCODER_MM_POR_PULSO;
    lectura->velocidad_mm_s = velocidad_mm_s(e, t);
    lectura->ultimo_pulso_ns = e->t_ultimo_ns;
}

int encoder_leer(encoder_id_t id, encoder_lectura_t *lectura) {
    if ((int)id < 0 || id >= ENCODER_CANTIDAD || !lectura) {
        return -EINVAL;
    }
    pthread_mutex_lock(&encoders_mutex);
    int rv = -ENODEV;
    if (inicializado) {
        copiar_lectura(&encoders[id], ahora_ns(), lectura);
        rv = 0;
    }
    pthread_mutex_unlock(&encoders_mutex);
    return rv;
}

int encoders_leer_todos(encoder_lectura_t lecturas[ENCODER_CANTIDAD]) {
    if (!lecturas) {
        return -EINVAL;
    }
    pthread_mutex_lock(&encoders_mutex);
    int rv = -ENODEV;
    if (inicializado) {
        uint64_t t = ahora_ns();
        for (int i = 0; i < ENCODER_CANTIDAD; i++) {
            copiar_lectura(&encoders[i], t, &lecturas[i]);
        }
        rv = 0;
    }
    pthread_mutex_unlock(&encoders_mutex);
    return rv;
}

int encoders_reset(void) {
    pthread_mutex_lock(&encoders_mutex);
    int rv = -ENODEV;
    if (inicializado) {
        for (int i = 0; i < ENCODER_CANTIDAD; i++) {
            encoders[i].pulsos = 0;
        }
        rv = 0;
    }
    pthread_mutex_unlock(&encoders_mutex);
    return rv;
}
