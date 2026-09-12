#include "thgpio.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// Funciones para motores y sensores
void setSpeed(int speed){
	printf("[PRUEBA] Ajustando velocidad a: %d\n", speed);
}

void move_left(){
	printf("[PRUEBA] Girando a la izquierda.\n");
}

void move_right() {
	printf("[PRUEBA] Girando a la derecha.\n");
}

float get_distance(int sensorID){
	return 42.0f;
}

void ledSet(int led_id, bool state){
	printf("[PRUEBA] LED %d cambiado a %d\n", led_id, state);
}

/*//-------------------------------------
// Para los hilos de audio
void* play_audio_thread(void* arg) {
        
	printf("\n[HILO AUDIO] >>> ¡Inicio de la musica de fondo! <<< \n");

	// Simulamos la reproducción de un audio de 3 segundos
	for(int i = 1; i <= 3; i++) {
		printf("♫---[HILO AUDIO] la la la la la... Segundo: %d/3---♫\n", i);
		sleep(1);
	}

	printf("[HILO AUDIO] >>> Audio finalizado. Hilo cierra de forma limpia. <<<\n\n");
	return NULL;
}

void trigger_notification_audio(const char* path) {
	pthread_t thread_id;
	// Creamos el hilo real de Linux
	if (pthread_create(&thread_id, NULL, play_audio_thread, NULL) != 0) {
		perror("[ERROR DevOps] Falló la creación del hilo de audio");
	}
	pthread_detach(thread_id); // Lo separamos para que no bloquee el resto del robot
}
*/
//-------------------------------------

// Para los hilos del audio
void* play_audio_thread(void* arg) {
	char* path = (char*)arg;
	char command[512];
	printf("\n[HILO AUDIO] >>> ¡Inicio de la musica de fondo! <<< \n");
	snprintf(command, sizeof(command), "mpg123 -q %s > /dev/null 2>&1", path);
	printf("[PRUEBA AUDIO] Reproduciendo mientras en background... lalala...\n");
	system(command);
	printf("[HILO AUDIO] >>> Audio finalizado. Hilo cierra de forma limpia. <<<\n\n");
	free(path);
	return NULL;
}

void trigger_notification_audio(const char* path) {
        pthread_t thread_id;

        // Duplicamos dinámicamente la ruta en memoria para que el hilo pueda leerla de forma segura
        char* path_copy = malloc(256);
        if (path_copy != NULL) {
                snprintf(path_copy, 256, "%s", path);

                if (pthread_create(&thread_id, NULL, play_audio_thread, path_copy) != 0) {
                        perror("[ERROR] Falló la creación del hilo de audio");
                        free(path_copy);
                }
                pthread_detach(thread_id); // Hilo concurrente en background
        }
}


