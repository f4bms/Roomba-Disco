#include "roombateca_control.h"

#include "encoders.h"
#include "leds.h"
#include "motores.h"
#include "sensores.h"
#include "succion.h"

#include <stdlib.h>
#include <string.h>

/* Sincronización de ruedas: en cada tick se compara cuántos pulsos lleva
 * cada rueda desde que empezó el movimiento y se frena la adelantada (a
 * velocidad 100 no hay margen para acelerar la otra). La parte integral se
 * conserva entre movimientos del mismo tipo y funciona como trim aprendido.
 * Por definir: afinar KP/KI con el robot en el piso. */
#define SYNC_KP          4.0   /* unidades de velocidad por pulso de diferencia */
#define SYNC_KI          0.5   /* por pulso de diferencia y por tick */
#define SYNC_AJUSTE_MAX  40.0  /* corrección máxima, en unidades de velocidad */

/* Rampa de arranque: la velocidad base sube este tanto por tick (0 a 100 en
 * ~0,5 s) para no patinar ni pedirle un pico de corriente al pack. */
#define RAMPA_PASO       20

enum { MOV_FWD, MOV_BACK, MOV_TURN_L, MOV_TURN_R, MOV_CANTIDAD };

static struct {
	int activo;
	int tipo;
	int velocidad;
	int base;          /* velocidad actual de la rampa, hasta llegar a velocidad */
	int signo_izq;
	int signo_der;
	int64_t inicio_izq;
	int64_t inicio_der;
} mov;

static double ajuste_aprendido[MOV_CANTIDAD];

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
	succion_init();
	audio_disponible = audio_control_init() == 0;
	if (audio_disponible) trigger_notification_audio(AUDIO_INICIO_SYS);
	return 0;
}

void roombateca_control_cleanup(void) {
	audio_control_cleanup();
	audio_disponible = 0;
	succion_set(0);
	succion_cleanup();
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

void roombateca_aspiradora_set(bool activa) {
	succion_set(activa ? SUCCION_POTENCIA_MAX : 0);
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

static int set_wheels(int left_speed, int right_speed) {
	if (motor_izquierdo_set(left_speed) != 0) return -1;
	if (motor_derecho_set(right_speed) != 0) {
		motor_izquierdo_set(0);
		return -1;
	}
	return 0;
}

/* error > 0: la izquierda va adelantada. Se le resta la corrección a la
 * rueda adelantada sin bajarla de 1 (por debajo quedaría en rueda libre). */
static int apply_synced_speeds(double error) {
	double ajuste = SYNC_KP * error + ajuste_aprendido[mov.tipo];
	int left = mov.base;
	int right = mov.base;
	int reduccion;

	if (ajuste > SYNC_AJUSTE_MAX) ajuste = SYNC_AJUSTE_MAX;
	if (ajuste < -SYNC_AJUSTE_MAX) ajuste = -SYNC_AJUSTE_MAX;
	reduccion = (int)(ajuste >= 0.0 ? ajuste + 0.5 : -ajuste + 0.5);
	if (ajuste > 0.0) {
		left = mov.base - reduccion < 1 ? 1 : mov.base - reduccion;
	} else {
		right = mov.base - reduccion < 1 ? 1 : mov.base - reduccion;
	}
	return set_wheels(mov.signo_izq * left, mov.signo_der * right);
}

int roombateca_set_motion(const char *direction, int speed) {
	encoder_lectura_t lecturas[ENCODER_CANTIDAD];
	int motor_speed = clamp_motor_speed(scaled_speed(speed));
	int tipo;
	int signo_izq;
	int signo_der;

	if (strcmp(direction, "FWD") == 0) {
		tipo = MOV_FWD;    signo_izq = 1;  signo_der = 1;
	} else if (strcmp(direction, "BACK") == 0) {
		tipo = MOV_BACK;   signo_izq = -1; signo_der = -1;
	} else if (strcmp(direction, "TURN_L") == 0) {
		tipo = MOV_TURN_L; signo_izq = -1; signo_der = 1;
	} else if (strcmp(direction, "TURN_R") == 0) {
		tipo = MOV_TURN_R; signo_izq = 1;  signo_der = -1;
	} else if (strcmp(direction, "STOP") == 0) {
		mov.activo = 0;
		/* Freno dinámico: en rueda libre el robot sigue rodando por inercia
		 * y se pasa en los giros. */
		return motores_frenar();
	} else {
		return -1;
	}

	if (motor_speed == 0) {
		mov.activo = 0;
		return motores_frenar();
	}
	/* El modo AUTO repite la misma orden en cada tick: si no cambió, se
	 * conserva la corrección en curso en vez de volver a la velocidad base. */
	if (mov.activo && mov.tipo == tipo && mov.velocidad == motor_speed) return 0;

	/* Un cambio de velocidad en el mismo sentido sigue la rampa desde donde
	 * iba; un arranque o cambio de sentido la empieza de cero. Bajar es
	 * inmediato. */
	if (!(mov.activo && mov.tipo == tipo)) mov.base = 0;
	if (mov.base == 0) mov.base = motor_speed < RAMPA_PASO ? motor_speed : RAMPA_PASO;
	if (mov.base > motor_speed) mov.base = motor_speed;
	mov.tipo = tipo;
	mov.velocidad = motor_speed;
	mov.signo_izq = signo_izq;
	mov.signo_der = signo_der;
	mov.activo = encoders_leer_todos(lecturas) == 0;
	if (!mov.activo) return set_wheels(signo_izq * motor_speed, signo_der * motor_speed);
	mov.inicio_izq = lecturas[ENCODER_IZQUIERDO].pulsos;
	mov.inicio_der = lecturas[ENCODER_DERECHO].pulsos;
	return apply_synced_speeds(0.0);
}

void roombateca_sync_wheels(const encoder_lectura_t readings[ENCODER_CANTIDAD]) {
	double error;

	if (!mov.activo || readings == NULL) return;
	if (mov.base < mov.velocidad) {
		mov.base = mov.base + RAMPA_PASO < mov.velocidad ? mov.base + RAMPA_PASO : mov.velocidad;
	}
	error =(double)(llabs(readings[ENCODER_IZQUIERDO].pulsos - mov.inicio_izq)
	                 - llabs(readings[ENCODER_DERECHO].pulsos - mov.inicio_der));
	/* Solo se aprende con las dos ruedas girando: una rueda trabada no es
	 * diferencia entre motores y dejaría el trim saturado. */
	if (readings[ENCODER_IZQUIERDO].velocidad_mm_s != 0.0
			&& readings[ENCODER_DERECHO].velocidad_mm_s != 0.0) {
		double *ajuste = &ajuste_aprendido[mov.tipo];
		*ajuste += SYNC_KI * error;
		if (*ajuste > SYNC_AJUSTE_MAX) *ajuste = SYNC_AJUSTE_MAX;
		if (*ajuste < -SYNC_AJUSTE_MAX) *ajuste = -SYNC_AJUSTE_MAX;
	}
	apply_synced_speeds(error);
}
