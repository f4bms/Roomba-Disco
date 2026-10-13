/* Pruebas del modo AUTO (auto.c).
 *
 * Para correr el test en host, desde la raiz del repo (~/Roomba-Disco):
 * >>>cmake -S Servidor -B Servidor/build-sim -DROOMBATECA_HARDWARE=OFF
 * >>>cmake --build Servidor/build-sim --target test_auto logica_simulador
 * >>>ctest --test-dir Servidor/build-sim --output-on-failure -R auto
 *
 * Para ver la tabla de cobertura y los prints, correr el ejecutable (igual desde (~/Roomba-Disco):
 * >>>./Servidor/build-sim/test_auto
 * Y para ver ademas el mapa de lo aspirado de cada escenario:
 * >>>./Servidor/build-sim/test_auto mapa
 *
 * Hay dos tipos de prueba:
 *  1. Unitarias: se le dan a auto_paso() diferentes poses y lecturas "a pata" 
 *     y se revisa la orden que devuelve (secuencia, reinicio, atascos, giros, etc.)
 *  2. Simulacion: un robot de 30 cm en una habitacion con "paredes y muebles",
 *     usando lo calculado de rampa de motores que roombateca_control.c, el retraso del
 *     sensor y el freno. Este mide la cobertura de la boquilla y que no choque.
 */

#include "auto.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_ 3.14159265358979323846
#define MS 1000000ULL
#define DT_MS 100                  /* tick de control, igual que CONTROL_TICK_MS */

/* ------------------------------------------------------------------------
 * Fisica del robot simulado: Luego CALIBRAR con el real: si es que hay cambios en las velocidades
 * de auto.h o el robot, revisar que test_frenado() siga dando la dist que se ve en el piso (~5 cm).
 * ------------------------------------------------------------------------ */
#define SIM_MM_S_VEL_1000   500.0  // ~CALIBRAR~: mm/s de cada rueda con velocidad 1000 
#define SIM_DIST_RUEDAS_MM  220.0  // igual que distancia_ruedas_mm en logica.c 
#define SIM_RADIO_MM        150.0  // robot de ~30 cm de diametro 
#define SIM_BOQUILLA_MM     120.0  // ancho de la boquilla (centrada) 
#define SIM_CONO_RAD        0.26   // HC-SR04: ~15 grados a cada lado 
#define SIM_RAMPA           0.20   // roombateca_control.c: RAMPA_PASO = 20 de 100 por tick 
#define SIM_RESPUESTA       0.8    // que tanto se acerca la velocidad real a la pedida en un tick 
#define SIM_FRENO           0.2    // STOP con freno dinamico: queda este tanto de la velocidad 
#define SIM_RETRASO_TICKS   2      // ~CALIBRAR~: ticks de atraso de la lectura del ultrasonico 
#define SIM_FACTOR_GIRO     0.35   // ~CALIBRAR~: girando sobre el eje las ruedas patinan y rinden menos 
#define SIM_MAX_TICKS       18000  // 30 min, igual que AUTO_TIMEOUT_TOTAL_NS 

#define CELDA_MM 20.0
#define MAX_CELDAS 250             // cuarto de  5x5m 

/* ======================================================================
 * Ambiente: cuarto rectangular [0,w] x [0,h] con algunas cajas
 * ====================================================================== */
typedef struct { double x0, y0, x1, y1; } caja_t;

typedef struct {
    const char *nombre;
    double w, h;
    int n_cajas;
    caja_t cajas[4];
    double x0, y0, theta0;       /* pose de arranque */
    double cobertura_min;        /* lo que se exige en el assert (%) */
} mundo_t;

