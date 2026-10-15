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
//el movimiento lateral siempre va hacia el mismo lado dentro de una "S" (en la primera es a la derecha; en las siguientes se elige con el mapa)
static double rumbo_lateral(const auto_t *a) {
    return normalizar(a->rumbo_fila_rad + a->lateral * AUTO_GIRO_RAD);
}
//para cambio de fase y prepararse para la nueva fase
//Fases: auto_avanzar, auto_parar, auto_girar, auto_mover, auto_retroceder y auto_fin
static void entrar_fase(auto_t *a, auto_fase_t fase,
                        const odometria_pose_t *pose, uint64_t ahora_ns) {
    a->fase = fase;
    a->inicio_fase_ns = ahora_ns;
    if (fase == AUTO_AVANZAR && a->corrigiendo) {
        a->corrigiendo = false;      /* sigue la misma fila: conserva su inicio */
    } else if (fase == AUTO_AVANZAR || fase == AUTO_MOVER || fase == AUTO_RETROCEDER
               || fase == AUTO_IR) {
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
//la "S" actual ya no puede seguir. Con mapa busca otra zona; sin mapa termina
static void fin_de_bloque(auto_t *a, const char *motivo,
                          const odometria_pose_t *pose, uint64_t ahora_ns) {
    if (a->mapa != NULL) {
        a->atascos = 0;
        parar_y_luego(a, AUTO_PLANEAR, pose, ahora_ns);
    } else {
        ir_a_fin(a, motivo, pose, ahora_ns);
    }
}
//un atasco yendo hacia adelante: lo que el sensor no vio queda en el mapa
static void marcar_atasco(auto_t *a, const odometria_pose_t *pose) {
    if (a->mapa == NULL) return;
    mapa_marcar_obstaculo(a->mapa,
                          pose->x_mm + (AUTO_RADIO_ROBOT_MM + 50.0) * cos(pose->theta_rad),
                          pose->y_mm + (AUTO_RADIO_ROBOT_MM + 50.0) * sin(pose->theta_rad));
}

/* ======================================================================
 * Navegacion con el mapa
 * ====================================================================== */
#define CELDAS_MAX (MAPA_DIMENSION_MAXIMA * MAPA_DIMENSION_MAXIMA)
static int bfs_previa[CELDAS_MAX];   // de donde se llego a cada celda (-1 = sin visitar)
static int bfs_cola[CELDAS_MAX];

static bool es_piso_conocido(unsigned char v) {
    return v == MAPA_VISITADA || v == MAPA_LIBRE_OBSERVADA || v == MAPA_ASPIRADA;
}
//el centro del robot puede pasar por la celda: todo lo que cubre el robot
//(AUTO_RADIO_ROBOT_MM alrededor, redondeado a celdas) es piso conocido. Si cerca
//hay un obstaculo o algo desconocido (que suele ser pared), pasar lo haria rozar
static bool transitable(const mapa_t *m, int x, int y) {
    const int r = (int)floor(AUTO_RADIO_ROBOT_MM / m->config.resolution_mm);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (!es_piso_conocido(mapa_valor(m, x + dx, y + dy))) return false;
    return true;
}
static bool descartada(const auto_t *a, double x_mm, double y_mm) {
    for (int i = 0; i < a->n_descartes; ++i)
        if (hypot(x_mm - a->descartes_mm[i][0], y_mm - a->descartes_mm[i][1])
                < AUTO_DESCARTE_RADIO_MM) return true;
    return false;
}
static void descartar(auto_t *a, double x_mm, double y_mm) {
    int i = a->n_descartes < AUTO_DESCARTES_MAX ? a->n_descartes++ : AUTO_DESCARTES_MAX - 1;
    a->descartes_mm[i][0] = x_mm;
    a->descartes_mm[i][1] = y_mm;
}
//zona pendiente: piso sin aspirar y transitable (las celdas pegadas a la pared
//o a lo desconocido no las alcanza la boquilla de un robot redondo)
static bool es_pendiente(const auto_t *a, int x, int y) {
    unsigned char v = mapa_valor(a->mapa, x, y);
    double px, py;
    if (v != MAPA_VISITADA && v != MAPA_LIBRE_OBSERVADA) return false;
    if (!transitable(a->mapa, x, y)) return false;
    mapa_punto_de_celda(a->mapa, x, y, &px, &py);
    return !descartada(a, px, py);
}
//busqueda en anchura desde la celda del robot por celdas transitables.
//Con buscar_pendiente = true para en la primera zona pendiente (la mas cercana)
//y la devuelve en *fin; si no, para al llegar a *fin. Devuelve false si no hay camino
static bool bfs(const auto_t *a, int x0, int y0, bool buscar_pendiente, int *fin_x, int *fin_y) {
    const mapa_t *m = a->mapa;
    const int w = m->config.width, h = m->config.height;
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int ini = 0, n = 0;

    if (x0 < 0 || y0 < 0 || x0 >= w || y0 >= h || w * h > CELDAS_MAX) return false;
    for (int i = 0; i < w * h; ++i) bfs_previa[i] = -1;
    bfs_previa[y0 * w + x0] = y0 * w + x0;
    bfs_cola[n++] = y0 * w + x0;
    while (ini < n) {
        int c = bfs_cola[ini++], x = c % w, y = c / w;
        if (buscar_pendiente ? es_pendiente(a, x, y) : (x == *fin_x && y == *fin_y)) {
            *fin_x = x;
            *fin_y = y;
            return true;
        }
        for (int k = 0; k < 4; ++k) {
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || bfs_previa[ny * w + nx] >= 0) continue;
            if (!transitable(m, nx, ny)) continue;
            bfs_previa[ny * w + nx] = c;
            bfs_cola[n++] = ny * w + nx;
        }
    }
    return false;
}
//se puede ir en linea recta del robot al punto sin salir de lo transitable
static bool linea_libre(const auto_t *a, const odometria_pose_t *pose, double x_mm, double y_mm) {
    double d = hypot(x_mm - pose->x_mm, y_mm - pose->y_mm);
    int cx0, cy0;
    if (mapa_celda_de_punto(a->mapa, pose->x_mm, pose->y_mm, &cx0, &cy0) != 0) return false;
    for (double r = 0.0; r <= d; r += a->mapa->config.resolution_mm / 4.0) {
        int cx, cy;
        double f = d > 0.0 ? r / d : 0.0;
        if (mapa_celda_de_punto(a->mapa, pose->x_mm + (x_mm - pose->x_mm) * f,
                                pose->y_mm + (y_mm - pose->y_mm) * f, &cx, &cy) != 0) return false;
        if ((cx != cx0 || cy != cy0) && !transitable(a->mapa, cx, cy)) return false;
    }
    return true;
}
//cuenta zonas pendientes en un cuadro de ~1.5 m hacia el rumbo indicado
static int pendientes_hacia(const auto_t *a, const odometria_pose_t *pose, double rumbo_rad) {
    const double res = a->mapa->config.resolution_mm;
    int n = 0;
    for (double adelante = res; adelante <= 1500.0; adelante += res) {
        for (double lado = -750.0; lado <= 750.0; lado += res) {
            int cx, cy;
            double px = pose->x_mm + adelante * cos(rumbo_rad) - lado * sin(rumbo_rad);
            double py = pose->y_mm + adelante * sin(rumbo_rad) + lado * cos(rumbo_rad);
            unsigned char v;
            if (mapa_celda_de_punto(a->mapa, px, py, &cx, &cy) != 0) continue;
            v = mapa_valor(a->mapa, cx, cy);
            if (v == MAPA_VISITADA || v == MAPA_LIBRE_OBSERVADA) ++n;
        }
    }
    return n;
}
//llego a la zona pendiente: empieza otra "S" ahi, con las filas paralelas a las
//de la primera y hacia el lado donde queda mas por aspirar
static void iniciar_bloque(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns) {
    double der, izq;
    ++a->bloques;
    descartar(a, a->objetivo_x_mm, a->objetivo_y_mm);   // no volver a elegirla
    a->tiene_objetivo = false;
    a->etapa = AUTO_ETAPA_BARRIDO;
    a->sentido = pendientes_hacia(a, pose, a->rumbo_fila_rad + PI)
                 > pendientes_hacia(a, pose, a->rumbo_fila_rad) ? 1 : 0;
    der = pendientes_hacia(a, pose, a->rumbo_fila_rad - AUTO_GIRO_RAD);
    izq = pendientes_hacia(a, pose, a->rumbo_fila_rad + AUTO_GIRO_RAD);
    a->lateral = izq > der ? 1 : -1;
    a->giro_n = 2;
    parar_y_girar_a(a, rumbo_de_fila(a), pose, ahora_ns);
}
//decide a donde ir: elige la zona pendiente mas cercana y el siguiente tramo recto
static void planear(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns) {
    int rx, ry, ox, oy, c, w = a->mapa->config.width, mejor, ultimo;
    double mejor_x, mejor_y;

    if (a->bloques >= AUTO_BLOQUES_MAX) {
        ir_a_fin(a, "limite de bloques", pose, ahora_ns);
        return;
    }
    if (a->tiene_objetivo && a->fallos_objetivo >= AUTO_FALLOS_OBJETIVO_MAX) {
        descartar(a, a->objetivo_x_mm, a->objetivo_y_mm);   // no se logra llegar
        a->tiene_objetivo = false;
    }
    if (mapa_celda_de_punto(a->mapa, pose->x_mm, pose->y_mm, &rx, &ry) != 0) {
        ir_a_fin(a, "fuera del mapa", pose, ahora_ns);
        return;
    }
    if (!a->tiene_objetivo) {
        if (!bfs(a, rx, ry, true, &ox, &oy)) {
            ir_a_fin(a, "mapa completo", pose, ahora_ns);   // no queda nada alcanzable
            return;
        }
        mapa_punto_de_celda(a->mapa, ox, oy, &a->objetivo_x_mm, &a->objetivo_y_mm);
        a->tiene_objetivo = true;
        a->fallos_objetivo = 0;
    }
    if (hypot(a->objetivo_x_mm - pose->x_mm, a->objetivo_y_mm - pose->y_mm) <= AUTO_LLEGADA_MM) {
        iniciar_bloque(a, pose, ahora_ns);
        return;
    }
    if (mapa_celda_de_punto(a->mapa, a->objetivo_x_mm, a->objetivo_y_mm, &ox, &oy) != 0
            || !bfs(a, rx, ry, false, &ox, &oy)) {
        descartar(a, a->objetivo_x_mm, a->objetivo_y_mm);   // ya no hay camino: otra zona
        a->tiene_objetivo = false;
        return;                                             // se vuelve a planear el proximo tick
    }
    //se recorre el camino desde la zona hacia el robot. El primer punto al que se
    //llega en linea recta (sin salir de lo transitable) es el que mas adelanta
    mejor = -1;
    ultimo = -1;
    for (c = oy * w + ox; c != ry * w + rx; c = bfs_previa[c]) {
        double px, py;
        mapa_punto_de_celda(a->mapa, c % w, c / w, &px, &py);
        if (mejor < 0 && hypot(px - pose->x_mm, py - pose->y_mm) <= AUTO_TRAMO_MAX_MM
                && linea_libre(a, pose, px, py)) {
            mejor = c;
        }
        ultimo = c;              //al final queda la celda vecina al robot
    }
    if (mejor < 0) mejor = ultimo;
    if (mejor < 0) {             //la zona esta en la misma celda que el robot
        mejor_x = a->objetivo_x_mm;
        mejor_y = a->objetivo_y_mm;
    } else {
        mapa_punto_de_celda(a->mapa, mejor % w, mejor / w, &mejor_x, &mejor_y);
    }
    a->ir_x_mm = mejor_x;
    a->ir_y_mm = mejor_y;
    a->ir_distancia_mm = hypot(mejor_x - pose->x_mm, mejor_y - pose->y_mm);
    a->giro_n = 3;
    parar_y_girar_a(a, atan2(mejor_y - pose->y_mm, mejor_x - pose->x_mm), pose, ahora_ns);
}

//fin de fila, por el ultrasonico o por un atasco. Con atasco primero retrocede
//y no aplica la regla de fila vacia: trabarse al arrancar la fila no quiere
//decir que ya no quede por barrer
static void fin_de_fila(auto_t *a, const odometria_pose_t *pose, uint64_t ahora_ns,
                        bool por_atasco) {
    if (!por_atasco && a->etapa == AUTO_ETAPA_BARRIDO
            && distancia_desde_inicio(a, pose) < AUTO_FILA_MIN_MM) {
        fin_de_bloque(a, "fila vacia", pose, ahora_ns);   /* ya no hay mas por barrer */
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
        marcar_atasco(a, pose);
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
        marcar_atasco(a, pose);
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
    case AUTO_IR:
        return (auto_orden_t){"FWD", AUTO_VEL_IR, aspirar};
    case AUTO_GIRAR:
        return (auto_orden_t){a->giro_dir > 0 ? "TURN_L" : "TURN_R",
                              a->giro_fino ? AUTO_VEL_GIRO_FINO : AUTO_VEL_GIRO, aspirar};
    case AUTO_PARAR:
    case AUTO_PLANEAR:
    case AUTO_FIN:
    default:
        return (auto_orden_t){"STOP", 0, aspirar};
    }
}

void auto_init(auto_t *a) {
    if (a == NULL) return;
    a->mapa = NULL;               //sin mapa hasta que se llame auto_usar_mapa()
    auto_reset(a);
}
//Para poner los valores en su estado inicial
void auto_reset(auto_t *a) {
    mapa_t *mapa;
    if (a == NULL) return;
    mapa = a->mapa;               //el mapa se conserva entre recorridos
    *a = (auto_t){0};
    a->mapa = mapa;
    a->lateral = -1;              //el lateral de la primera "S" va a la derecha
    a->fase = AUTO_AVANZAR;
    a->fase_siguiente = AUTO_AVANZAR;
    a->etapa = AUTO_BUSCAR_ESQUINA ? AUTO_ETAPA_ESQUINA_1
                                   : AUTO_ETAPA_BARRIDO;
    a->giro_n = 1;
    a->giro_dir = -1;             //el primer giro es a la derecha
    a->motivo_fin = "";
}
void auto_usar_mapa(auto_t *a, mapa_t *mapa) {
    if (a != NULL) a->mapa = mapa;
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
        } else if (AUTO_CORREGIR_RUMBO && a->etapa != AUTO_ETAPA_ESQUINA_1
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
            parar_y_luego(a, a->giro_n == 1 ? AUTO_MOVER
                             : a->giro_n == 3 ? AUTO_IR : AUTO_AVANZAR, pose, ahora_ns);
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
                fin_de_bloque(a, "borde lateral", pose, ahora_ns);  // no cabe otra fila
            } else {
                fin_de_mover(a, pose, ahora_ns, false);       // paso parcial 
            }
        } else if (atascado(a, pose, ahora_ns)) {
            if (recorrido < AUTO_PASO_MM / 2.0) {
                marcar_atasco(a, pose);
                fin_de_bloque(a, "borde lateral (atasco)", pose, ahora_ns);
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
	//(con mapa) elige zona pendiente y tramo; o empieza otra "S"; o termina
    case AUTO_PLANEAR:
        planear(a, pose, ahora_ns);
        break;
	//(con mapa) recto hasta el final del tramo; si algo se cruza, vuelve a planear
    case AUTO_IR:
        if (distancia_desde_inicio(a, pose) >= a->ir_distancia_mm
                || hypot(a->ir_x_mm - pose->x_mm, a->ir_y_mm - pose->y_mm) < AUTO_LLEGADA_MM / 2.0) {
            parar_y_luego(a, AUTO_PLANEAR, pose, ahora_ns);
        } else if (if_obstaculo(s->frontal_cm, s->frontal_ok, AUTO_DISTANCIA_OBSTACULO_CM)) {
            ++a->fallos_objetivo;                // el obstaculo ya quedo en el mapa
            parar_y_luego(a, AUTO_PLANEAR, pose, ahora_ns);
        } else if (atascado(a, pose, ahora_ns)) {
            ++a->fallos_objetivo;
            marcar_atasco(a, pose);
            retroceder_y_luego(a, AUTO_PLANEAR, pose, ahora_ns);
        } else if (transcurrido >= AUTO_TIMEOUT_IR_NS) {
            ++a->fallos_objetivo;
            parar_y_luego(a, AUTO_PLANEAR, pose, ahora_ns);
        }
        break;
	// es para llegar al final y devuelve un stop
    case AUTO_FIN:
        break;
    }
    {
        auto_orden_t orden = orden_de_fase(a);
        //lo que pasa por la boquilla con la aspiradora encendida queda como aspirado
        if (a->mapa != NULL && orden.aspirar) {
            mapa_marcar_aspirada(a->mapa, pose, AUTO_ANCHO_BOQUILLA_MM);
        }
        return orden;
    }
}
