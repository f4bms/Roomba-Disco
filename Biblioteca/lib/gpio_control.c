#define _GNU_SOURCE  /* ppoll */

#include "gpio_control.h"

#include <errno.h>
#include <gpiod.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>

#define GPIO_CHIP_PATH "/dev/gpiochip0"
#define GPIO_CONTROL_MAX_PIN 64

typedef struct {
    struct gpiod_line_request *request;
    struct gpiod_edge_event_buffer *eventos;  /* solo si hay detección de flancos */
    int requested;
} gpio_line_state_t;

static pthread_mutex_t gpio_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct gpiod_chip *chip = NULL;
static gpio_line_state_t lineas[GPIO_CONTROL_MAX_PIN];

static int pin_valido(int pin) {
    return pin >= 0 && pin < GPIO_CONTROL_MAX_PIN;
}

static int gpio_control_ensure_chip(void) {
    if (chip) {
        return 0;
    }
    chip = gpiod_chip_open(GPIO_CHIP_PATH);
    return chip ? 0 : -errno;
}

static void gpio_control_release_pin(int pin) {
    if (lineas[pin].requested) {
        gpiod_line_request_release(lineas[pin].request);
        gpiod_edge_event_buffer_free(lineas[pin].eventos);
        lineas[pin].request = NULL;
        lineas[pin].eventos = NULL;
        lineas[pin].requested = 0;
    }
}

static struct gpiod_line_settings *crear_settings(gpio_mode_t mode,
                                                  unsigned int flags,
                                                  unsigned int debounce_us) {
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) {
        return NULL;
    }

    gpiod_line_settings_set_direction(settings,
        mode == GPIO_OUTPUT ? GPIOD_LINE_DIRECTION_OUTPUT
                             : GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_active_low(settings, (flags & GPIO_FLAG_ACTIVE_LOW) != 0);

    if (flags & GPIO_FLAG_PULL_UP) {
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    } else if (flags & GPIO_FLAG_PULL_DOWN) {
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_DOWN);
    }

    if (mode == GPIO_INPUT) {
        unsigned int flancos = flags & GPIO_FLAG_EDGE_BOTH;
        if (flancos == GPIO_FLAG_EDGE_BOTH) {
            gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
        } else if (flancos == GPIO_FLAG_EDGE_RISING) {
            gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);
        } else if (flancos == GPIO_FLAG_EDGE_FALLING) {
            gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
        }
        /* Timestamps de eventos en CLOCK_MONOTONIC, el mismo reloj que usan
         * los módulos (ahora_ns), para poder compararlos directamente. */
        if (flancos) {
            gpiod_line_settings_set_event_clock(settings, GPIOD_LINE_CLOCK_MONOTONIC);
        }
        if (debounce_us > 0) {
            gpiod_line_settings_set_debounce_period_us(settings, debounce_us);
        }
    } else {
        /* Salida arranca inactiva (con active-low, la línea física en alto). */
        gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
    }

    return settings;
}

int pinModeEx(int pin, gpio_mode_t mode, unsigned int flags,
              unsigned int debounce_us) {
    if (!pin_valido(pin)) {
        return -EINVAL;
    }
    if ((flags & GPIO_FLAG_PULL_UP) && (flags & GPIO_FLAG_PULL_DOWN)) {
        return -EINVAL;
    }
    if (mode == GPIO_OUTPUT && ((flags & GPIO_FLAG_EDGE_BOTH) || debounce_us > 0)) {
        return -EINVAL;
    }

    pthread_mutex_lock(&gpio_mutex);

    int rv = gpio_control_ensure_chip();
    if (rv < 0) {
        pthread_mutex_unlock(&gpio_mutex);
        return rv;
    }

    /* Reconfigurar un pin = soltar su request y pedir uno nuevo */
    gpio_control_release_pin(pin);

    struct gpiod_line_settings *settings = crear_settings(mode, flags, debounce_us);
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    struct gpiod_line_request *request = NULL;
    rv = -ENOMEM;

    if (settings && line_cfg) {
        unsigned int offset = (unsigned int)pin;
        if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) == 0) {
            request = gpiod_chip_request_lines(chip, NULL, line_cfg);
        }
        rv = request ? 0 : -errno;
    }

    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);

    /* Buffer de un evento: waitEdge los entrega de a uno y el resto queda
     * encolado en el kernel hasta la siguiente lectura. */
    if (request && (flags & GPIO_FLAG_EDGE_BOTH)) {
        lineas[pin].eventos = gpiod_edge_event_buffer_new(1);
        if (!lineas[pin].eventos) {
            gpiod_line_request_release(request);
            request = NULL;
            rv = -ENOMEM;
        }
    }

    if (request) {
        lineas[pin].request = request;
        lineas[pin].requested = 1;
    }

    pthread_mutex_unlock(&gpio_mutex);
    return rv;
}

int pinMode(int pin, gpio_mode_t mode) {
    return pinModeEx(pin, mode, 0, 0) == 0 ? 0 : -1;
}

int digitalWrite(int pin, int value) {
    if (!pin_valido(pin)) {
        return -1;
    }
    pthread_mutex_lock(&gpio_mutex);
    int rv = -1;
    if (lineas[pin].requested) {
        enum gpiod_line_value v = value ? GPIOD_LINE_VALUE_ACTIVE
                                         : GPIOD_LINE_VALUE_INACTIVE;
        rv = gpiod_line_request_set_value(lineas[pin].request, (unsigned int)pin, v);
    }
    pthread_mutex_unlock(&gpio_mutex);
    return rv;
}

