#include "auto.h"

#include <math.h>
#include <stddef.h>

#define PI 3.14159265358979323846

//revisa si hay obstaculo
static bool if_obstaculo(float distancia_cm, bool valida, float umbral_cm) {
    return valida && distancia_cm < umbral_cm;
}
//Es para que al girar se pasa del rango que estamos usando entonces
//es para reajustar el numero a [-PI, PI]
static double normalizar(double angulo_rad) {
    while (angulo_rad > PI) angulo_rad -= 2.0 * PI;
    while (angulo_rad < -PI) angulo_rad += 2.0 * PI;
    return angulo_rad;
}
//para hacer mediciones, es una diferencia
static double distancia_desde_inicio(const auto_t *a, const odometria_pose_t *pose) {
    return hypot(pose->x_mm - a->x_inicio_mm, pose->y_mm - a->y_inicio_mm);
}
//rumbo de la fila actual: ida (rumbo_fila) o vuelta (rumbo_fila + 180)
static double rumbo_de_fila(const auto_t *a) {
    return normalizar(a->rumbo_fila_rad + (a->sentido ? PI : 0.0));
}
//el movimiento lateral siempre va hacia el mismo lado (el primer giro es a la derecha)
static double rumbo_lateral(const auto_t *a) {
    return normalizar(a->rumbo_fila_rad - AUTO_GIRO_RAD);
}
//para cambio de fase y prepararse para la nueva fase
//Fases: auto_avanzar, auto_parar, auto_girar, auto_mover, auto_retroceder y auto_fin
static void entrar_fase(auto_t *a, auto_fase_t fase,
                        const odometria_pose_t *pose, uint64_t ahora_ns) {
    a->fase = fase;
    a->inicio_fase_ns = ahora_ns;
    if (fase == AUTO_AVANZAR && a->corrigiendo) {
        a->corrigiendo = false;      /* sigue la misma fila: conserva su inicio */
    } else if (fase == AUTO_AVANZAR || fase == AUTO_MOVER || fase == AUTO_RETROCEDER) {
        a->x_inicio_mm = pose->x_mm;
        a->y_inicio_mm = pose->y_mm;
    } else if (fase == AUTO_GIRAR) {
        a->theta_anterior_rad = pose->theta_rad;
        a->giro_acumulado_rad = 0.0;
        a->giro_fino = false;
        a->giro_dir = normalizar(a->rumbo_objetivo_rad - pose->theta_rad) > 0.0 ? 1 : -1;
    }
    a->atasco_ref_ns = ahora_ns;
    a->atasco_ref_x_mm = pose->x_mm;
    a->atasco_ref_y_mm = pose->y_mm;
    a->atasco_ref_giro_rad = a->giro_acumulado_rad;
}
//después de cada maniobra se detiene para evitar descuadre a la hora de girar principalmente
static void parar_y_luego(auto_t *a, auto_fase_t siguiente,
                          const odometria_pose_t *pose, uint64_t ahora_ns) {
    a->fase_siguiente = siguiente;
    entrar_fase(a, AUTO_PARAR, pose, ahora_ns);
}
//pausa y luego gira hasta el rumbo absoluto indicado
static void parar_y_girar_a(auto_t *a, double rumbo_rad,
                            const odometria_pose_t *pose, uint64_t ahora_ns) {
    a->rumbo_objetivo_rad = normalizar(rumbo_rad);
    parar_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
}
//señala que ya se terminó el recorrido y lo manda a manual
static void ir_a_fin(auto_t *a, const char *motivo,
                     const odometria_pose_t *pose, uint64_t ahora_ns) {
    a->motivo_fin = motivo;
    entrar_fase(a, AUTO_FIN, pose, ahora_ns);
}
//tras un atasco retrocede y luego sigue con 'siguiente'; si se traba muchas
//veces seguidas sin completar una maniobra, el robot no logra salir y termina
static void retroceder_y_luego(auto_t *a, auto_fase_t siguiente,
                               const odometria_pose_t *pose, uint64_t ahora_ns) {
    if (++a->atascos > AUTO_ATASCOS_MAX) {
        ir_a_fin(a, "atascos seguidos", pose, ahora_ns);
        return;
    }
    a->fase_siguiente = siguiente;
    entrar_fase(a, AUTO_RETROCEDER, pose, ahora_ns);
}
//true si con los motores andando la odometria no avanzo en la ultima ventana.
//Durante la gracia del arranque solo se corre la referencia.
static bool atascado(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns) {
    bool sin_avance = false;

    if (ahora_ns - a->inicio_fase_ns >= AUTO_ATASCO_GRACIA_NS) {
        if (ahora_ns - a->atasco_ref_ns < AUTO_ATASCO_VENTANA_NS) return false;
        if (a->fase == AUTO_GIRAR) {
            sin_avance = a->giro_acumulado_rad - a->atasco_ref_giro_rad
                         < AUTO_ATASCO_MIN_RAD;
        } else {
            sin_avance = hypot(pose->x_mm - a->atasco_ref_x_mm,
                               pose->y_mm - a->atasco_ref_y_mm) < AUTO_ATASCO_MIN_MM;
        }
    }
    a->atasco_ref_ns = ahora_ns;
    a->atasco_ref_x_mm = pose->x_mm;
    a->atasco_ref_y_mm = pose->y_mm;
    a->atasco_ref_giro_rad = a->giro_acumulado_rad;
    return sin_avance;
}
//fin de fila, por el ultrasonico o por un atasco. Con atasco primero retrocede
//y no aplica la regla de fila vacia: trabarse al arrancar la fila no quiere
//decir que ya no quede por barrer
static void fin_de_fila(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns,
                        bool por_atasco) {
    if (!por_atasco && a->etapa == AUTO_ETAPA_BARRIDO
            && distancia_desde_inicio(a, pose) < AUTO_FILA_MIN_MM) {
        ir_a_fin(a, "fila vacia", pose, ahora_ns);   /* ya no hay mas por barrer */
        return;
    }
    if (a->etapa == AUTO_ETAPA_ESQUINA_1) {
        // Pared 1: giro simple a la derecha. 
        //Tramo 2 (a lo largo de Pared 1) es la 1ra fila y su rumbo queda de referencia
        a->etapa = AUTO_ETAPA_ESQUINA_2;
        a->rumbo_fila_rad = normalizar(pose->theta_rad - AUTO_GIRO_RAD);
        a->sentido = 0;
        a->giro_n = 2;
        a->rumbo_objetivo_rad = a->rumbo_fila_rad;
    } else {
        //Pared 2 (esquina) o fin de una fila del barrido: hacia el lateral 
        a->etapa = AUTO_ETAPA_BARRIDO;
        a->giro_n = 1;
        a->rumbo_objetivo_rad = rumbo_lateral(a);
    }
    if (por_atasco) {
        retroceder_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
    } else {
        a->atascos = 0;
        parar_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
    }
}
//el movimiento lateral terminó (completo o parcial): giro hacia la fila de vuelta
static void fin_de_mover(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns,
                         bool por_atasco) {
    a->giro_n = 2;
    a->sentido ^= 1;
    a->rumbo_objetivo_rad = rumbo_de_fila(a);
    if (por_atasco) {
        retroceder_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
    } else {
        a->atascos = 0;
        parar_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
    }
}

