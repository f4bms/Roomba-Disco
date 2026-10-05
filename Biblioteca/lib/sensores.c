#include "sensores.h"

#include "gpio_control.h"
#include "pinout.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define SENSOR_PULSO_TRIG_US 10

/* El HC-SR04 levanta ECHO unos 0,5 ms después del disparo. */
#define SENSOR_TIMEOUT_INICIO_NS (10 * 1000000LL)

/* Ancho de ECHO a SENSOR_DISTANCIA_MAX_CM (~23 ms), con margen. */
#define SENSOR_TIMEOUT_ECO_NS (30 * 1000000LL)

/* Sin eco, el HC-SR04 deja ECHO en alto ~38 ms (algunos clones, bastante
 * más); hay que esperar a que baje antes del siguiente disparo. */
#define SENSOR_TIMEOUT_ECO_COLGADO_NS (200 * 1000000LL)

/* 343 m/s a ~20 °C, en cm/ns. */
#define SENSOR_VELOCIDAD_SONIDO_CM_NS 3.43e-5

typedef struct {
    int pin_trig;
    int pin_echo;
} sensor_t;

static const sensor_t sensores[SENSOR_CANTIDAD] = {
    [SENSOR_FRONTAL] = { PIN_US_FRONTAL_TRIG, PIN_US_FRONTAL_ECHO },
    [SENSOR_TRASERO] = { PIN_US_TRASERO_TRIG, PIN_US_TRASERO_ECHO },
};

/* Una sola medición a la vez entre todos los sensores. */
static pthread_mutex_t sensores_mutex = PTHREAD_MUTEX_INITIALIZER;
static int inicializado = 0;
static uint64_t ultimo_disparo_ns = 0;

static uint64_t ahora_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* Espera hasta un instante absoluto: si una señal interrumpe el sueño,
 * reintentar con el mismo instante no acumula error (con un sleep
 * relativo habría que recalcular cuánto falta). */
static void dormir_hasta_ns(uint64_t instante) {
    struct timespec t = {
        .tv_sec = (time_t)(instante / 1000000000u),
        .tv_nsec = (long)(instante % 1000000000u),
    };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR) {
    }
}

/* Espera un flanco del tipo pedido, descartando los del otro tipo.
 * Devuelve 1 con su timestamp, 0 si venció el timeout, o -errno. */
static int esperar_flanco(int pin, gpio_edge_t tipo, int64_t timeout_ns,
                          uint64_t *timestamp_ns) {
    uint64_t limite = ahora_ns() + (uint64_t)timeout_ns;
    for (;;) {
        uint64_t t = ahora_ns();
        if (t >= limite) {
            return 0;
        }
        gpio_edge_event_t e;
        int rv = waitEdge(pin, (int64_t)(limite - t), &e);
        if (rv <= 0) {
            return rv;
        }
        if (e.tipo == tipo) {
            *timestamp_ns = e.timestamp_ns;
            return 1;
        }
    }
}

static void liberar_pines(void) {
    for (int i = 0; i < SENSOR_CANTIDAD; i++) {
        pinRelease(sensores[i].pin_trig);
        pinRelease(sensores[i].pin_echo);
    }
}

int sensores_init(void) {
    pthread_mutex_lock(&sensores_mutex);
    if (inicializado) {
        pthread_mutex_unlock(&sensores_mutex);
        return 0;
    }

    int rv = 0;
    for (int i = 0; i < SENSOR_CANTIDAD && rv == 0; i++) {
        rv = pinModeEx(sensores[i].pin_trig, GPIO_OUTPUT, 0, 0);
        if (rv == 0) {
            rv = pinModeEx(sensores[i].pin_echo, GPIO_INPUT, GPIO_FLAG_EDGE_BOTH, 0);
        }
    }

    if (rv < 0) {
        liberar_pines();
    } else {
        inicializado = 1;
        ultimo_disparo_ns = 0;
    }
    pthread_mutex_unlock(&sensores_mutex);
    return rv;
}

