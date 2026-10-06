/* Para probar este test en host: colocarse en raíz de repo: ~/Roomba-Disco
>>>cmake -S Servidor -B Servidor/build-sim -DROOMBATECA_HARDWARE=OFF
>>>cmake --build Servidor/build-sim --target test_auto logica_simulador
>>>ctest --test-dir Servidor/build-sim --output-on-failure -R auto

y debería decirnos que paso el test al 100%

Si queremos ver los prints, corremos el exe:
>>>./Servidor/build-sim/test_auto
*/

#include "auto.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_ 3.14159265358979323846
#define MS 1000000ULL
#define DT_MS 100
#define CELDA_MM 50.0
#define ANCHO_LIMPIEZA_MM 200.0   /* ancho que cubre el robot en la simulacion */
#define MAX_X 100
#define MAX_Y 100

/* Mundo simulado: habitacion rectangular [0,W]x[0,H] con una "caja" opcional. */
typedef struct {
    double w, h;
    int tiene_caja;
    double cx0, cy0, cx1, cy1;
} mundo_t;

static double rayo_hasta_pared(const mundo_t *m, double x, double y, double ang) {
    double dx = cos(ang), dy = sin(ang), t = 1e9, tt;
    if (dx > 1e-9) { tt = (m->w - x) / dx; if (tt < t) t = tt; }
    if (dx < -1e-9) { tt = (0.0 - x) / dx; if (tt < t) t = tt; }
    if (dy > 1e-9) { tt = (m->h - y) / dy; if (tt < t) t = tt; }
    if (dy < -1e-9) { tt = (0.0 - y) / dy; if (tt < t) t = tt; }
    if (m->tiene_caja) {            /* interseccion rayo - AABB (slabs) */
        double t0 = 0.0, t1 = 1e9, a, b, tmp;
        int ok = 1;
        if (fabs(dx) < 1e-9) { if (x < m->cx0 || x > m->cx1) ok = 0; }
        else { a = (m->cx0 - x) / dx; b = (m->cx1 - x) / dx;
               if (a > b) { tmp = a; a = b; b = tmp; }
               if (a > t0) { t0 = a; }
               if (b < t1) { t1 = b; } }
        if (fabs(dy) < 1e-9) { if (y < m->cy0 || y > m->cy1) ok = 0; }
        else { a = (m->cy0 - y) / dy; b = (m->cy1 - y) / dy;
               if (a > b) { tmp = a; a = b; b = tmp; }
               if (a > t0) { t0 = a; }
               if (b < t1) { t1 = b; } }
        if (ok && t0 <= t1 && t0 < t) t = t0;
    }
    return t;
}

static int en_caja(const mundo_t *m, double x, double y) {
    return m->tiene_caja && x >= m->cx0 && x <= m->cx1 && y >= m->cy0 && y <= m->cy1;
}

typedef struct { int pasos; double cobertura; int fin; int choco; } resultado_t;

