#include "roombateca_control.h"

#include "encoders.h"
#include "motores.h"
#include "sensores.h"

#include <string.h>

static int clamp_motor_speed(int speed) {
	if (speed < -100) return -100;
	if (speed > 100) return 100;
	return speed;
}

static int scaled_speed(int speed) {
	return (speed * 100 + 500) / 1000;
}

int roombateca_control_init(void) {
	if (motor_control_init() != 0) return -1;
	if (sensores_init() != 0) {
		motor_control_cleanup();
		return -1;
	}
	if (encoders_init() != 0) {
		sensores_cleanup();
		motor_control_cleanup();
		return -1;
	}
	return 0;
}

void roombateca_control_cleanup(void) {
	encoders_cleanup();
	sensores_cleanup();
	motor_control_cleanup();
}

int roombateca_read_sensors(float distances[SENSOR_CANTIDAD], bool valid[SENSOR_CANTIDAD]) {
	int sensor;

	if (distances == NULL || valid == NULL) return -1;
	for (sensor = 0; sensor < SENSOR_CANTIDAD; ++sensor) {
		valid[sensor] = sensor_medir((sensor_id_t)sensor, &distances[sensor]) == 0;
	}
	return 0;
}

int roombateca_read_encoders(encoder_lectura_t readings[ENCODER_CANTIDAD]) {
	if (readings == NULL) return -1;
	return encoders_leer_todos(readings);
}

int roombateca_set_motion(const char *direction, int speed) {
	int left_speed;
	int right_speed;
	int motor_speed = clamp_motor_speed(scaled_speed(speed));

	if (strcmp(direction, "FWD") == 0) {
		left_speed = motor_speed;
		right_speed = motor_speed;
	} else if (strcmp(direction, "BACK") == 0) {
		left_speed = -motor_speed;
		right_speed = -motor_speed;
	} else if (strcmp(direction, "TURN_L") == 0) {
		left_speed = -motor_speed;
		right_speed = motor_speed;
	} else if (strcmp(direction, "TURN_R") == 0) {
		left_speed = motor_speed;
		right_speed = -motor_speed;
	} else if (strcmp(direction, "STOP") == 0) {
		left_speed = 0;
		right_speed = 0;
	} else {
		return -1;
	}

	if (motor_izquierdo_set(left_speed) != 0) return -1;
	if (motor_derecho_set(right_speed) != 0) {
		motor_izquierdo_set(0);
		return -1;
	}
	return 0;
}