//* distancia desde (x,y) siguiendo el angulo hasta la primera pared o caja (obstáculo)
static double rayo(const mundo_t *m, double x, double y, double ang) {
    double dx = cos(ang), dy = sin(ang), t = 1e9, tt;
    int i;

    if (dx > 1e-9)  { tt = (m->w - x) / dx; if (tt < t) t = tt; }
    if (dx < -1e-9) { tt = -x / dx;         if (tt < t) t = tt; }
    if (dy > 1e-9)  { tt = (m->h - y) / dy; if (tt < t) t = tt; }
    if (dy < -1e-9) { tt = -y / dy;         if (tt < t) t = tt; }
    for (i = 0; i < m->n_cajas; ++i) {   // interseccion rayo - caja
        const caja_t *c = &m->cajas[i];
        double t0 = 0.0, t1 = 1e9, a, b, tmp;
        int ok = 1;
        if (fabs(dx) < 1e-9) { if (x < c->x0 || x > c->x1) ok = 0; }
        else {
            a = (c->x0 - x) / dx; b = (c->x1 - x) / dx;
            if (a > b) { tmp = a; a = b; b = tmp; }
            if (a > t0) t0 = a;
            if (b < t1) t1 = b;
        }
        if (fabs(dy) < 1e-9) { if (y < c->y0 || y > c->y1) ok = 0; }
        else {
            a = (c->y0 - y) / dy; b = (c->y1 - y) / dy;
            if (a > b) { tmp = a; a = b; b = tmp; }
            if (a > t0) t0 = a;
            if (b < t1) t1 = b;
        }
        if (ok && t0 <= t1 && t0 < t) t = t0;
    }
    return t;
}

// lectura del sensor en el borde (cono de +-15°) 
static float sensor_cm(const mundo_t *m, const odometria_pose_t *p, double ang_sensor, bool *ok) {
    double ang = p->theta_rad + ang_sensor, d = 1e9, a;
    double sx = p->x_mm + SIM_RADIO_MM * cos(ang), sy = p->y_mm + SIM_RADIO_MM * sin(ang);
    for (a = -SIM_CONO_RAD; a <= SIM_CONO_RAD + 1e-9; a += SIM_CONO_RAD / 4) {
        double r = rayo(m, sx, sy, ang + a);
        if (r < d) d = r;
    }
    *ok = d >= 20.0 && d <= 4000.0;      // rango util del HC-SR04: 2 a 400 cm 
    return (float)(d / 10.0);
}

// true si el cuerpo del robot (circulo) se mete en una pared o caja
static int choca(const mundo_t *m, double x, double y) {
    int i;
    if (x < SIM_RADIO_MM || y < SIM_RADIO_MM
            || x > m->w - SIM_RADIO_MM || y > m->h - SIM_RADIO_MM) return 1;
    for (i = 0; i < m->n_cajas; ++i) {
        const caja_t *c = &m->cajas[i];
        double qx = fmax(c->x0, fmin(x, c->x1)), qy = fmax(c->y0, fmin(y, c->y1));
        if (hypot(qx - x, qy - y) < SIM_RADIO_MM) return 1;
    }
    return 0;
}

static int es_piso(const mundo_t *m, double x, double y) {
    int i;
    if (x < 0 || y < 0 || x > m->w || y > m->h) return 0;
    for (i = 0; i < m->n_cajas; ++i) {
        const caja_t *c = &m->cajas[i];
        if (x >= c->x0 && x <= c->x1 && y >= c->y0 && y <= c->y1) return 0;
    }
    return 1;
}

/* ======================================================================
 * Roomba-disco simulado: misma rampa que roombateca_set_motion() y freno al STOP
 * ====================================================================== */
typedef struct {
    odometria_pose_t pose;       // pose real (y la de la odometria: encoders sin error) 
    const char *orden_previa;
    double base;                 // rampa: fraccion de la velocidad pedida, 0..1 
    double v_mm_s, w_rad_s;      // velocidades reales 
    int contactos;               // ticks en que el cuerpo choco 
} robot_t;

