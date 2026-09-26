#ifndef MOTORES_H
#define MOTORES_H

#ifdef __cplusplus
extern "C" {
#endif

#define MOTOR_VELOCIDAD_MAX 100

/* Configura PWM y GPIO de ambos motores y los deja detenidos. Debe
 * llamarse antes de cualquier otra función. Devuelve 0 o -errno. */
int motor_control_init(void);

/* Detiene ambos motores y libera GPIO/PWM. */
void motor_control_cleanup(void);

/* velocidad en [-100, 100]: signo = sentido (positivo = adelante),
 * magnitud = fracción de la velocidad máxima segura. 0 = rueda libre.
 * Devuelve 0 o -errno (-EINVAL fuera de rango, -ENODEV sin init). */
int motor_izquierdo_set(int velocidad);
int motor_derecho_set(int velocidad);

/* Última velocidad aplicada (0 tras motores_frenar o sin init). */
int motor_izquierdo_get(void);
int motor_derecho_get(void);

/* Freno dinámico en ambos motores. Se sale con cualquier *_set. */
int motores_frenar(void);

#ifdef __cplusplus
}
#endif

#endif
