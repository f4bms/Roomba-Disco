#include "motores.h"

#include "gpio_control.h"
#include "pinout.h"
#include "pwm_control.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define MOTOR_PWM_PERIODO_NS 1000000u   /* 1 kHz, límite por los PC817 */

/* Rango útil de duty (%) al que se mapea velocidad 1..100. El máximo topa
 * la tensión efectiva en los TT con el pack lleno; el mínimo salta la zona
 * muerta. Por definir: medir en bornes del motor y ajustar ambos. */
#define MOTOR_DUTY_MIN_PCT 25u
#define MOTOR_DUTY_MAX_PCT 70u

/* Rueda libre entre un sentido y el opuesto, para no invertir en seco. */
#define MOTOR_PAUSA_INVERSION_MS 30

/* +1 o -1 según cómo quedaron soldados los cables de cada motor. */
#define MOTOR_IZQ_SENTIDO  1
#define MOTOR_DER_SENTIDO  1

typedef struct {
    int pin_in_a;       /* IN1 / IN3 */
    int pin_in_b;       /* IN2 / IN4 */
    int canal_pwm;
    int sentido;
    int velocidad;      /* último valor aplicado, en [-100, 100] */
    int frenado;
} motor_t;

static motor_t motor_izq = {
    .pin_in_a = PIN_MOTOR_IZQ_IN1,
    .pin_in_b = PIN_MOTOR_IZQ_IN2,
    .canal_pwm = PWM_CANAL_MOTOR_IZQ,
    .sentido = MOTOR_IZQ_SENTIDO,
};

static motor_t motor_der = {
    .pin_in_a = PIN_MOTOR_DER_IN3,
    .pin_in_b = PIN_MOTOR_DER_IN4,
    .canal_pwm = PWM_CANAL_MOTOR_DER,
    .sentido = MOTOR_DER_SENTIDO,
};

static pthread_mutex_t motores_mutex = PTHREAD_MUTEX_INITIALIZER;
static int inicializado = 0;

/* Mapea magnitud 1..100 lineal a [MIN, MAX] % del periodo:
 *   pct = MIN + (MAX - MIN) · (magnitud - 1) / 99
 * Se multiplica todo por 99 antes de dividir para hacer la cuenta en
 * enteros sin perder los decimales del porcentaje. Magnitud 1 cae justo
 * en MIN (el motor ya arranca) y 100 justo en MAX. */
static uint32_t duty_para(int magnitud) {
    if (magnitud <= 0) {
        return 0;
    }
    uint64_t pct_x99 = (uint64_t)MOTOR_DUTY_MIN_PCT * 99u +
                       (uint64_t)(MOTOR_DUTY_MAX_PCT - MOTOR_DUTY_MIN_PCT) * (uint64_t)(magnitud - 1);
    return (uint32_t)((uint64_t)MOTOR_PWM_PERIODO_NS * pct_x99 / (100u * 99u));
}

static int escribir_direccion(const motor_t *m, int a, int b) {
    if (digitalWrite(m->pin_in_a, a) != 0 || digitalWrite(m->pin_in_b, b) != 0) {
        return -EIO;
    }
    return 0;
}

