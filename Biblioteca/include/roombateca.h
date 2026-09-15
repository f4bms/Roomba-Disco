#ifndef ROOMBATECA_H
#define ROOMBATECA_H

/* Punto de entrada de la biblioteca: agrupa los módulos de hardware. */

#include "motores.h"
#include "sensores.h"
#include "leds.h"
#include "audio_th.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reproduce una de las pistas de prueba: 0 = alerta, 1 = MrTaxiCut.
 * Temporal, para validar audio_th end-to-end; los índices/rutas cambian
 * cuando se decida dónde viven los assets de audio en la imagen final. */
void roombateca_reproducir(int cancion);

/* Healthcheck: no toca hardware, solo confirma que la biblioteca está
 * cargada. Pensada para que el servidor la use al arrancar. */
int roombateca_ping(void);

/* Por definir: el resto de operaciones de alto nivel del robot (giros,
 * avance/retroceso compuesto, evasión de obstáculos), construidas sobre
 * motor_izquierdo_set/motor_derecho_set de motores.h. */

#ifdef __cplusplus
}
#endif

#endif