static void robot_paso(robot_t *r, const mundo_t *m, auto_orden_t o) {
    double dt = DT_MS / 1000.0, v_obj = 0.0, w_obj = 0.0, ruedas;
    double nx, ny;

    if (strcmp(o.direccion, r->orden_previa) != 0) r->base = 0.0;  // cambio: rampa de cero 
    r->orden_previa = o.direccion;
    if (strcmp(o.direccion, "STOP") == 0 || o.velocidad == 0) {
        r->v_mm_s *= SIM_FRENO;
        r->w_rad_s *= SIM_FRENO;
    } else {
        r->base = r->base + SIM_RAMPA < 1.0 ? r->base + SIM_RAMPA : 1.0;
        ruedas = SIM_MM_S_VEL_1000 * o.velocidad / 1000.0 * r->base;
        if (strcmp(o.direccion, "FWD") == 0) v_obj = ruedas;
        else if (strcmp(o.direccion, "BACK") == 0) v_obj = -ruedas;
        else if (strcmp(o.direccion, "TURN_L") == 0) w_obj = SIM_FACTOR_GIRO * 2.0 * ruedas / SIM_DIST_RUEDAS_MM;
        else if (strcmp(o.direccion, "TURN_R") == 0) w_obj = -SIM_FACTOR_GIRO * 2.0 * ruedas / SIM_DIST_RUEDAS_MM;
        r->v_mm_s += (v_obj - r->v_mm_s) * SIM_RESPUESTA;
        r->w_rad_s += (w_obj - r->w_rad_s) * SIM_RESPUESTA;
    }
    nx = r->pose.x_mm + r->v_mm_s * dt * cos(r->pose.theta_rad);
    ny = r->pose.y_mm + r->v_mm_s * dt * sin(r->pose.theta_rad);
    if (choca(m, nx, ny)) {
        ++r->contactos;            // pegado: las ruedas no avanzan (encoders quietos) 
        r->v_mm_s = 0.0;
    } else {
        r->pose.x_mm = nx;
        r->pose.y_mm = ny;
    }
    r->pose.theta_rad += r->w_rad_s * dt;
    while (r->pose.theta_rad > PI_) r->pose.theta_rad -= 2 * PI_;
    while (r->pose.theta_rad < -PI_) r->pose.theta_rad += 2 * PI_;
}

// Un recorrido en marcha: robot + maquina de estados + poses pasadas (el sensor mide con SIM_RETRASO_TICKS de atraso)
typedef struct {
    const mundo_t *mundo;
    robot_t robot;
    auto_t a;
    odometria_pose_t historial[SIM_RETRASO_TICKS + 1];
    uint64_t t;
} sim_t;

static void sim_init(sim_t *s, const mundo_t *m) {
    int k;
    memset(s, 0, sizeof(*s));
    s->mundo = m;
    s->robot.pose = (odometria_pose_t){m->x0, m->y0, m->theta0};
    s->robot.orden_previa = "STOP";
    for (k = 0; k <= SIM_RETRASO_TICKS; ++k) s->historial[k] = s->robot.pose;
    s->t = 1000ULL * MS;
    auto_init(&s->a);
}

// un tick de control: medir, decidir (auto_paso) y mover 
static auto_orden_t sim_tick(sim_t *s) {
    auto_sensores_t lect;
    auto_orden_t o;
    int k;

    for (k = SIM_RETRASO_TICKS; k > 0; --k) s->historial[k] = s->historial[k - 1];
    s->historial[0] = s->robot.pose;
    lect.frontal_cm = sensor_cm(s->mundo, &s->historial[SIM_RETRASO_TICKS], 0.0, &lect.frontal_ok);
    lect.trasero_cm = sensor_cm(s->mundo, &s->historial[SIM_RETRASO_TICKS], PI_, &lect.trasero_ok);
    o = auto_paso(&s->a, &s->robot.pose, &lect, s->t += DT_MS * MS);
    robot_paso(&s->robot, s->mundo, o);
    return o;
}

/* ======================================================================
 * Simulacion completa de un recorrido y medicion de cobertura
 * ====================================================================== */
static unsigned char aspirado[MAX_CELDAS][MAX_CELDAS];

// marca la franja de la boquilla (perpendicular al rumbo) 
static void marcar_boquilla(double x, double y, double theta) {
    double k;
    for (k = -SIM_BOQUILLA_MM / 2; k <= SIM_BOQUILLA_MM / 2; k += CELDA_MM / 2) {
        int cx = (int)((x - k * sin(theta)) / CELDA_MM);
        int cy = (int)((y + k * cos(theta)) / CELDA_MM);
        if (cx >= 0 && cy >= 0 && cx < MAX_CELDAS && cy < MAX_CELDAS) aspirado[cy][cx] = 1;
    }
}

/* piso que la boquilla puede alcanzar sin que el cuerpo choque (las esquinas
 * y el borde pegado a la pared no se puede alcanzar físicamente) */
static int alcanzable(const mundo_t *m, double px, double py) {
    double k;
    for (k = -SIM_BOQUILLA_MM / 2; k <= SIM_BOQUILLA_MM / 2; k += CELDA_MM)
        if (!choca(m, px + k, py) || !choca(m, px, py + k)) return 1;
    return 0;
}

