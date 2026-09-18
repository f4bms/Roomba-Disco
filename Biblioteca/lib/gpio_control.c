#include "gpio_control.h"

#include <gpiod.h>
#include <time.h>

#define GPIO_CHIP_PATH "/dev/gpiochip0"
#define GPIO_CONTROL_MAX_PIN 64

typedef struct {
    struct gpiod_line_request *request;
    int requested;
} gpio_line_state_t;

static struct gpiod_chip *chip = NULL;
static gpio_line_state_t lineas[GPIO_CONTROL_MAX_PIN];

static int gpio_control_ensure_chip(void) {
    if (chip) {
        return 0;
    }
    chip = gpiod_chip_open(GPIO_CHIP_PATH);
    return chip ? 0 : -1;
}

static void gpio_control_release_pin(int pin) {
    if (lineas[pin].requested) {
        gpiod_line_request_release(lineas[pin].request);
        lineas[pin].request = NULL;
        lineas[pin].requested = 0;
    }
}

int pinMode(int pin, gpio_mode_t mode) {
    if (pin < 0 || pin >= GPIO_CONTROL_MAX_PIN) {
        return -1;
    }
    if (gpio_control_ensure_chip() != 0) {
        return -1;
    }

    gpio_control_release_pin(pin);

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) {
        return -1;
    }
    gpiod_line_settings_set_direction(settings,
        mode == GPIO_OUTPUT ? GPIOD_LINE_DIRECTION_OUTPUT
                             : GPIOD_LINE_DIRECTION_INPUT);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    if (!line_cfg) {
        gpiod_line_settings_free(settings);
        return -1;
    }

    unsigned int offset = (unsigned int)pin;
    int rv = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    struct gpiod_line_request *request = NULL;
    if (rv == 0) {
        request = gpiod_chip_request_lines(chip, NULL, line_cfg);
    }

    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);

    if (!request) {
        return -1;
    }

    lineas[pin].request = request;
    lineas[pin].requested = 1;
    return 0;
}

int digitalWrite(int pin, int value) {
    if (pin < 0 || pin >= GPIO_CONTROL_MAX_PIN || !lineas[pin].requested) {
        return -1;
    }
    enum gpiod_line_value v = value ? GPIOD_LINE_VALUE_ACTIVE
                                     : GPIOD_LINE_VALUE_INACTIVE;
    return gpiod_line_request_set_value(lineas[pin].request, (unsigned int)pin, v);
}

int digitalRead(int pin) {
    if (pin < 0 || pin >= GPIO_CONTROL_MAX_PIN || !lineas[pin].requested) {
        return -1;
    }
    enum gpiod_line_value v =
        gpiod_line_request_get_value(lineas[pin].request, (unsigned int)pin);
    if (v == GPIOD_LINE_VALUE_ERROR) {
        return -1;
    }
    return v == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;
}

int blink(int pin, double freq, double duration) {
    if (freq <= 0 || duration <= 0) {
        return -1;
    }
    if (pin < 0 || pin >= GPIO_CONTROL_MAX_PIN) {
        return -1;
    }
    if (!lineas[pin].requested && pinMode(pin, GPIO_OUTPUT) != 0) {
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

void gpio_control_cleanup(void) {
    for (int pin = 0; pin < GPIO_CONTROL_MAX_PIN; pin++) {
        gpio_control_release_pin(pin);
    }
    if (chip) {
        gpiod_chip_close(chip);
        chip = NULL;
    }
}