static resultado_t simular(const mundo_t *m, double x0, double y0, double theta0, int imprimir) {
    auto_t a;
    odometria_pose_t pose = {x0, y0, theta0};
    unsigned char visitado[MAX_Y][MAX_X] = {{0}};
    uint64_t t = 1000ULL * MS;
    resultado_t r = {0, 0.0, 0, 0};
    int i, cx, cy;

    auto_init(&a);
    for (i = 0; i < 20000 && !auto_fin(&a); ++i) {
        double dist_mm = rayo_hasta_pared(m, pose.x_mm, pose.y_mm, pose.theta_rad);
        float cm = (float)(dist_mm / 10.0);
        int valida = cm >= 2.0f && cm <= 400.0f;
        auto_orden_t o = auto_paso(&a, &pose, cm, valida != 0, t += DT_MS * MS);
        double dt = DT_MS / 1000.0;
        double dx, dy;

        if (strcmp(o.direccion, "FWD") == 0) {
            double v = o.velocidad * 0.5;               /* 300 -> 150 mm/s */
            dx = v * dt * cos(pose.theta_rad); dy = v * dt * sin(pose.theta_rad);
            if (dist_mm - v * dt < 0.0 || en_caja(m, pose.x_mm + dx, pose.y_mm + dy)) r.choco = 1;
            pose.x_mm += dx; pose.y_mm += dy;
        } else if (strcmp(o.direccion, "TURN_L") == 0) {
            pose.theta_rad += o.velocidad * 0.004 * dt;  /* 250 -> 1 rad/s */
        } else if (strcmp(o.direccion, "TURN_R") == 0) {
            pose.theta_rad -= o.velocidad * 0.004 * dt;
        }
        while (pose.theta_rad > PI_) pose.theta_rad -= 2 * PI_;
        while (pose.theta_rad < -PI_) pose.theta_rad += 2 * PI_;

        for (cy = 0; cy < MAX_Y; ++cy) for (cx = 0; cx < MAX_X; ++cx) {
            double px = (cx + 0.5) * CELDA_MM, py = (cy + 0.5) * CELDA_MM;
            if (px > m->w || py > m->h || en_caja(m, px, py)) continue;
            if (fabs(px - pose.x_mm) <= ANCHO_LIMPIEZA_MM / 2 &&
                fabs(py - pose.y_mm) <= ANCHO_LIMPIEZA_MM / 2) visitado[cy][cx] = 1;
        }
    }
    {   int total = 0, vistas = 0;
        for (cy = 0; cy < MAX_Y; ++cy) for (cx = 0; cx < MAX_X; ++cx) {
            double px = (cx + 0.5) * CELDA_MM, py = (cy + 0.5) * CELDA_MM;
            if (px > m->w || py > m->h || en_caja(m, px, py)) continue;
            ++total; vistas += visitado[cy][cx];
        }
        r.cobertura = 100.0 * vistas / total;
    }
    r.pasos = i;
    r.fin = auto_fin(&a);
    if (imprimir)
        printf("  pasos=%d (%.0f s)  fin=%d  choque=%d  cobertura=%.1f%%\n",
               i, i * DT_MS / 1000.0, r.fin, r.choco, r.cobertura);
    return r;
}

static int es(auto_orden_t o, const char *dir) { return strcmp(o.direccion, dir) == 0; }

static void test_secuencia_basica(void) {
    /* Con BUSCAR_ESQUINA=1: pared -> giro der -> pared -> giro der -> 12 cm -> giro der. */
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    int i;

    auto_init(&a);
    o = auto_paso(&a, &pose, 150.0f, true, t);
    assert(es(o, "FWD"));
    assert(!o.aspirar);                                          /* esquina: sin aspirar */
    o = auto_paso(&a, &pose, 0.0f, false, t += 100 * MS);   /* sin eco = libre */
    assert(es(o, "FWD"));
    o = auto_paso(&a, &pose, 15.0f, true, t += 100 * MS);   /* pared 1 */
    assert(es(o, "STOP"));
    o = auto_paso(&a, &pose, 15.0f, true, t += 400 * MS);
    assert(es(o, "TURN_R"));
    for (i = 0; i < 40 && es(o, "TURN_R"); ++i) {
        pose.theta_rad -= 0.1;
        o = auto_paso(&a, &pose, 150.0f, true, t += 100 * MS);
    }
    assert(es(o, "STOP"));
    o = auto_paso(&a, &pose, 150.0f, true, t += 400 * MS);
    assert(es(o, "FWD"));                                        /* tramo 2 */
    assert(a.etapa == AUTO_ETAPA_ESQUINA_2);
    o = auto_paso(&a, &pose, 12.0f, true, t += 100 * MS);   /* pared 2: esquina */
    assert(es(o, "STOP"));
    assert(a.etapa == AUTO_ETAPA_BARRIDO && a.lado == -1);
    assert(o.aspirar);                                           /* barrido: aspirando */
}

/* Pasar a MANUAL a mitad del recorrido y volver a AUTO: logica.c llama a
 * auto_reset(), que debe dejar la maquina en el estado inicial. */