typedef struct {
    double cobertura;      // % del piso alcanzable que se aspiro 
    double segundos;
    int fin;
    int contactos;
    const char *motivo;
} resultado_t;

static resultado_t simular(const mundo_t *m, int imprimir_mapa) {
    static sim_t s;
    resultado_t res;
    int i, cx, cy, total = 0, vistas = 0;

    memset(aspirado, 0, sizeof(aspirado));
    sim_init(&s, m);
    for (i = 0; i < SIM_MAX_TICKS && !auto_fin(&s.a); ++i) {
        double x0 = s.robot.pose.x_mm, y0 = s.robot.pose.y_mm, th0 = s.robot.pose.theta_rad;
        auto_orden_t o = sim_tick(&s);
        int k;
        if (o.aspirar) {
            for (k = 1; k <= 5; ++k) {       // interpolar entre ticks 
                double f = k / 5.0;
                marcar_boquilla(x0 + (s.robot.pose.x_mm - x0) * f,
                                y0 + (s.robot.pose.y_mm - y0) * f,
                                th0 + (s.robot.pose.theta_rad - th0) * f);
            }
        }
    }
    for (cy = 0; cy < MAX_CELDAS; ++cy) {
        for (cx = 0; cx < MAX_CELDAS; ++cx) {
            double px = (cx + 0.5) * CELDA_MM, py = (cy + 0.5) * CELDA_MM;
            if (!es_piso(m, px, py) || !alcanzable(m, px, py)) continue;
            ++total;
            vistas += aspirado[cy][cx];
        }
    }
    res.cobertura = 100.0 * vistas / total;
    res.segundos = i * DT_MS / 1000.0;
    res.fin = auto_fin(&s.a);
    res.contactos = s.robot.contactos;
    res.motivo = s.a.motivo_fin;

    if (imprimir_mapa) {      /* '.' aspirado, 'o' sin aspirar, '#' mueble (1 caracter = 4x6 cm) */
        for (cy = (int)(m->h / CELDA_MM) - 1; cy >= 0; cy -= 3) {
            for (cx = 0; cx < (int)(m->w / CELDA_MM); cx += 2) {
                double px = (cx + 0.5) * CELDA_MM, py = (cy + 0.5) * CELDA_MM;
                putchar(!es_piso(m, px, py) ? '#' : aspirado[cy][cx] ? '.' : 'o');
            }
            putchar('\n');
        }
    }
    return res;
}

/* ======================================================================
 * Pruebas unitarias
 * ====================================================================== */
static int es(auto_orden_t o, const char *dir) { return strcmp(o.direccion, dir) == 0; }

static auto_sensores_t sens(float frontal_cm, bool frontal_ok) {
    auto_sensores_t s = {frontal_cm, frontal_ok, 150.0f, true};
    return s;
}

// gira la pose escrita a mano mientras la orden sea de giro; devuelve la orden final 
static auto_orden_t girar(auto_t *a, odometria_pose_t *pose, uint64_t *t, double paso_rad) {
    auto_orden_t o = auto_paso(a, pose, &(auto_sensores_t){150.0f, true, 150.0f, true}, *t += 100 * MS);
    int i;
    for (i = 0; i < 60 && (es(o, "TURN_R") || es(o, "TURN_L")); ++i) {
        pose->theta_rad += es(o, "TURN_L") ? paso_rad : -paso_rad;
        o = auto_paso(a, pose, &(auto_sensores_t){150.0f, true, 150.0f, true}, *t += 100 * MS);
    }
    return o;
}