void sensores_cleanup(void) {
    pthread_mutex_lock(&sensores_mutex);
    if (inicializado) {
        for (int i = 0; i < SENSOR_CANTIDAD; i++) {
            digitalWrite(sensores[i].pin_trig, 0);
        }
        liberar_pines();
        inicializado = 0;
    }
    pthread_mutex_unlock(&sensores_mutex);
}

/* Una medición del HC-SR04:
 *  1. respetar SENSOR_INTERVALO_MIN_MS desde el último disparo (de
 *     cualquier sensor), para no oír el eco rezagado de otro;
 *  2. si ECHO sigue en alto por una medición previa sin eco, esperar a
 *     que baje;
 *  3. pulso de 10 µs en TRIG;
 *  4. ECHO sube al emitir la ráfaga y baja al volver el eco: el ancho del
 *     pulso es el tiempo de ida y vuelta del sonido. */
static int medir(const sensor_t *s, float *distancia_cm) {
    uint64_t proximo = ultimo_disparo_ns + (uint64_t)SENSOR_INTERVALO_MIN_MS * 1000000u;
    if (ultimo_disparo_ns != 0 && ahora_ns() < proximo) {
        dormir_hasta_ns(proximo);
    }

    uint64_t subida, bajada;
    int rv;

    /* Eco sin respuesta de la medición anterior todavía en curso. */
    if (digitalRead(s->pin_echo) == 1) {
        rv = esperar_flanco(s->pin_echo, GPIO_EDGE_FALLING,
                            SENSOR_TIMEOUT_ECO_COLGADO_NS, &bajada);
        if (rv <= 0) {
            return rv < 0 ? rv : -EBUSY;
        }
    }

    /* Descarta flancos viejos para no medir contra ellos: el kernel los
     * encola aunque nadie esté midiendo (p. ej. el pulso de la medición
     * anterior o ruido en la línea). */
    while ((rv = waitEdge(s->pin_echo, 0, NULL)) == 1) {
    }
    if (rv < 0) {
        return rv;
    }

    struct timespec pulso = { .tv_sec = 0, .tv_nsec = SENSOR_PULSO_TRIG_US * 1000L };
    if (digitalWrite(s->pin_trig, 1) != 0) {
        return -EIO;
    }
    nanosleep(&pulso, NULL);
    if (digitalWrite(s->pin_trig, 0) != 0) {
        return -EIO;
    }
    ultimo_disparo_ns = ahora_ns();

    /* Los timestamps los pone el kernel al llegar el flanco, así que la
     * latencia de este hilo no afecta la medida. */
    rv = esperar_flanco(s->pin_echo, GPIO_EDGE_RISING, SENSOR_TIMEOUT_INICIO_NS, &subida);
    if (rv <= 0) {
        return rv < 0 ? rv : -EIO;
    }
    rv = esperar_flanco(s->pin_echo, GPIO_EDGE_FALLING, SENSOR_TIMEOUT_ECO_NS, &bajada);
    if (rv <= 0) {
        return rv < 0 ? rv : -ETIMEDOUT;
    }

    /* El sonido va y vuelve: distancia = tiempo · velocidad / 2. Más allá
     * del rango del sensor la lectura no es confiable y se trata como "sin
     * obstáculo"; por debajo del mínimo se satura en el mínimo, porque el
     * obstáculo existe aunque la medida no sea precisa. */
    double d = (double)(bajada - subida) * SENSOR_VELOCIDAD_SONIDO_CM_NS / 2.0;
    if (d > SENSOR_DISTANCIA_MAX_CM) {
        return -ETIMEDOUT;
    }
    *distancia_cm = d < SENSOR_DISTANCIA_MIN_CM ? SENSOR_DISTANCIA_MIN_CM : (float)d;
    return 0;
}

int sensor_medir(sensor_id_t id, float *distancia_cm) {
    if ((int)id < 0 || id >= SENSOR_CANTIDAD || !distancia_cm) {
        return -EINVAL;
    }
    pthread_mutex_lock(&sensores_mutex);
    int rv = inicializado ? medir(&sensores[id], distancia_cm) : -ENODEV;
    pthread_mutex_unlock(&sensores_mutex);
    return rv;
}
