#include <stdio.h>
#include <stdlib.h>

#include "sensores.h"

int sensor_medir(sensor_id_t id, float *distancia_cm) {
    const char *path = getenv("ROO_SIM_SENSORS_FILE");
    float distances[SENSOR_CANTIDAD];
    FILE *file;

    if (distancia_cm == NULL || id < 0 || id >= SENSOR_CANTIDAD) return -1;
    *distancia_cm = 42.0f;
    if (path == NULL) return 0;

    file = fopen(path, "r");
    if (file == NULL) return 0;
    int parsed = fscanf(file, "%f %f %f", &distances[0], &distances[1], &distances[2]);
    fclose(file);
    if (parsed != SENSOR_CANTIDAD) return 0;
    for (int index = 0; index < SENSOR_CANTIDAD; ++index) {
        if (!(distances[index] >= SENSOR_DISTANCIA_MIN_CM
                && distances[index] <= SENSOR_DISTANCIA_MAX_CM)) return 0;
    }
    *distancia_cm = distances[id];
    return 0;
}