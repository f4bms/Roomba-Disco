#include <stdio.h>
#include <unistd.h>
#include "thgpio.h"

//Ponemos speed en 200

int main(void) {
	printf("[HILO PRINCIPAL] Inicializando el Roomba-Disco...\n");
	setSpeed(200);
	ledSet(5, 1);

	printf("[HILO PRINCIPAL] ¡Obstáculo! Trigger audio...\n");
	// Disparamos el audio (El hilo de fondo debería arrancar de inmediato)
	trigger_notification_audio("audio/MrTaxiCut.mp3");
        
        ledSet(5, 0);
	// Probando hilos
	for(int i = 1; i <= 5; i++) {
		printf("[HILO PRINCIPAL] Evadiendo... shun shun %d/5\n", i);
		float distancia = get_distance(i);
	        printf("[HILO PRINCIPAL] Leyendo sensor ID %d -> distancia %.2fcm\n", i*2, distancia);
		move_left();
		sleep(1);
	}
        ledSet(2, 1);
        move_right();
        move_right();
        move_right();
	printf("[HILO PRINCIPAL] Simulación terminada con éxito.\n");
	return 0;
}
