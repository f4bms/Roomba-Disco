#ifndef AUTO_H
#define AUTO_H

#include <stdbool.h>
#include <stdint.h>

#include "mapa.h"
#include "odometria.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Patron de barrido en "S" (boustrophedon):
 *
 *   fila -> obstaculo -> giro 90 (lado) -> mover PASO -> giro 90 (lado)
 *   -> fila de vuelta -> obstaculo -> mismo patron con el lado contrario formando una S
 *
 * Con AUTO_BUSCAR_ESQUINA = 1 el robot primero va a una esquina en donde busca:
 * topar pared, giro a la derecha y topar pared. De esta manera el barrido cubre toda la
 * habitacion aunque arranque en el centro. Con 0 empieza a barrer sin buscar la esquina
 * Cada "S" termina cuando el movimiento "lateral" ya no cabe (llega al borde). Con el
 * mapa busca otra zona sin aspirar (ver "Navegacion con el mapa"); sin mapa ahi termina
 *
 * Rumbo absoluto: el rumbo del tramo 2 de la esquina (o el del arranque si no se busca esquina) 
 * queda como referencia "rumbo_fila". Todas las filas van a ir a rumbo_fila o a rumbo_fila + 180 grados
 * y el movimiento lateral siempre a rumbo_fila - 90 grados. Cada giro apunta a uno de esos rumbos, 
 * no a "90 grados desde donde queda", asi el error de un giro no se arrastra al siguiente. */
#define AUTO_BUSCAR_ESQUINA 1

/* ------------------------------------------------------------------------
 * Parametros que tienen ~CALIBRAR~: ajustar ya con el robot armado en el piso.
 * ------------------------------------------------------------------------ */

// Definimos las velocidades Entre 0-1000 de roombateca_set_motion
#define AUTO_VEL_AVANCE 1000
#define AUTO_VEL_MOVER 1000
#define AUTO_VEL_GIRO   1000
#define AUTO_VEL_RETROCEDER 1000
/* CALIBRAR: velocidad del giro del ultimo tramo (ver AUTO_GIRO_FINO_RAD).
 * Si el robot no se mueve al llegar significa que hay que suirlo porque los motores no 
 * arrancan con poca potencia. Y bajarlo si se sigue pasando del angulo */
#define AUTO_VEL_GIRO_FINO 500 //~CALIBRAR~
// Definimos distancias:
// El paso es del ancho de boquilla, según el modelo3D 12cm
#define AUTO_DISTANCIA_OBSTACULO_CM 20.0f  // ~CALIBRAR~ Si toca la pared, SUBIR. Si queda muy lejos, BAJAR.
#define AUTO_DISTANCIA_MOVER_CM   20.0f // ~CALIBRAR~: tope durante el movimiento lateral
#define AUTO_DISTANCIA_TRASERA_CM 20.0f // ~CALIBRAR~: Tope del sensor trasero mientras retrocede
#define AUTO_PASO_MM        120.0          // 12 cm entre filas
#define AUTO_FILA_MIN_MM     50.0          // fila mas corta que = fin

#define AUTO_GIRO_RAD       1.5707963267948966  // 90 grados
#define AUTO_GIRO_TOL_RAD   0.04  // ~CALIBRAR~ tolerancia para dar por terminado un giro. Si se queda gira a un lado y al otro sin terminar: SUBIR.
#define AUTO_GIRO_FINO_RAD  0.35  // Cuando faltan menos de 20° baja la velocidad para mejorar precisión
/* CALIBRAR: parar el giro antes de llegar, para compensar la inercia. . Hoy los giros quedan cerca de 90, por eso 0. */
#define AUTO_GIRO_ANTICIPO_RAD 0.0 // ~CALIBRAR~ para compensar inercia, Si los giros se PASAN de 90°, SUBIR (ej. 0.05 = ~3°). Si se quedan cortos, BAJAR (minimo 0)
/* Con 1 corrige el rumbo a mitad de fila; con 0 solo lo corrigen sync_wheels y los giros a
 * rumbo absoluto. Sin IMU el rumbo sale de los encoders (~1.3° por pulso) y la correccion
 * frenaba el robot por ruido. */