static void test_secuencia_basica(void) {
    //* Con BUSCAR_ESQUINA=1: pared -> giro der -> pared (aspirando) -> giro der -> 12 cm -> giro der. 
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true);

    auto_init(&a);
    o = auto_paso(&a, &pose, &libre, t);
    assert(es(o, "FWD") && !o.aspirar);                  // Tramo 1: sin aspirar 
    o = auto_paso(&a, &pose, &(auto_sensores_t){0.0f, false, 0.0f, false}, t += 100 * MS);
    assert(es(o, "FWD"));                                // sin eco = libre 
    o = auto_paso(&a, &pose, &(auto_sensores_t){15.0f, true, 150.0f, true}, t += 100 * MS);
    assert(es(o, "STOP"));                               // Pared 1 
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "TURN_R"));
    o = girar(&a, &pose, &t, 0.1);
    assert(es(o, "STOP"));
    assert(fabs(pose.theta_rad + PI_ / 2) < 0.1);        // ~ -90° 
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "FWD") && o.aspirar);                   // tramo 2: pegado a la pared 1, aspira 
    assert(a.etapa == AUTO_ETAPA_ESQUINA_2);
    o = auto_paso(&a, &pose, &(auto_sensores_t){12.0f, true, 150.0f, true}, t += 100 * MS);
    assert(es(o, "STOP"));                               // pared 2: esquina 
    assert(a.etapa == AUTO_ETAPA_BARRIDO);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "TURN_R"));                             // hacia el lateral (-180) 
    o = girar(&a, &pose, &t, 0.1);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "FWD") && a.fase == AUTO_MOVER);
    pose.x_mm -= 130.0;                                  //13 cm de lateral 
    o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "STOP") && a.sentido == 1);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "TURN_R"));                             // fila de vuelta: +90 
}

/* Pasar a MANUAL a mitad del recorrido y volver a AUTO: logica.c llama a
 * auto_reset(), que debe dejar la maquina en el estado inicial. */
static void test_reinicio_a_mitad(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true), pared = sens(15.0f, true);

    auto_init(&a);
    auto_paso(&a, &pose, &libre, t);
    auto_paso(&a, &pose, &pared, t += 100 * MS);
    auto_paso(&a, &pose, &pared, t += 400 * MS);
    a.etapa = AUTO_ETAPA_BARRIDO;                        // simula estar barriendo 
    a.fase = AUTO_MOVER;
    a.sentido = 1;

    auto_reset(&a);
    assert(a.fase == AUTO_AVANZAR && a.etapa == AUTO_ETAPA_ESQUINA_1 && a.sentido == 0);
    assert(!auto_fin(&a));
    o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "FWD") && !o.aspirar);

    a.fase = AUTO_FIN;                                           // tambien desde FIN 
    auto_reset(&a);
    assert(!auto_fin(&a));
}

/* Giro con la odometria congelada (rueda trabada): retrocede y reintenta
 * AUTO_ATASCOS_MAX veces; si nunca logra girar, termina. */
static void test_atasco_giro(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true), pared = sens(10.0f, true);
    int i, retrocesos = 0;

    auto_init(&a);
    auto_paso(&a, &pose, &libre, t);
    auto_paso(&a, &pose, &pared, t += 100 * MS);
    o = auto_paso(&a, &pose, &pared, t += 400 * MS);
    assert(es(o, "TURN_R"));
    for (i = 0; i < 200 && !auto_fin(&a); ++i) {
        o = auto_paso(&a, &pose, &libre, t += 100 * MS);
        if (es(o, "BACK") && a.inicio_fase_ns == t) ++retrocesos;
    }
    assert(auto_fin(&a));
    assert(es(o, "STOP"));
    assert(retrocesos == AUTO_ATASCOS_MAX);
    assert(strcmp(a.motivo_fin, "atascos seguidos") == 0);
}

/* Avanzando sin nada a la vista del ultrasonico pero con la pose quieta: choco
 * con algo que el sensor no ve. Retrocede y lo toma como fin de fila. */
static void test_atasco_avanzando(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true);
    int i;

    auto_init(&a);
    o = auto_paso(&a, &pose, &libre, t);
    for (i = 0; i < 20 && es(o, "FWD"); ++i)
        o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "BACK"));
    assert(i <= 10);                                     /* gracia + una ventana */
    assert(a.etapa == AUTO_ETAPA_ESQUINA_2);             /* cuenta como la pared 1 */
    for (i = 0; i < 10 && es(o, "BACK"); ++i) {
        pose.x_mm -= 10.0;
        o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    }
    assert(es(o, "STOP"));
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "TURN_R"));
}