static auto_orden_t orden_de_fase(const auto_t *a) {
    // La aspiradora se enciende desde Tramo 2 de la esquina (pegado a Pared 1)
    const bool aspirar = a->etapa != AUTO_ETAPA_ESQUINA_1 && a->fase != AUTO_FIN;

    switch (a->fase) {
    case AUTO_AVANZAR:
        return (auto_orden_t){"FWD", AUTO_VEL_AVANCE, aspirar};
    case AUTO_MOVER:
        return (auto_orden_t){"FWD", AUTO_VEL_MOVER, aspirar};
    case AUTO_RETROCEDER:
        return (auto_orden_t){"BACK", AUTO_VEL_RETROCEDER, aspirar};
    case AUTO_GIRAR:
        return (auto_orden_t){a->giro_dir > 0 ? "TURN_L" : "TURN_R",
                              a->giro_fino ? AUTO_VEL_GIRO_FINO : AUTO_VEL_GIRO, aspirar};
    case AUTO_PARAR:
    case AUTO_FIN:
    default:
        return (auto_orden_t){"STOP", 0, aspirar};
    }
}

void auto_init(auto_t *a) {
    auto_reset(a);
}
//Para poner los valores en su estado inicial
void auto_reset(auto_t *a) {
    if (a == NULL) return;
    *a = (auto_t){0};
    a->fase = AUTO_AVANZAR;
    a->fase_siguiente = AUTO_AVANZAR;
    a->etapa = AUTO_BUSCAR_ESQUINA ? AUTO_ETAPA_ESQUINA_1
                                   : AUTO_ETAPA_BARRIDO;
    a->giro_n = 1;
    a->giro_dir = -1;             //el primer giro es a la derecha
    a->motivo_fin = "";
}
//es como un if auto_fin? devuelve true si está en fase auto_fin
bool auto_fin(const auto_t *a) {
    return a != NULL && a->fase == AUTO_FIN;
}
//ESte es el que manda las ordenes al robot dependiendo de su fase
auto_orden_t auto_paso(auto_t *a, const odometria_pose_t *pose,
                       const auto_sensores_t *s, uint64_t ahora_ns) {
    const auto_orden_t parar = {"STOP", 0, false};
    uint64_t transcurrido;

    if (a == NULL || pose == NULL || s == NULL) return parar;
    if (a->inicio_fase_ns == 0 && a->fase == AUTO_AVANZAR) {
        // primer tick tras reset: fija el origen de la primera fila y el rumbo 
        a->inicio_ns = ahora_ns;
        a->rumbo_fila_rad = pose->theta_rad;
        entrar_fase(a, AUTO_AVANZAR, pose, ahora_ns);
    }
    if (a->fase != AUTO_FIN && ahora_ns - a->inicio_ns >= AUTO_TIMEOUT_TOTAL_NS) {
        ir_a_fin(a, "tiempo total", pose, ahora_ns);
    }
    transcurrido = ahora_ns - a->inicio_fase_ns;

    switch (a->fase) {
	//avanzar: va recto hasta que el sensor topa obstáculo
    case AUTO_AVANZAR:
        if (if_obstaculo(s->frontal_cm, s->frontal_ok, AUTO_DISTANCIA_OBSTACULO_CM)) {
            fin_de_fila(a, pose, ahora_ns, false);
        } else if (atascado(a, pose, ahora_ns)) {
            fin_de_fila(a, pose, ahora_ns, true);  // choco con algo que el sensor no vio 
        } else if (transcurrido >= AUTO_TIMEOUT_AVANZAR_NS) {
            ir_a_fin(a, "sin pared en 30 s", pose, ahora_ns);  // falla del sensor
        } else if (a->etapa != AUTO_ETAPA_ESQUINA_1
                   && fabs(normalizar(rumbo_de_fila(a) - pose->theta_rad))
                      > AUTO_RUMBO_MAX_RAD) {
            a->corrigiendo = true;             //se desvia: corrige y sigue la fila 
            a->giro_n = 2;
            parar_y_girar_a(a, rumbo_de_fila(a), pose, ahora_ns);
        }
        break;
	//parar: se detiene y prepara la sig maniobra
    case AUTO_PARAR:
        if (transcurrido >= AUTO_PAUSA_NS) {
            entrar_fase(a, a->fase_siguiente, pose, ahora_ns);
        }
        break;
	//gira hasta el rumbo objetivo. Si se pasa, el error cambia de signo y gira de vuelta (en giro fino)
    case AUTO_GIRAR: {
        double delta = normalizar(pose->theta_rad - a->theta_anterior_rad);
        double error = normalizar(a->rumbo_objetivo_rad - pose->theta_rad);
        int dir = error > 0.0 ? 1 : -1;
        // ya venia en giro fino y cruzo el objetivo: aceptar en vez de oscilar 
        bool cruzo = a->giro_fino && dir != a->giro_dir;
        // lo que gira en un tick (delta) pasaria del objetivo: parar ya 
        bool se_pasaria = delta * error > 0.0 && fabs(error) < 0.5 * fabs(delta);

        a->giro_acumulado_rad += fabs(delta);
        a->theta_anterior_rad = pose->theta_rad;
        a->giro_dir = dir;
        if (fabs(error) < AUTO_GIRO_FINO_RAD) a->giro_fino = true;

        if (fabs(error) <= AUTO_GIRO_TOL_RAD + AUTO_GIRO_ANTICIPO_RAD
                || cruzo || se_pasaria) {
            a->atascos = 0;
            parar_y_luego(a, a->giro_n == 1 ? AUTO_MOVER : AUTO_AVANZAR, pose, ahora_ns);
        } else if (atascado(a, pose, ahora_ns)) {
            retroceder_y_luego(a, AUTO_GIRAR, pose, ahora_ns);  // luego sigue al mismo rumbo 
        } else if (transcurrido >= AUTO_TIMEOUT_GIRO_NS) {
            ir_a_fin(a, "no logra girar", pose, ahora_ns);     // motores atascados 
        }
        break;
    }
	//Se mueve a la siguiente fila, es el movimiento "lateral" es de 12cm
    case AUTO_MOVER: {
        double recorrido = distancia_desde_inicio(a, pose);
        if (recorrido >= AUTO_PASO_MM) {
            fin_de_mover(a, pose, ahora_ns, false);
        } else if (if_obstaculo(s->frontal_cm, s->frontal_ok, AUTO_DISTANCIA_MOVER_CM)) {
            if (recorrido < AUTO_PASO_MM / 2.0) {
                ir_a_fin(a, "borde lateral", pose, ahora_ns);  // no cabe otra fila
            } else {
                fin_de_mover(a, pose, ahora_ns, false);       // paso parcial 
            }
        } else if (atascado(a, pose, ahora_ns)) {
            if (recorrido < AUTO_PASO_MM / 2.0) {
                ir_a_fin(a, "borde lateral (atasco)", pose, ahora_ns);
            } else {
                fin_de_mover(a, pose, ahora_ns, true);
            }
        } else if (transcurrido >= AUTO_TIMEOUT_MOVER_NS) {
            ir_a_fin(a, "mover sin terminar", pose, ahora_ns);
        }
        break;
    }
	//se aleja del atasco y sigue con la maniobra que quedo en fase_siguiente
	//Si el sensor trasero ve algo cerca, no sigue retrocediendo
    case AUTO_RETROCEDER:
        if (distancia_desde_inicio(a, pose) >= AUTO_RETROCESO_MM
                || if_obstaculo(s->trasero_cm, s->trasero_ok, AUTO_DISTANCIA_TRASERA_CM)
                || transcurrido >= AUTO_TIMEOUT_RETROCEDER_NS) {
            parar_y_luego(a, a->fase_siguiente, pose, ahora_ns);
        }
        break;
	// es para llegar al final y devuelve un stop
    case AUTO_FIN:
        break;
    }
    return orden_de_fase(a);
}
