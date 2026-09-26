#ifndef PWM_CONTROL_H
#define PWM_CONTROL_H

#include <stdint.h>

/* Exporta y configura un canal, deshabilitado y con duty 0.
 * Devuelve 0 o -errno. */
int pwm_init(int chip, int canal, uint32_t periodo_ns, int invertido);

/* duty_ns en [0, periodo_ns]. */
int pwm_set_duty(int chip, int canal, uint32_t duty_ns);

int pwm_enable(int chip, int canal, int habilitado);

/* Duty 0, deshabilita y libera el canal. */
void pwm_cleanup(int chip, int canal);

#endif