// Retrocediendo con algo atras: el sensor trasero corta el retroceso. 
static void test_retroceso_con_sensor_trasero(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true);
    int i;

    auto_init(&a);
    o = auto_paso(&a, &pose, &libre, t);
    for (i = 0; i < 20 && es(o, "FWD"); ++i)
        o = auto_paso(&a, &pose, &libre, t += 100 * MS);   /* atasco -> BACK */
    assert(es(o, "BACK"));
    o = auto_paso(&a, &pose, &(auto_sensores_t){150.0f, true, 8.0f, true}, t += 100 * MS);
    assert(es(o, "STOP"));                               /* no retrocede contra lo de atras */
}

// Giro que se pasa (inercia): debe volver hacia el objetivo en vez de seguir. 
static void test_giro_corrige_sobregiro(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true);

    auto_init(&a);
    auto_paso(&a, &pose, &libre, t);
    auto_paso(&a, &pose, &(auto_sensores_t){15.0f, true, 150.0f, true}, t += 100 * MS);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "TURN_R"));
    pose.theta_rad = -1.0;                               /* gira rapido... */
    o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "TURN_R"));
    pose.theta_rad = -1.85;                              /* ...y se pasa ~16 grados */
    o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "TURN_L") && o.velocidad == AUTO_VEL_GIRO_FINO);   /* vuelve, despacio */
    assert(a.fase == AUTO_GIRAR);
    pose.theta_rad = -1.55;                              /* vuelve y cruza de nuevo, ya cerca */
    o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "STOP"));                               /* lo da por bueno, no oscila */
}

/* Fila que se desvia de su rumbo: para, corrige y sigue la misma fila sin
 * perder el punto de inicio. */
static void test_corrige_rumbo_en_fila(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    auto_sensores_t libre = sens(150.0f, true);
    double x_inicio;

    auto_init(&a);
    auto_paso(&a, &pose, &libre, t);
    auto_paso(&a, &pose, &(auto_sensores_t){15.0f, true, 150.0f, true}, t += 100 * MS);
    auto_paso(&a, &pose, &libre, t += 400 * MS);
    o = girar(&a, &pose, &t, 0.1);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "FWD") && a.etapa == AUTO_ETAPA_ESQUINA_2);
    x_inicio = a.y_inicio_mm;
    pose.y_mm -= 300.0;
    pose.theta_rad += 0.2;                               /* se desvio ~11 grados */
    o = auto_paso(&a, &pose, &libre, t += 100 * MS);
    assert(es(o, "STOP") && a.corrigiendo);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "TURN_R"));                             /* corrige hacia la derecha */
    o = girar(&a, &pose, &t, 0.05);
    o = auto_paso(&a, &pose, &libre, t += 400 * MS);
    assert(es(o, "FWD") && a.fase == AUTO_AVANZAR);
    assert(a.y_inicio_mm == x_inicio);                   /* es la misma fila */
}

/* El recorrido entero tiene un limite de tiempo. */
static void test_tiempo_total(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;

    auto_init(&a);
    auto_paso(&a, &pose, &(auto_sensores_t){150.0f, true, 150.0f, true}, t);
    auto_paso(&a, &pose, &(auto_sensores_t){150.0f, true, 150.0f, true}, t + AUTO_TIMEOUT_TOTAL_NS);
    assert(auto_fin(&a) && strcmp(a.motivo_fin, "tiempo total") == 0);
}

/* ======================================================================
 * Pruebas con la simulacion
 * ====================================================================== */

/* Calibracion del simulador: con el umbral de 20 cm el robot real frena a ~5 cm.
 * Si esta prueba falla tras cambiar velocidades, recalibrar SIM_* o el umbral. */
static void test_frenado(void) {
    mundo_t pasillo = {"pasillo", 3000.0, 1000.0, 0, {{0, 0, 0, 0}}, 500.0, 500.0, 0.0, 0.0};
    static sim_t s;
    double hueco_cm;
    int i;

    sim_init(&s, &pasillo);
    for (i = 0; i < 200 && s.a.fase != AUTO_GIRAR; ++i) sim_tick(&s);
    hueco_cm = (pasillo.w - SIM_RADIO_MM - s.robot.pose.x_mm) / 10.0;
    printf("Frenado a velocidad %d: queda a %.1f cm de la pared (robot real: ~5 cm)\n",
           AUTO_VEL_AVANCE, hueco_cm);
    assert(s.robot.contactos == 0);
    assert(hueco_cm > 2.0 && hueco_cm < 9.0);
}

