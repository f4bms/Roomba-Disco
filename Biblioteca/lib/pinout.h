#ifndef ROOMBATECA_PINOUT_H
#define ROOMBATECA_PINOUT_H

/* Mapa de pines de la Raspberry Pi 4 (numeración BCM). */

#define PINOUT_GPIOCHIP "/dev/gpiochip0"

/* Tracción: L298N, a través de los PC817 (todas las líneas invertidas). */
#define PIN_MOTOR_IZQ_ENA     12   /* pin 32, PWM0 canal 0 */
#define PIN_MOTOR_IZQ_IN1      5   /* pin 29 */
#define PIN_MOTOR_IZQ_IN2      6   /* pin 31 */
#define PIN_MOTOR_DER_ENB     13   /* pin 33, PWM0 canal 1 */
#define PIN_MOTOR_DER_IN3     16   /* pin 36 */
#define PIN_MOTOR_DER_IN4     17   /* pin 11 */

#define PWM_CHIP              0    /* /sys/class/pwm/pwmchip0 */
#define PWM_CANAL_MOTOR_IZQ   0    /* GPIO12 */
#define PWM_CANAL_MOTOR_DER   1    /* GPIO13 */

/* 1 = la línea llega invertida al L298N por el optoacoplador. */
#define MOTOR_DIRECCION_INVERTIDO 1
#define MOTOR_PWM_INVERTIDO       1

/* Sensores de proximidad HC-SR04. ECHO llega por divisor 2,2 kΩ / 3,3 kΩ. */
#define PIN_US_FRONTAL_TRIG   20   /* pin 38 */
#define PIN_US_FRONTAL_ECHO   21   /* pin 40 */
#define PIN_US_IZQ_TRIG       19   /* pin 35 */
#define PIN_US_IZQ_ECHO       26   /* pin 37 */
#define PIN_US_DER_TRIG       10   /* pin 19 */
#define PIN_US_DER_ECHO        9   /* pin 21 */

/* Encoders ópticos F249 (DO a 3,3 V). Izquierdo = rueda del motor de ENA. */
#define PIN_ENCODER_IZQ       27   /* pin 13 */
#define PIN_ENCODER_DER       11   /* pin 23 */

/* LEDs de estado, directos con resistencia serie (≤ 5 mA). */
#define PIN_LED_ENCENDIDO     25   /* pin 22, verde */
#define PIN_LED_ALERTA        24   /* pin 18, rojo */
#define PIN_LED_MANUAL        23   /* pin 16, amarillo */
#define PIN_LED_AUTONOMO      22   /* pin 15, azul */

#endif
