#ifndef AUTO_H
#define AUTO_H

#include <stdbool.h>
#include <stdint.h>

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
 * habitacion aunque arranque en el centro. Con 0 empieza a barrer sin buscar la esquina.
 * Termina cuando el movimiento "lateral" ya no cabe, es decir, llega al borde. */
#define AUTO_BUSCAR_ESQUINA 1

// Definimos las velocidades Entre 0-1000 de roombateca_set_motion
#define AUTO_VEL_AVANCE 300
#define AUTO_VEL_MOVER 250
#define AUTO_VEL_GIRO   250

// Definimos distancias:
// El paso es del ancho de boquilla, según el modelo3D 12cm
#define AUTO_DISTANCIA_OBSTACULO_CM 20.0f  // fin de fila 
#define AUTO_DISTANCIA_MOVER_CM   10.0f    // tope durante el movimiento lateral
#define AUTO_PASO_MM        120.0          // 12 cm entre filas
#define AUTO_FILA_MIN_MM     50.0          // fila mas corta que = fin

#define AUTO_GIRO_RAD       1.5707963267948966  // 90 grados
#define AUTO_GIRO_ANTICIPO_RAD 0.0   // parar para compensar inercia

//NS es de nanosegundos por el reloj que estamos usando
#define AUTO_PAUSA_NS         300000000ULL  // parada entre maniobras (0.3s)
#define AUTO_TIMEOUT_GIRO_NS 4000000000ULL  // 4s
#define AUTO_TIMEOUT_MOVER_NS 3000000000ULL // 3s

typedef enum {
    AUTO_AVANZAR = 0,   // fila: recto hasta obstaculo
    AUTO_PARAR,         // pausa corta entre maniobras
    AUTO_GIRAR,         // 90 grados dependiendo del 'lado' 
    AUTO_MOVER,         // avanzar AUTO_PASO_MM hacia la siguiente fila
    AUTO_FIN            // termina recorrido se detienen los motores
} auto_fase_t;

typedef enum {
    AUTO_ETAPA_ESQUINA_1 = 0,  // nos movemos a la primera pared
    AUTO_ETAPA_ESQUINA_2,      // nos movemos a la segunda pared (quedamos en esquina)
    AUTO_ETAPA_BARRIDO
} auto_etapa_t;

typedef struct {
    const char *direccion;   // "FWD", "TURN_L", "TURN_R" o "STOP" 
    int velocidad;           // 0-1000
    bool aspirar;            // true durante el barrido (esta parte todavía no está atada)
} auto_orden_t;

typedef struct {
    auto_fase_t fase;
    auto_fase_t fase_siguiente;      //a donde ir al terminar PARAR
    auto_etapa_t etapa;
    int lado;                        // +1 = izquierda, -1 = derecha, para saber hacia donde girar
    int giro_n;                      // 1 o 2 dentro del patron
    bool patron_completo;            // false: giro simple (en la esquina)
    uint64_t inicio_fase_ns;
    double x_inicio_mm;              // inicio de la fila del "movimiento lateral"
    double y_inicio_mm;
    double theta_anterior_rad;
    double giro_acumulado_rad;
    double giro_objetivo_rad;        // tiene signo para saber hacia donde es el giro
} auto_t;

void auto_init(auto_t *auto_estado);

// Vuelve al estado inicial. logica.c lo llama cada vez que se entra a AUTO
void auto_reset(auto_t *auto_estado);
bool auto_fin(const auto_t *auto_estado);

// Cada paso de la maquina de estados (tick de control)
// frontal_ok = la lectura es valida (false = sin eco = nada en rango, 
// Devuelve la orden que debe estar aplicada
auto_orden_t auto_paso(auto_t *auto_estado,
                               const odometria_pose_t *pose,
                               float frontal_cm, bool frontal_ok,
                               uint64_t ahora_ns);

#ifdef __cplusplus
}
#endif

#endif