#define AUTO_CORREGIR_RUMBO 0
#define AUTO_RUMBO_MAX_RAD  0.20  // ~CALIBRAR~ desvio maximo del rumbo durante una fila antes de parar y corregirlo (~11°, unos 9 pulsos de diferencia entre ruedas). Si corrige demasiado seguido, SUBIR.

/* ------------------------------------------------------------------------
 * Navegacion con el mapa (mapa.c). Si logica.c le pasa el mapa con
 * auto_usar_mapa(), cuando una "S" se corta (borde, fila vacia, mueble) en vez
 * de terminar busca en el mapa la zona sin aspirar mas cercana, va hasta ella
 * y empieza otra "S" ahi. Termina cuando no queda nada alcanzable sin aspirar.
 * Sin mapa (NULL) se comporta como antes: la primera "S" que se corta termina.
 * ------------------------------------------------------------------------ */
#define AUTO_RADIO_ROBOT_MM     150.0  // robot de ~30 cm. Tambien es lo que hay del centro al ultrasonico
#define AUTO_ANCHO_BOQUILLA_MM  120.0  // lo que se marca como aspirado en el mapa
#define AUTO_VEL_IR             1000   // velocidad yendo hacia la zona pendiente
#define AUTO_LLEGADA_MM          80.0  // a esta distancia de la zona ya se considera que llego
#define AUTO_TRAMO_MAX_MM      1500.0  // tramo recto maximo antes de volver a planear
#define AUTO_FALLOS_OBJETIVO_MAX  3    // intentos fallidos de llegar a una zona antes de descartarla
#define AUTO_DESCARTE_RADIO_MM  250.0  // alrededor de una zona descartada no se vuelve a buscar
#define AUTO_DESCARTES_MAX       64    // zonas descartadas que se recuerdan
#define AUTO_BLOQUES_MAX         60    // "S" nuevas como maximo en un recorrido
#define AUTO_TIMEOUT_IR_NS     6000000000ULL  // 6s por tramo

//NS es de nanosegundos por el reloj que estamos usando
#define AUTO_PAUSA_NS           300000000ULL  // parada entre maniobras (0.3s)
#define AUTO_TIMEOUT_AVANZAR_NS 30000000000ULL // 30s - el sensor no agarró nada
#define AUTO_TIMEOUT_GIRO_NS   4000000000ULL  // 4s
#define AUTO_TIMEOUT_MOVER_NS  3000000000ULL  // 3s
#define AUTO_TIMEOUT_TOTAL_NS  (30ULL * 60ULL * 1000000000ULL) // límite de todo el recorrido: 30 min

// Atasco: con los motores andando, si la odometria (encoders) no avanza en una
// ventana se toma como obstaculo que el ultrasonico no vio. La gracia evita
// confundir la aceleracion al arrancar la fase con un atasco.
#define AUTO_ATASCO_GRACIA_NS   500000000ULL  // 0.5s
#define AUTO_ATASCO_VENTANA_NS  400000000ULL  // 0.4s
#define AUTO_ATASCO_MIN_MM      10.0          // avance minimo por ventana
#define AUTO_ATASCO_MIN_RAD     0.05          // ~3 grados por ventana en giro
#define AUTO_ATASCOS_MAX        3             // seguidos sin completar maniobra = fin
#define AUTO_RETROCESO_MM       50.0          // cuanto se aleja tras un atasco
#define AUTO_TIMEOUT_RETROCEDER_NS 800000000ULL // 0.8s

typedef enum {
    AUTO_AVANZAR = 0,   // fila: recto hasta obstaculo
    AUTO_PARAR,         // pausa corta entre maniobras
    AUTO_GIRAR,         // hasta el rumbo objetivo (absoluto)
    AUTO_MOVER,         // avanzar AUTO_PASO_MM hacia la siguiente fila
    AUTO_RETROCEDER,    // tras un atasco, alejarse AUTO_RETROCESO_MM
    AUTO_PLANEAR,       // (con mapa) elegir la zona sin aspirar mas cercana y el siguiente tramo
    AUTO_IR,            // (con mapa) avanzar en linea recta hacia el tramo elegido
    AUTO_FIN            // termina recorrido se detienen los motores
} auto_fase_t;