int digitalRead(int pin) {
    if (!pin_valido(pin)) {
        return -1;
    }
    pthread_mutex_lock(&gpio_mutex);
    int rv = -1;
    if (lineas[pin].requested) {
        enum gpiod_line_value v =
            gpiod_line_request_get_value(lineas[pin].request, (unsigned int)pin);
        if (v != GPIOD_LINE_VALUE_ERROR) {
            rv = v == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;
        }
    }
    pthread_mutex_unlock(&gpio_mutex);
    return rv;
}

static uint64_t ahora_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* Lee un evento si hay uno pendiente, sin bloquear. Con gpio_mutex tomado.
 * Devuelve 1 si leyó, 0 si no había, o -errno. */
static int leer_evento_pendiente(int pin, gpio_edge_event_t *evento) {
    if (!lineas[pin].requested || !lineas[pin].eventos) {
        return -EINVAL;
    }
    struct gpiod_line_request *req = lineas[pin].request;
    /* Timeout 0: solo pregunta si hay un evento encolado, no bloquea. */
    int rv = gpiod_line_request_wait_edge_events(req, 0);
    if (rv <= 0) {
        return rv < 0 ? -errno : 0;
    }
    if (gpiod_line_request_read_edge_events(req, lineas[pin].eventos, 1) < 0) {
        return -errno;
    }
    struct gpiod_edge_event *e = gpiod_edge_event_buffer_get_event(lineas[pin].eventos, 0);
    if (evento) {
        evento->tipo = gpiod_edge_event_get_event_type(e) == GPIOD_EDGE_EVENT_RISING_EDGE
                           ? GPIO_EDGE_RISING : GPIO_EDGE_FALLING;
        evento->timestamp_ns = gpiod_edge_event_get_timestamp_ns(e);
    }
    return 1;
}

int waitEdge(int pin, int64_t timeout_ns, gpio_edge_event_t *evento) {
    if (!pin_valido(pin)) {
        return -EINVAL;
    }
    /* timeout_ns: 0 = no bloquear, < 0 = esperar sin límite, > 0 = esperar
     * hasta un instante absoluto. Con el límite absoluto, cada vuelta del
     * bucle (señal, evento que otro hilo consumió) espera solo lo que falta. */
    uint64_t limite = timeout_ns > 0 ? ahora_ns() + (uint64_t)timeout_ns : 0;

    for (;;) {
        /* La espera se hace sin el mutex para no frenar a los demás pines;
         * la lectura se revalida con el mutex tomado por si otro hilo
         * consumió el evento o liberó la línea mientras tanto. */
        pthread_mutex_lock(&gpio_mutex);
        int rv = leer_evento_pendiente(pin, evento);
        int fd = rv == 0 ? gpiod_line_request_get_fd(lineas[pin].request) : -1;
        pthread_mutex_unlock(&gpio_mutex);
        if (rv != 0 || timeout_ns == 0) {
            return rv;
        }

        struct timespec espera, *pespera = NULL;
        if (timeout_ns > 0) {
            uint64_t t = ahora_ns();
            if (t >= limite) {
                return 0;
            }
            uint64_t resto = limite - t;
            espera.tv_sec = (time_t)(resto / 1000000000u);
            espera.tv_nsec = (long)(resto % 1000000000u);
            pespera = &espera;
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int n = ppoll(&pfd, 1, pespera, NULL);
        if (n < 0 && errno != EINTR) {
            return -errno;
        }
    }
}

int edgeFd(int pin) {
    if (!pin_valido(pin)) {
        return -EINVAL;
    }
    pthread_mutex_lock(&gpio_mutex);
    int rv = lineas[pin].requested && lineas[pin].eventos
                 ? gpiod_line_request_get_fd(lineas[pin].request) : -EINVAL;
    pthread_mutex_unlock(&gpio_mutex);
    return rv;
}

int blink(int pin, double freq, double duration) {
    if (freq <= 0 || duration <= 0) {
        return -1;
    }
    if (!pin_valido(pin)) {
        return -1;
    }
    pthread_mutex_lock(&gpio_mutex);
    int configurado = lineas[pin].requested;
    pthread_mutex_unlock(&gpio_mutex);
    if (!configurado && pinMode(pin, GPIO_OUTPUT) != 0) {
        return -1;
    }

    double semiperiodo = 1.0 / (2.0 * freq);
    struct timespec espera = {
        .tv_sec = (time_t)semiperiodo,
        .tv_nsec = (long)((semiperiodo - (time_t)semiperiodo) * 1e9)
    };

    struct timespec inicio, ahora;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    int encendido = 0;
    do {
        encendido = !encendido;
        if (digitalWrite(pin, encendido) != 0) {
            return -1;
        }
        nanosleep(&espera, NULL);
        clock_gettime(CLOCK_MONOTONIC, &ahora);
    } while ((ahora.tv_sec - inicio.tv_sec) +
             (ahora.tv_nsec - inicio.tv_nsec) / 1e9 < duration);

    digitalWrite(pin, 0);
    return 0;
}

void pinRelease(int pin) {
    if (!pin_valido(pin)) {
        return;
    }
    pthread_mutex_lock(&gpio_mutex);
    gpio_control_release_pin(pin);
    pthread_mutex_unlock(&gpio_mutex);
}

void gpio_control_cleanup(void) {
    pthread_mutex_lock(&gpio_mutex);
    for (int pin = 0; pin < GPIO_CONTROL_MAX_PIN; pin++) {
        gpio_control_release_pin(pin);
    }
    if (chip) {
        gpiod_chip_close(chip);
        chip = NULL;
    }
    pthread_mutex_unlock(&gpio_mutex);
}
