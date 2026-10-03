#include "pwm_control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PWM_MAX_CHIPS   2
#define PWM_MAX_CANALES 4

/* Tras el export, udev tarda un poco en crear pwmN/ y ajustarle permisos. */
#define PWM_EXPORT_REINTENTOS  100
#define PWM_EXPORT_ESPERA_MS   10

typedef struct {
    int fd_duty;
    int fd_enable;
    uint32_t periodo_ns;
    int invertir_sw;   /* el driver no aceptó "inversed": se invierte el duty a mano */
    int activo;
} pwm_canal_t;

static pwm_canal_t canales[PWM_MAX_CHIPS][PWM_MAX_CANALES];

static pwm_canal_t *pwm_canal(int chip, int canal) {
    if (chip < 0 || chip >= PWM_MAX_CHIPS || canal < 0 || canal >= PWM_MAX_CANALES) {
        return NULL;
    }
    return &canales[chip][canal];
}

/* Los atributos de sysfs se leen enteros en cada escritura desde el
 * offset 0; pwrite permite reusar el mismo fd abierto sin hacer lseek. */
static int escribir_fd(int fd, const char *valor) {
    size_t n = strlen(valor);
    ssize_t escrito = pwrite(fd, valor, n, 0);
    if (escrito < 0) {
        return -errno;
    }
    return (size_t)escrito == n ? 0 : -EIO;
}

static int escribir_archivo(const char *ruta, const char *valor) {
    int fd = open(ruta, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    int rv = escribir_fd(fd, valor);
    close(fd);
    return rv;
}

static int escribir_atributo(int chip, int canal, const char *atributo,
                             const char *valor) {
    char ruta[96];
    snprintf(ruta, sizeof(ruta), "/sys/class/pwm/pwmchip%d/pwm%d/%s",
             chip, canal, atributo);
    return escribir_archivo(ruta, valor);
}

static int abrir_atributo(int chip, int canal, const char *atributo) {
    char ruta[96];
    snprintf(ruta, sizeof(ruta), "/sys/class/pwm/pwmchip%d/pwm%d/%s",
             chip, canal, atributo);
    int fd = open(ruta, O_WRONLY | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}

static int exportar(int chip, int canal) {
    char ruta[96];
    char valor[16];

    /* Si period ya existe y se puede escribir, el canal quedó exportado
     * de una ejecución anterior: no hace falta exportarlo de nuevo. */
    snprintf(ruta, sizeof(ruta), "/sys/class/pwm/pwmchip%d/pwm%d/period", chip, canal);
    if (access(ruta, W_OK) == 0) {
        return 0;
    }

    char ruta_export[64];
    snprintf(ruta_export, sizeof(ruta_export), "/sys/class/pwm/pwmchip%d/export", chip);
    snprintf(valor, sizeof(valor), "%d", canal);
    /* EBUSY = ya exportado (p. ej. por otro proceso); igual se espera a
     * que el atributo quede escribible. */
    int rv = escribir_archivo(ruta_export, valor);
    if (rv < 0 && rv != -EBUSY) {
        return rv;
    }

    struct timespec espera = { .tv_sec = 0, .tv_nsec = PWM_EXPORT_ESPERA_MS * 1000000L };
    for (int i = 0; i < PWM_EXPORT_REINTENTOS; i++) {
        if (access(ruta, W_OK) == 0) {
            return 0;
        }
        nanosleep(&espera, NULL);
    }
    return -ETIMEDOUT;
}

static void cerrar_canal(pwm_canal_t *c) {
    if (c->fd_duty >= 0) {
        close(c->fd_duty);
    }
    if (c->fd_enable >= 0) {
        close(c->fd_enable);
    }
    c->fd_duty = -1;
    c->fd_enable = -1;
    c->activo = 0;
}

int pwm_init(int chip, int canal, uint32_t periodo_ns, int invertido) {
    pwm_canal_t *c = pwm_canal(chip, canal);
    if (!c || periodo_ns == 0) {
        return -EINVAL;
    }
    if (c->activo) {
        cerrar_canal(c);
    }
    c->fd_duty = -1;
    c->fd_enable = -1;

    int rv = exportar(chip, canal);
    if (rv < 0) {
        return rv;
    }

    /* polarity solo se acepta deshabilitado, y duty_cycle nunca puede
     * superar a period: se baja el duty antes de fijar el periodo. */
    char valor[16];
    escribir_atributo(chip, canal, "enable", "0");
    escribir_atributo(chip, canal, "duty_cycle", "0");
    snprintf(valor, sizeof(valor), "%u", periodo_ns);
    rv = escribir_atributo(chip, canal, "period", valor);
    if (rv < 0) {
        return rv;
    }

    /* No todos los drivers soportan polaridad invertida. Si falla, se
     * invierte el duty en software (duty' = periodo − duty, ver
     * pwm_set_duty): la forma de onda resultante es la misma. */
    c->invertir_sw = 0;
    rv = escribir_atributo(chip, canal, "polarity", invertido ? "inversed" : "normal");
    if (rv < 0) {
        if (!invertido) {
            return rv;
        }
        c->invertir_sw = 1;
    }

    /* duty_cycle y enable quedan abiertos mientras el canal esté activo:
     * los motores cambian el duty a menudo y así cada cambio es una sola
     * escritura, sin open/close. */
    c->fd_duty = abrir_atributo(chip, canal, "duty_cycle");
    c->fd_enable = abrir_atributo(chip, canal, "enable");
    if (c->fd_duty < 0 || c->fd_enable < 0) {
        rv = c->fd_duty < 0 ? c->fd_duty : c->fd_enable;
        cerrar_canal(c);
        return rv;
    }

    c->periodo_ns = periodo_ns;
    c->activo = 1;
    return pwm_set_duty(chip, canal, 0);
}

int pwm_set_duty(int chip, int canal, uint32_t duty_ns) {
    pwm_canal_t *c = pwm_canal(chip, canal);
    if (!c || !c->activo) {
        return -ENODEV;
    }
    if (duty_ns > c->periodo_ns) {
        return -EINVAL;
    }
    if (c->invertir_sw) {
        duty_ns = c->periodo_ns - duty_ns;
    }
    char valor[16];
    snprintf(valor, sizeof(valor), "%u", duty_ns);
    return escribir_fd(c->fd_duty, valor);
}

int pwm_enable(int chip, int canal, int habilitado) {
    pwm_canal_t *c = pwm_canal(chip, canal);
    if (!c || !c->activo) {
        return -ENODEV;
    }
    return escribir_fd(c->fd_enable, habilitado ? "1" : "0");
}

void pwm_cleanup(int chip, int canal) {
    pwm_canal_t *c = pwm_canal(chip, canal);
    if (!c || !c->activo) {
        return;
    }
    pwm_set_duty(chip, canal, 0);
    pwm_enable(chip, canal, 0);
    cerrar_canal(c);

    char ruta[64];
    char valor[16];
    snprintf(ruta, sizeof(ruta), "/sys/class/pwm/pwmchip%d/unexport", chip);
    snprintf(valor, sizeof(valor), "%d", canal);
    escribir_archivo(ruta, valor);
}
