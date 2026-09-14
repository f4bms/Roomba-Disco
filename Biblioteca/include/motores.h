#ifndef MOTORES_H
#define MOTORES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa GPIO y PWM para ambos motores. Debe llamarse antes de cualquier
 * motor_*_set. Devuelve 0 en éxito, -1 en error. */
int motor_control_init(void);

/* Detiene ambos motores y libera GPIO/PWM. */
void motor_control_cleanup(void);

/* velocidad en [-100, 100]: signo = dirección, magnitud = duty cycle. */
int motor_izquierdo_set(int velocidad);
int motor_derecho_set(int velocidad);

#ifdef __cplusplus
}
#endif

#endif