typedef enum {
    AUTO_ETAPA_ESQUINA_1 = 0,  // nos movemos a la primera pared
    AUTO_ETAPA_ESQUINA_2,      // a la segunda pared, a lo largo de la primera (ya aspira)
    AUTO_ETAPA_BARRIDO
} auto_etapa_t;

typedef struct {
    const char *direccion;   // "FWD", "BACK", "TURN_L", "TURN_R" o "STOP"
    int velocidad;           // 0-1000
    bool aspirar;            // true durante el barrido
} auto_orden_t;

// Lecturas de los ultrasonicos. *_ok = la lectura es valida (false = sin eco, nada en rango)
typedef struct {
    float frontal_cm;
    bool frontal_ok;
    float trasero_cm;
    bool trasero_ok;
} auto_sensores_t;

typedef struct {
    auto_fase_t fase;
    auto_fase_t fase_siguiente;      //a donde ir al terminar PARAR
    auto_etapa_t etapa;
    int giro_n;                      // 1 = hacia el lateral (luego MOVER), 2 = hacia la fila (luego AVANZAR), 3 = hacia el tramo (luego IR)
    int lateral;                     // -1 = el lateral va a la derecha de rumbo_fila, +1 = a la izquierda
    int sentido;                     // 0 = fila a rumbo_fila, 1 = fila de vuelta (rumbo_fila + 180)
    double rumbo_fila_rad;           // referencia de todas las filas
    double rumbo_objetivo_rad;       // a donde apunta el giro en curso (absoluto)
    int giro_dir;                    // +1 = TURN_L, -1 = TURN_R
    bool giro_fino;                  // ya esta cerca: gira a AUTO_VEL_GIRO_FINO
    bool corrigiendo;                // giro para corregir el rumbo a mitad de fila
    uint64_t inicio_ns;              // arranque del recorrido (para el tiempo total)
    uint64_t inicio_fase_ns;
    double x_inicio_mm;              // inicio de la fila o del movimiento lateral
    double y_inicio_mm;
    double theta_anterior_rad;
    double giro_acumulado_rad;       // cuanto lleva girado (para detectar atasco)
    int atascos;                     // atascos seguidos sin completar una maniobra
    uint64_t atasco_ref_ns;          // punto de referencia de la ventana de atasco
    double atasco_ref_x_mm;
    double atasco_ref_y_mm;
    double atasco_ref_giro_rad;
    //Para la parte de navegación con el mapa *******
    mapa_t *mapa;                    // NULL = sin navegacion por mapa (se conserva en auto_reset)
    bool tiene_objetivo;             // hay una zona pendiente elegida
    double objetivo_x_mm;            // centro de la zona pendiente
    double objetivo_y_mm;
    double ir_x_mm;                  // final del tramo recto en curso
    double ir_y_mm;
    double ir_distancia_mm;
    int fallos_objetivo;             // intentos fallidos de llegar a la zona
    int bloques;                     // "S" empezadas desde una zona pendiente
    int n_descartes;
    double descartes_mm[AUTO_DESCARTES_MAX][2];  // zonas que no se vuelven a buscar
	// *******
    const char *motivo_fin;          // el porqué termino (para log y pruebas)
} auto_t;

void auto_init(auto_t *auto_estado);

// Vuelve al estado inicial. logica.c lo llama cada vez que se entra a AUTO
void auto_reset(auto_t *auto_estado);
// Le da el mapa para navegar (NULL = sin mapa). logica.c lo llama una vez al arrancar
void auto_usar_mapa(auto_t *auto_estado, mapa_t *mapa);
bool auto_fin(const auto_t *auto_estado);

// Cada paso de la maquina de estados (tick de control)
// Devuelve la orden que debe estar aplicada
auto_orden_t auto_paso(auto_t *auto_estado,
                       const odometria_pose_t *pose,
                       const auto_sensores_t *sensores,
                       uint64_t ahora_ns);

#ifdef __cplusplus
}
#endif

#endif