static void test_reinicio_a_mitad(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;

    auto_init(&a);
    auto_paso(&a, &pose, 150.0f, true, t);
    auto_paso(&a, &pose, 15.0f, true, t += 100 * MS);            /* pared 1 */
    auto_paso(&a, &pose, 15.0f, true, t += 400 * MS);            /* gira */
    a.etapa = AUTO_ETAPA_BARRIDO;                                /* simula estar barriendo */
    a.fase = AUTO_MOVER;
    a.lado = 1;

    auto_reset(&a);
    assert(a.fase == AUTO_AVANZAR && a.etapa == AUTO_ETAPA_ESQUINA_1 && a.lado == -1);
    assert(!auto_fin(&a));
    o = auto_paso(&a, &pose, 150.0f, true, t += 100 * MS);
    assert(es(o, "FWD") && !o.aspirar);

    a.fase = AUTO_FIN;                                           /* tambien desde FIN */
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
    int i, retrocesos = 0;

    auto_init(&a);
    auto_paso(&a, &pose, 150.0f, true, t);
    auto_paso(&a, &pose, 10.0f, true, t += 100 * MS);
    o = auto_paso(&a, &pose, 10.0f, true, t += 400 * MS);
    assert(es(o, "TURN_R"));
    for (i = 0; i < 200 && !auto_fin(&a); ++i) {
        o = auto_paso(&a, &pose, 150.0f, true, t += 100 * MS);
        if (es(o, "BACK") && a.inicio_fase_ns == t) ++retrocesos;
    }
    assert(auto_fin(&a));
    assert(es(o, "STOP"));
    assert(retrocesos == AUTO_ATASCOS_MAX);
}

/* Avanzando sin nada a la vista del ultrasonico pero con la pose quieta: choco
 * con algo que el sensor no ve. Retrocede y lo toma como fin de fila. */
static void test_atasco_avanzando(void) {
    auto_t a;
    odometria_pose_t pose = {0, 0, 0};
    uint64_t t = 1000ULL * MS;
    auto_orden_t o;
    int i;

    auto_init(&a);
    o = auto_paso(&a, &pose, 150.0f, true, t);
    for (i = 0; i < 20 && es(o, "FWD"); ++i)
        o = auto_paso(&a, &pose, 150.0f, true, t += 100 * MS);
    assert(es(o, "BACK"));
    assert(i <= 10);                                             /* gracia + una ventana */
    assert(a.etapa == AUTO_ETAPA_ESQUINA_2);                     /* cuenta como la pared 1 */
    for (i = 0; i < 10 && es(o, "BACK"); ++i) {
        pose.x_mm -= 10.0;
        o = auto_paso(&a, &pose, 150.0f, true, t += 100 * MS);
    }
    assert(es(o, "STOP"));
    o = auto_paso(&a, &pose, 150.0f, true, t += 400 * MS);
    assert(es(o, "TURN_R"));
}
//


int main(void) {
    mundo_t vacia = {3000.0, 2000.0, 0, 0, 0, 0, 0};
    mundo_t con_caja = {3000.0, 2000.0, 1, 1400.0, 700.0, 1900.0, 1200.0};
    resultado_t r;

    test_secuencia_basica();
    test_reinicio_a_mitad();
    test_atasco_giro();
    test_atasco_avanzando();
	//pasos son los ticks de 100ms que tardó la simulación
	//Se espera resultado: pasos=5697 (570 s)  fin=1  choque=0  cobertura=85.2%
    printf("Habitacion 3.0 x 2.0 m vacia, arranque en el centro:\n");
    r = simular(&vacia, 1500.0, 1000.0, 0.0, 1);
    assert(r.fin && !r.choco && r.cobertura > 85.0);
	//Se espera resultado: pasos=8065 (806 s)  fin=1  choque=0  cobertura=85.7%
    printf("Habitacion vacia, arranque cerca de una pared:\n");
    r = simular(&vacia, 600.0, 400.0, PI_ / 2, 1);
    assert(r.fin && !r.choco && r.cobertura > 85.0);
	//Se espera resultado:
    printf("Habitacion con una caja de 50 x 50 cm:\n");
    r = simular(&con_caja, 400.0, 400.0, 0.0, 1);
    assert(r.fin && !r.choco);

    printf("test_auto OK\n");
    return 0;
}
