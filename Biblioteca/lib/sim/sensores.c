#include "sensores.h"

#include <stdio.h>

int sensores_init(void) {
	printf("[sensores] sensores_init\n");
	return 0;
}

void sensores_cleanup(void) {
	printf("[sensores] sensores_cleanup\n");
}

int sensor_medir(sensor_id_t id, float *distancia_cm) {
	if (distancia_cm == NULL || id < 0 || id >= SENSOR_CANTIDAD) {
		return -1;
	}

	*distancia_cm = 42.0f;
	printf("[sensores] sensor_medir: sensor=%d distancia=%.1f cm\n", id, *distancia_cm);
	return 0;
}
