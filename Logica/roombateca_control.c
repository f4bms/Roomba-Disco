#include "roombateca_control.h"

#include "encoders.h"
#include "leds.h"
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

static int audio_disponible;
static const char *const audio_tracks[] = {
	AUDIO_TRACK_1,
	AUDIO_TRACK_2,
	AUDIO_TRACK_3,
};

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
	if (leds_init() != 0) {
		encoders_cleanup();
		sensores_cleanup();
		motor_control_cleanup();
		return -1;
	}
	led_set(LED_ENCENDIDO, true);
	audio_disponible = audio_control_init() == 0;
	if (audio_disponible) trigger_notification_audio(AUDIO_INICIO_SYS);
	return 0;
}

void roombateca_control_cleanup(void) {
	audio_control_cleanup();
	audio_disponible = 0;
	encoders_cleanup();
	sensores_cleanup();
	motor_control_cleanup();
	led_set(LED_ENCENDIDO, false);
	leds_cleanup();
}

void roombateca_set_mode_leds(const char *mode) {
	if (mode == NULL) return;
	led_set(LED_MANUAL,   strcmp(mode, "MANUAL") == 0);
	led_set(LED_AUTONOMO, strcmp(mode, "AUTO")   == 0);
}

int roombateca_audio_play_track(int track) {
	if (!audio_disponible || track < 0
			|| track >= (int)(sizeof(audio_tracks) / sizeof(audio_tracks[0]))) return -1;
	return audio_play(audio_tracks[track]);
}

int roombateca_audio_pause(void) {
	return audio_disponible ? audio_pause() : -1;
}

int roombateca_audio_resume(void) {
	return audio_disponible ? audio_resume() : -1;
}

int roombateca_audio_stop(void) {
	return audio_disponible ? audio_stop() : -1;
}

int roombateca_audio_set_volume(int volume) {
	return audio_disponible ? audio_set_volume(volume) : -1;
}

audio_estado_t roombateca_audio_get_state(void) {
	return audio_disponible ? audio_get_state() : AUDIO_STOP;
}

int roombateca_audio_get_volume(void) {
	return audio_disponible ? audio_get_volume() : -1;
}

int roombateca_audio_obstacle_alert(void) {
	audio_estado_t previous_state;
	int result;

	if (!audio_disponible) return -1;
	previous_state = audio_get_state();
	if (previous_state == AUDIO_PLAY) {
		if (audio_pause() != 0) return -1;
	}
	result = play_notification_wait(AUDIO_ALERTA);
	if (result == 0 && previous_state == AUDIO_PLAY) {
		result = audio_resume();
	}
	return result;
}

bool roombateca_audio_available(void) {
	return audio_disponible != 0;
}

void roombateca_audio_notify_mode(const char *mode) {
	if (!audio_disponible || mode == NULL) return;
	trigger_notification_audio(strcmp(mode, "AUTO") == 0
		? AUDIO_AUTO_MODE : AUDIO_MANUAL_MODE);
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
