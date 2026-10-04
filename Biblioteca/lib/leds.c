#include "leds.h"

#include <errno.h>
#include <stdio.h>

#ifdef ROOMBATECA_HARDWARE
#include "gpio_control.h"
#include "pinout.h"

static const int led_pins[LED_CANTIDAD] = {
    [LED_ENCENDIDO] = PIN_LED_ENCENDIDO,
    [LED_ALERTA]    = PIN_LED_ALERTA,
    [LED_MANUAL]    = PIN_LED_MANUAL,
    [LED_AUTONOMO]  = PIN_LED_AUTONOMO,
};

int leds_init(void) {
    int i;
    for (i = 0; i < LED_CANTIDAD; ++i) {
        if (pinMode(led_pins[i], GPIO_OUTPUT) != 0) return -1;
        digitalWrite(led_pins[i], 0);
    }
    return 0;
}

void leds_cleanup(void) {
    int i;
    for (i = 0; i < LED_CANTIDAD; ++i) {
        digitalWrite(led_pins[i], 0);
        pinRelease(led_pins[i]);
    }
}

int led_set(led_id_t led, bool encendido) {
    if (led < 0 || led >= LED_CANTIDAD) return -EINVAL;
    return digitalWrite(led_pins[led], encendido ? 1 : 0);
}

int led_get(led_id_t led) {
    if (led < 0 || led >= LED_CANTIDAD) return -EINVAL;
    return digitalRead(led_pins[led]);
}

#else

static bool led_estados[LED_CANTIDAD] = {false};

static const char *led_nombre(led_id_t led) {
    switch (led) {
        case LED_ENCENDIDO: return "ENCENDIDO";
        case LED_ALERTA:    return "ALERTA";
        case LED_MANUAL:    return "MANUAL";
        case LED_AUTONOMO:  return "AUTONOMO";
        default:            return "?";
    }
}

int leds_init(void) {
    int i;
    for (i = 0; i < LED_CANTIDAD; ++i) led_estados[i] = false;
    printf("[leds] init\n");
    return 0;
}

void leds_cleanup(void) {
    int i;
    for (i = 0; i < LED_CANTIDAD; ++i) led_estados[i] = false;
    printf("[leds] cleanup\n");
}

int led_set(led_id_t led, bool encendido) {
    if (led < 0 || led >= LED_CANTIDAD) return -EINVAL;
    if (led_estados[led] != encendido) {
        led_estados[led] = encendido;
        printf("[leds] %s -> %s\n", led_nombre(led), encendido ? "ON" : "OFF");
    }
    return 0;
}

int led_get(led_id_t led) {
    if (led < 0 || led >= LED_CANTIDAD) return -EINVAL;
    return led_estados[led] ? 1 : 0;
}

#endif
