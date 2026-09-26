#include "encoders.h"

#include <stdio.h>
#include <string.h>

int encoders_init(void) {
    printf("[encoders] encoders_init\n");
    return 0;
}

void encoders_cleanup(void) {
    printf("[encoders] encoders_cleanup\n");
}

int encoder_leer(encoder_id_t id, encoder_lectura_t *lectura) {
    if (lectura == NULL || id < 0 || id >= ENCODER_CANTIDAD) {
        return -1;
    }

    memset(lectura, 0, sizeof(*lectura));
    printf("[encoders] encoder_leer: encoder=%d\n", id);
    return 0;
}

int encoders_reset(void) {
    printf("[encoders] encoders_reset\n");
    return 0;
}