static void pausa_ms(long ms) {
    struct timespec t = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* Rueda libre: EN en bajo y las dos entradas de sentido en bajo. */
static int motor_liberar(motor_t *m) {
    int rv = pwm_set_duty(PWM_CHIP, m->canal_pwm, 0);
    if (rv == 0) {
        rv = escribir_direccion(m, 0, 0);
    }
    return rv;
}

static int motor_aplicar(motor_t *m, int velocidad) {
    if (velocidad == 0) {
        int rv = motor_liberar(m);
        if (rv == 0) {
            m->velocidad = 0;
            m->frenado = 0;
        }
        return rv;
    }

    /* "fisica" es el sentido real del eje, ya corregido por cómo quedó
     * cableado el motor; el resto de la función trabaja solo con ese. */
    int fisica = velocidad * m->sentido;
    int actual = m->velocidad * m->sentido;
    int cambia_sentido = actual != 0 && (actual > 0) != (fisica > 0);
    int rv;

    /* Los pines de sentido solo se reescriben si hace falta: al salir del
     * freno (IN1 = IN2 = 1), al invertir o al arrancar desde quieto. Si el
     * motor ya gira hacia el mismo lado, basta con cambiar el duty y así
     * no hay tirones. Antes de tocar IN se corta EN (motor_liberar) para no
     * pasar por una combinación intermedia con el motor energizado. */
    if (m->frenado || cambia_sentido || actual == 0) {
        rv = motor_liberar(m);
        if (rv < 0) {
            return rv;
        }
        /* Dejar que la rueda pierda inercia antes de invertir: invertir en
         * seco genera un pico de corriente en el L298N y en la batería. */
        if (cambia_sentido) {
            pausa_ms(MOTOR_PAUSA_INVERSION_MS);
        }
        /* Tabla del L298N: IN_a = 1, IN_b = 0 → adelante; 0, 1 → atrás. */
        rv = fisica > 0 ? escribir_direccion(m, 1, 0) : escribir_direccion(m, 0, 1);
        if (rv < 0) {
            return rv;
        }
    }

    rv = pwm_set_duty(PWM_CHIP, m->canal_pwm, duty_para(fisica > 0 ? fisica : -fisica));
    if (rv == 0) {
        m->velocidad = velocidad;
        m->frenado = 0;
    }
    return rv;
}

/* Freno dinámico: IN1 = IN2 en alto con EN al 100 %. El puente H pone
 * en corto los bornes del motor y la fuerza contraelectromotriz lo frena,
 * mucho más rápido que dejarlo en rueda libre. EN se baja primero para que
 * el cambio de IN no ocurra con el motor energizado. */
static int motor_frenar(motor_t *m) {
    int rv = pwm_set_duty(PWM_CHIP, m->canal_pwm, 0);
    if (rv == 0) {
        rv = escribir_direccion(m, 1, 1);
    }
    if (rv == 0) {
        rv = pwm_set_duty(PWM_CHIP, m->canal_pwm, MOTOR_PWM_PERIODO_NS);
    }
    if (rv == 0) {
        m->velocidad = 0;
        m->frenado = 1;
    }
    return rv;
}

/* Los PC817 invierten la señal (GPIO alto → entrada del L298N en bajo).
 * En vez de invertir cada escritura, se compensa en la configuración:
 * active-low en los GPIO de sentido y polaridad invertida en el PWM. Así
 * el resto del módulo razona en lógica positiva, como si no hubiera
 * optoacoplador. */
static int motor_iniciar(motor_t *m) {
    unsigned int flags = MOTOR_DIRECCION_INVERTIDO ? GPIO_FLAG_ACTIVE_LOW : 0;
    int rv = pinModeEx(m->pin_in_a, GPIO_OUTPUT, flags, 0);
    if (rv == 0) {
        rv = pinModeEx(m->pin_in_b, GPIO_OUTPUT, flags, 0);
    }
    if (rv == 0) {
        rv = pwm_init(PWM_CHIP, m->canal_pwm, MOTOR_PWM_PERIODO_NS, MOTOR_PWM_INVERTIDO);
    }
    if (rv == 0) {
        rv = pwm_enable(PWM_CHIP, m->canal_pwm, 1);
    }
    m->velocidad = 0;
    m->frenado = 0;
    return rv;
}

/* Deja IN1 = IN2 antes de soltar todo: sea cual sea el estado en que quede
 * EN, el motor no puede girar. */
static void motor_detener_y_liberar(motor_t *m) {
    pwm_set_duty(PWM_CHIP, m->canal_pwm, 0);
    escribir_direccion(m, 0, 0);
    pwm_cleanup(PWM_CHIP, m->canal_pwm);
    pinRelease(m->pin_in_a);
    pinRelease(m->pin_in_b);
    m->velocidad = 0;
    m->frenado = 0;
}

int motor_control_init(void) {
    pthread_mutex_lock(&motores_mutex);
    if (inicializado) {
        pthread_mutex_unlock(&motores_mutex);
        return 0;
    }

    int rv = motor_iniciar(&motor_izq);
    if (rv == 0) {
        rv = motor_iniciar(&motor_der);
    }
    if (rv < 0) {
        motor_detener_y_liberar(&motor_der);
        motor_detener_y_liberar(&motor_izq);
    } else {
        inicializado = 1;
    }

    pthread_mutex_unlock(&motores_mutex);
    return rv;
}

void motor_control_cleanup(void) {
    pthread_mutex_lock(&motores_mutex);
    if (inicializado) {
        motor_detener_y_liberar(&motor_izq);
        motor_detener_y_liberar(&motor_der);
        inicializado = 0;
    }
    pthread_mutex_unlock(&motores_mutex);
}

static int motor_set(motor_t *m, int velocidad) {
    if (velocidad < -MOTOR_VELOCIDAD_MAX || velocidad > MOTOR_VELOCIDAD_MAX) {
        return -EINVAL;
    }
    pthread_mutex_lock(&motores_mutex);
    int rv = inicializado ? motor_aplicar(m, velocidad) : -ENODEV;
    pthread_mutex_unlock(&motores_mutex);
    return rv;
}

static int motor_get(const motor_t *m) {
    pthread_mutex_lock(&motores_mutex);
    int velocidad = inicializado ? m->velocidad : 0;
    pthread_mutex_unlock(&motores_mutex);
    return velocidad;
}

int motor_izquierdo_set(int velocidad) {
    return motor_set(&motor_izq, velocidad);
}

int motor_derecho_set(int velocidad) {
    return motor_set(&motor_der, velocidad);
}

int motor_izquierdo_get(void) {
    return motor_get(&motor_izq);
}

int motor_derecho_get(void) {
    return motor_get(&motor_der);
}

int motores_frenar(void) {
    pthread_mutex_lock(&motores_mutex);
    int rv = -ENODEV;
    if (inicializado) {
        /* Se intenta frenar el derecho aunque falle el izquierdo: ante una
         * emergencia importa detener lo que se pueda; se reporta el primer
         * error. */
        rv = motor_frenar(&motor_izq);
        int rv_der = motor_frenar(&motor_der);
        if (rv == 0) {
            rv = rv_der;
        }
    }
    pthread_mutex_unlock(&motores_mutex);
    return rv;
}
