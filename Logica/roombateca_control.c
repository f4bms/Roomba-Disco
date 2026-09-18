#include "roombateca_control.h"

#include "motores.h"

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
	return motor_control_init();
}

void roombateca_control_cleanup(void) {
	motor_control_cleanup();
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
