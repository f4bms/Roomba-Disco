#include "succion.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#ifdef ROOMBATECA_HARDWARE
#include "gpio_control.h"
#include "pinout.h"

#include <sched.h>
#include <stdatomic.h>
#include <time.h>

#define SUCCION_PERIODO_NS 1000000L   /* 1 kHz, límite por el PC817 */

/* Motor de 7,4 V con pack de hasta 12,6 V (≈ 59 %); el margen compensa el
 * apagado lento de la etapa, que alarga cada pulso. */
#define SUCCION_DUTY_MAX_PCT 55L
#define SUCCION_RAMPA_MS 1000L

#define DUTY_MAX_NS (SUCCION_PERIODO_NS * SUCCION_DUTY_MAX_PCT / 100)
#define PASO_RAMPA_NS (DUTY_MAX_NS / SUCCION_RAMPA_MS)

static pthread_mutex_t succion_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t hilo;
static int inicializado = 0;
static int potencia_actual = 0;
static atomic_long duty_objetivo_ns = 0;
static atomic_int seguir = 0;

static void sumar_ns(struct timespec *t, long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec++;
    }
}

/* Esperas a hora absoluta para que un ciclo atrasado no corra a los
 * siguientes. El duty sube con rampa y baja de inmediato. */
static void *hilo_pwm(void *arg) {
    (void)arg;
    struct sched_param sp = { .sched_priority = 50 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);   /* sin root, sigue sin RT */

    long duty_ns = 0;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);

    while (atomic_load(&seguir)) {
        long objetivo = atomic_load(&duty_objetivo_ns);
        if (duty_ns < objetivo) {
            duty_ns = duty_ns + PASO_RAMPA_NS < objetivo ? duty_ns + PASO_RAMPA_NS : objetivo;
        } else {
            duty_ns = objetivo;
        }

        struct timespec fin_alto = t;
        sumar_ns(&fin_alto, duty_ns);
        sumar_ns(&t, SUCCION_PERIODO_NS);
        if (duty_ns > 0) {
            digitalWrite(PIN_SUCCION, 1);
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &fin_alto, NULL);
        }
        digitalWrite(PIN_SUCCION, 0);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
    }

    digitalWrite(PIN_SUCCION, 0);
    return NULL;
}

int succion_init(void) {
    pthread_mutex_lock(&succion_mutex);
    if (inicializado) {
        pthread_mutex_unlock(&succion_mutex);
        return 0;
    }

    int rv = pinModeEx(PIN_SUCCION, GPIO_OUTPUT, 0, 0);
    if (rv == 0) {
        rv = digitalWrite(PIN_SUCCION, 0) == 0 ? 0 : -EIO;
    }
    if (rv == 0) {
        atomic_store(&duty_objetivo_ns, 0);
        atomic_store(&seguir, 1);
        rv = -pthread_create(&hilo, NULL, hilo_pwm, NULL);
    }
    if (rv == 0) {
        potencia_actual = 0;
        inicializado = 1;
    } else {
        atomic_store(&seguir, 0);
        pinRelease(PIN_SUCCION);
    }

    pthread_mutex_unlock(&succion_mutex);
    return rv;
}

/* El kernel deja la línea con su último valor al liberarla. */
void succion_cleanup(void) {
    pthread_mutex_lock(&succion_mutex);
    if (inicializado) {
        atomic_store(&seguir, 0);
        pthread_join(hilo, NULL);
        digitalWrite(PIN_SUCCION, 0);
        pinRelease(PIN_SUCCION);
        potencia_actual = 0;
        inicializado = 0;
    }
    pthread_mutex_unlock(&succion_mutex);
}

int succion_set(int potencia) {
    if (potencia < 0 || potencia > SUCCION_POTENCIA_MAX) {
        return -EINVAL;
    }
    pthread_mutex_lock(&succion_mutex);
    int rv = -ENODEV;
    if (inicializado) {
        atomic_store(&duty_objetivo_ns, DUTY_MAX_NS * potencia / SUCCION_POTENCIA_MAX);
        potencia_actual = potencia;
        rv = 0;
    }
    pthread_mutex_unlock(&succion_mutex);
    return rv;
}

int succion_get(void) {
    pthread_mutex_lock(&succion_mutex);
    int potencia = inicializado ? potencia_actual : 0;
    pthread_mutex_unlock(&succion_mutex);
    return potencia;
}

#else

static pthread_mutex_t succion_mutex = PTHREAD_MUTEX_INITIALIZER;
static int inicializado = 0;
static int potencia_actual = 0;

int succion_init(void) {
    pthread_mutex_lock(&succion_mutex);
    potencia_actual = 0;
    inicializado = 1;
    pthread_mutex_unlock(&succion_mutex);
    printf("[succion] init\n");
    return 0;
}

void succion_cleanup(void) {
    pthread_mutex_lock(&succion_mutex);
    potencia_actual = 0;
    inicializado = 0;
    pthread_mutex_unlock(&succion_mutex);
    printf("[succion] cleanup\n");
}

int succion_set(int potencia) {
    if (potencia < 0 || potencia > SUCCION_POTENCIA_MAX) {
        return -EINVAL;
    }
    pthread_mutex_lock(&succion_mutex);
    int rv = -ENODEV;
    if (inicializado) {
        if (potencia != potencia_actual) {
            printf("[succion] potencia -> %d\n", potencia);
        }
        potencia_actual = potencia;
        rv = 0;
    }
    pthread_mutex_unlock(&succion_mutex);
    return rv;
}

int succion_get(void) {
    pthread_mutex_lock(&succion_mutex);
    int potencia = inicializado ? potencia_actual : 0;
    pthread_mutex_unlock(&succion_mutex);
    return potencia;
}

#endif