/* Con la fisica del simulador, los giros del barrido deben quedar cerca de 90
 * grados (como en el robot real). Se miden los primeros giros en una sala grande. */
static void test_giros_simulados(void) {
    mundo_t sala = {"sala", 4000.0, 4000.0, 0, {{0, 0, 0, 0}}, 2000.0, 600.0, 0.0, 0.0};
    static sim_t s;
    const char *previa = "STOP";
    double peor = 0.0;
    int i, giros = 0;

    sim_init(&s, &sala);
    for (i = 0; i < 3000 && giros < 6; ++i) {
        auto_orden_t o = sim_tick(&s);
        if (strncmp(previa, "TURN", 4) == 0 && strncmp(o.direccion, "TURN", 4) != 0
                && s.a.fase == AUTO_PARAR) {
            int k;
            double error;
            for (k = 0; k < 3; ++k) sim_tick(&s);           /* que termine de frenar */
            error = fabs(atan2(sin(s.robot.pose.theta_rad - s.a.rumbo_objetivo_rad),
                               cos(s.robot.pose.theta_rad - s.a.rumbo_objetivo_rad)));
            if (error > peor) peor = error;
            ++giros;
            o = (auto_orden_t){"STOP", 0, false};
        }
        previa = o.direccion;
    }
    printf("Giros simulados: %d, peor error %.1f grados\n", giros, peor * 180.0 / PI_);
    assert(giros == 6);
    assert(peor < 0.1);                                  /* < ~6 grados */
}

int main(int argc, char **argv) {
    /* La ultima columna es la cobertura minima exigida. Con muebles es baja a
     * proposito: la S se corta en el mueble y hoy termina ("borde lateral").
     * Es lo que se va a mejorar usando el mapa (mapa.c); al hacerlo, subir estos
     * minimos. */
    static const mundo_t mundos[] = {
        {"vacia 3x2 m, desde el centro", 3000, 2000, 0, {{0, 0, 0, 0}}, 1500, 1000, 0.0, 80.0},
        {"vacia 3x2 m, junto a pared",  3000, 2000, 0, {{0, 0, 0, 0}}, 600, 400, PI_ / 2, 80.0},
        {"caja 50x50 en medio",         3000, 2000, 1, {{1400, 700, 1900, 1200}}, 400, 400, 0.0, 20.0},
        {"sala 4x3 m con 3 muebles",    4000, 3000, 3, {{1500, 1200, 2300, 1800},
                                                        {0, 2400, 600, 3000},
                                                        {3200, 0, 4000, 500}}, 800, 800, 0.3, 5.0},
        {"cuarto 3.5x3 m cama+escritorio", 3500, 3000, 2, {{0, 0, 1400, 2000},
                                                           {2700, 2400, 3500, 3000}}, 2200, 1200, 1.2, 55.0},
    };
    const int n = (int)(sizeof(mundos) / sizeof(mundos[0]));
    int mapa = argc > 1 && strcmp(argv[1], "mapa") == 0;
    int i;

    test_secuencia_basica();
    test_reinicio_a_mitad();
    test_atasco_giro();
    test_atasco_avanzando();
    test_retroceso_con_sensor_trasero();
    test_giro_corrige_sobregiro();
    test_corrige_rumbo_en_fila();
    test_tiempo_total();
    test_frenado();
    test_giros_simulados();

    /* Tabla de cobertura. "contactos" = ticks en que el cuerpo toco algo que el
     * sensor no vio (esquinas de muebles al costado); se resuelve como atasco. */
    printf("\n%-32s %9s %8s %10s  %s\n", "Escenario", "cobertura", "tiempo", "contactos", "motivo de fin");
    for (i = 0; i < n; ++i) {
        resultado_t r = simular(&mundos[i], 0);
        printf("%-32s %8.1f%% %7.0fs %10d  %s\n", mundos[i].nombre, r.cobertura,
               r.segundos, r.contactos, r.motivo);
        assert(r.fin);                                   /* siempre termina */
        assert(r.cobertura >= mundos[i].cobertura_min);
        if (mundos[i].n_cajas == 0) assert(r.contactos == 0);   /* sala vacia: nunca toca */
        if (mapa) { simular(&mundos[i], 1); putchar('\n'); }
    }
    printf("test_auto OK\n");
    return 0;
}
