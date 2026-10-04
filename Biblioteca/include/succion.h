#ifndef SUCCION_H
#define SUCCION_H

#ifdef __cplusplus
extern "C" {
#endif

#define SUCCION_POTENCIA_MAX 100

/* Configura el pin del motor de succión y arranca el PWM con el motor
 * apagado. Devuelve 0 o -errno. */
int succion_init(void);

/* Apaga el motor y libera el pin. */
void succion_cleanup(void);

/* potencia en [0, 100]: 0 = apagado, 100 = máximo seguro del motor.
 * Al subir, el motor acelera con rampa. Devuelve 0 o -errno
 * (-EINVAL fuera de rango, -ENODEV sin init). */
int succion_set(int potencia);

/* Última potencia pedida (0 sin init). */
int succion_get(void);

#ifdef __cplusplus
}
#endif

#endif
