#ifndef ROOMBATECA_H
#define ROOMBATECA_H

#include "motores.h"
#include "sensores.h"
#include "leds.h"
#include "audio_th.h"
#include "encoders.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Módulos para roombateca_init; se combinan con |. */
#define ROOMBATECA_MOTORES  (1u << 0)
#define ROOMBATECA_SENSORES (1u << 1)
#define ROOMBATECA_LEDS     (1u << 2)
#define ROOMBATECA_AUDIO    (1u << 3)
#define ROOMBATECA_ENCODERS (1u << 4)  /* requiere ROOMBATECA_MOTORES */
#define ROOMBATECA_TODOS    (0x1Fu)

/* Inicia los módulos pedidos, motores primero. Si uno falla, deshace los
 * anteriores. Devuelve 0 o -errno. */
int roombateca_init(unsigned int modulos);

/* Detiene los motores y libera todo lo iniciado. */
void roombateca_cleanup(void);

/* Healthcheck: no toca hardware, solo confirma que la biblioteca está
 * cargada. Pensada para que el servidor la use al arrancar. */
int roombateca_ping(void);

/* Reproduce una de las pistas de prueba: 0 = alerta, 1 = MrTaxiCut.
 * Temporal, para validar audio_th end-to-end. */
void roombateca_reproducir(int cancion);

#ifdef __cplusplus
}
#endif

#endif
