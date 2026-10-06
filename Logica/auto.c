#include "auto.h"

#include <math.h>
#include <stddef.h>

#define PI 3.14159265358979323846

//revisa si hay obstaculo
static bool if_obstaculo(float distancia_cm, bool valida, float umbral_cm) {
    return valida && distancia_cm < umbral_cm;
}
//Es para que al girar se pasa del rango que estamos usando entonces 
//es para reajustar el numero y ver cuanto ya giró
static double normalizar(double angulo_rad) {
    while (angulo_rad > PI) angulo_rad -= 2.0 * PI;
    while (angulo_rad < -PI) angulo_rad += 2.0 * PI;
    return angulo_rad;
}
//para hacer mediciones, es una diferencia
static double distancia_desde_inicio(const auto_t *a, const odometria_pose_t *pose) {
    return hypot(pose->x_mm - a->x_inicio_mm, pose->y_mm - a->y_inicio_mm);
}
//para cambio de fase y prepararse para la nueva fase
//Fases: Auto_mover, auto_avanzar, auto_girar, auto_parar y auto_fin
static void entrar_fase(auto_t *a, auto_fase_t fase,
                        const odometria_pose_t *pose, uint64_t ahora_ns) {
    a->fase = fase;
    a->inicio_fase_ns = ahora_ns;
    if (fase == AUTO_AVANZAR || fase == AUTO_MOVER || fase == AUTO_RETROCEDER) {
        a->x_inicio_mm = pose->x_mm;
        a->y_inicio_mm = pose->y_mm;
    } else if (fase == AUTO_GIRAR) {
        if (!a->reanudar_giro) {
            a->giro_objetivo_rad = a->lado * AUTO_GIRO_RAD;
            a->giro_acumulado_rad = 0.0;
        }
        a->reanudar_giro = false;
        a->theta_anterior_rad = pose->theta_rad;
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
//señala que ya se terminó el recorrido y lo manda a manual
static void ir_a_fin(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns) {
    entrar_fase(a, AUTO_FIN, pose, ahora_ns);
}
//tras un atasco retrocede y luego sigue con 'siguiente'; si se traba muchas
//veces seguidas sin completar una maniobra, el robot no logra salir y termina
static void retroceder_y_luego(auto_t *a, auto_fase_t siguiente,
                               const odometria_pose_t *pose, uint64_t ahora_ns) {
    if (++a->atascos > AUTO_ATASCOS_MAX) {
        ir_a_fin(a, pose, ahora_ns);
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
            sin_avance = fabs(a->giro_acumulado_rad - a->atasco_ref_giro_rad)
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
        ir_a_fin(a, pose, ahora_ns);   /* fila vacia: ya no hay mas por barrer */
        return;
    }
    a->giro_n = 1;
    if (a->etapa == AUTO_ETAPA_ESQUINA_1) {
        a->patron_completo = false;    /* solo girar a la derecha */
        a->lado = -1;
        a->etapa = AUTO_ETAPA_ESQUINA_2;
    } else {
        a->patron_completo = true;
        if (a->etapa == AUTO_ETAPA_ESQUINA_2) {
            a->lado = -1;              /* en la esquina: primer barrido a la derecha */
            a->etapa = AUTO_ETAPA_BARRIDO;
        }
    }
    if (por_atasco) {
        retroceder_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
    } else {
        a->atascos = 0;
        parar_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
    }
}

static auto_orden_t orden_de_fase(const auto_t *a) {
    // La aspiradora se enciende solo cuando se hace el barrido, no en las otras fases
    const bool aspirar = a->etapa == AUTO_ETAPA_BARRIDO && a->fase != AUTO_FIN;

    switch (a->fase) {
    case AUTO_AVANZAR:
        return (auto_orden_t){"FWD", AUTO_VEL_AVANCE, aspirar};
    case AUTO_MOVER:
        return (auto_orden_t){"FWD", AUTO_VEL_MOVER, aspirar};
    case AUTO_RETROCEDER:
        return (auto_orden_t){"BACK", AUTO_VEL_RETROCEDER, aspirar};
    case AUTO_GIRAR:
        return (auto_orden_t){a->giro_objetivo_rad > 0.0 ? "TURN_L" : "TURN_R",
                                  AUTO_VEL_GIRO, aspirar};
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
    a->fase = AUTO_AVANZAR;
    a->fase_siguiente = AUTO_AVANZAR;
    a->etapa = AUTO_BUSCAR_ESQUINA ? AUTO_ETAPA_ESQUINA_1
                                       : AUTO_ETAPA_BARRIDO;
    a->lado = -1;                 //el primer giro es a la derecha
    a->giro_n = 1;
    a->patron_completo = true;
    a->inicio_fase_ns = 0;
    a->x_inicio_mm = a->y_inicio_mm = 0.0;
    a->theta_anterior_rad = 0.0;
    a->giro_acumulado_rad = 0.0;
    a->giro_objetivo_rad = 0.0;
    a->reanudar_giro = false;
    a->atascos = 0;
    a->atasco_ref_ns = 0;
    a->atasco_ref_x_mm = a->atasco_ref_y_mm = 0.0;
    a->atasco_ref_giro_rad = 0.0;
}
//es como un if auto_fin? devuelve true si está en fase auto_fin
bool auto_fin(const auto_t *a) {
    return a != NULL && a->fase == AUTO_FIN;
}
//ESte es el que manda las ordenes al robot dependiendo de su fase
auto_orden_t auto_paso(auto_t *a, const odometria_pose_t *pose,
                               float frontal_cm, bool frontal_ok,
                               uint64_t ahora_ns) {
    const auto_orden_t parar = {"STOP", 0, false};
    uint64_t transcurrido;

    if (a == NULL || pose == NULL) return parar;
    if (a->inicio_fase_ns == 0 && a->fase == AUTO_AVANZAR) {
        /* primer tick tras reset: fija el origen de la primera fila */
        entrar_fase(a, AUTO_AVANZAR, pose, ahora_ns);
    }
    transcurrido = ahora_ns - a->inicio_fase_ns;
	//avanzar: va rectohasta que el sensor topa obstáculo
    switch (a->fase) {
    case AUTO_AVANZAR:
        if (if_obstaculo(frontal_cm, frontal_ok, AUTO_DISTANCIA_OBSTACULO_CM)) {
            fin_de_fila(a, pose, ahora_ns, false);
        } else if (atascado(a, pose, ahora_ns)) {
            fin_de_fila(a, pose, ahora_ns, true);  /* choco con algo que el sensor no vio */
        } else if (transcurrido >= AUTO_TIMEOUT_AVANZAR_NS) {
            ir_a_fin(a, pose, ahora_ns);       /* sensor no detecto obstaculo en 30s: falla */
        }
        break;
	//parar: se detiene y prepara para girar(la mayoría del tiempo)
    case AUTO_PARAR:
        if (transcurrido >= AUTO_PAUSA_NS) {
            entrar_fase(a, a->fase_siguiente, pose, ahora_ns);
        }
        break;
	//decide hacia que lado girar para hacer el patrón y gira
    case AUTO_GIRAR:
        a->giro_acumulado_rad += normalizar(pose->theta_rad - a->theta_anterior_rad);
        a->theta_anterior_rad = pose->theta_rad;
        if (fabs(a->giro_acumulado_rad)
                >= fabs(a->giro_objetivo_rad) - AUTO_GIRO_ANTICIPO_RAD) {
            a->atascos = 0;
            if (a->patron_completo && a->giro_n == 1) {
                parar_y_luego(a, AUTO_MOVER, pose, ahora_ns);
            } else {
                if (a->patron_completo) a->lado = -a->lado;  /* la proxima vuelta, al reves */
                parar_y_luego(a, AUTO_AVANZAR, pose, ahora_ns);
            }
        } else if (atascado(a, pose, ahora_ns)) {
            a->reanudar_giro = true;           /* retrocede y completa lo que faltaba */
            retroceder_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
        } else if (transcurrido >= AUTO_TIMEOUT_GIRO_NS) {
            ir_a_fin(a, pose, ahora_ns);       /* no gira: motores atascados */
        }
        break;
	//Se mueve a la siguiente fila, es el movimiento "lateral" es de 12cm
    case AUTO_MOVER: {
        double recorrido = distancia_desde_inicio(a, pose);
        if (recorrido >= AUTO_PASO_MM) {
            a->atascos = 0;
            a->giro_n = 2;
            parar_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
        } else if (if_obstaculo(frontal_cm, frontal_ok, AUTO_DISTANCIA_MOVER_CM)) {
            if (recorrido < AUTO_PASO_MM / 2.0) {
                ir_a_fin(a, pose, ahora_ns);   /* no cabe la fila siguiente: borde alcanzado */
            } else {
                a->atascos = 0;
                a->giro_n = 2;                 /* paso parcial: sigue con el segundo giro */
                parar_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
            }
        } else if (atascado(a, pose, ahora_ns)) {
            if (recorrido < AUTO_PASO_MM / 2.0) {
                ir_a_fin(a, pose, ahora_ns);   /* igual que con el sensor: no cabe la fila */
            } else {
                a->giro_n = 2;
                retroceder_y_luego(a, AUTO_GIRAR, pose, ahora_ns);
            }
        } else if (transcurrido >= AUTO_TIMEOUT_MOVER_NS) {
            ir_a_fin(a, pose, ahora_ns);
        }
        break;
    }
	//se aleja del atasco y sigue con la maniobra que quedo en fase_siguiente
    case AUTO_RETROCEDER:
        if (distancia_desde_inicio(a, pose) >= AUTO_RETROCESO_MM
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
