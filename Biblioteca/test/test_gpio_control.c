/* Aplicación de verificación de gpio_control (Tutorial Yocto II): configura
 * dos GPIO como salida y uno como entrada, escribe valores binarios en una
 * salida, hace blink en la otra por 5 s, y lee + imprime el pin de entrada.
 *
 * Compilar enlazando contra libroombateca (gpio_control vive ahí), p.ej.:
 *   $CC test/test_gpio_control.c -Iinclude -L<dir-con-libroombateca.so> \
 *       -lroombateca -o test_gpio_control */

#include "gpio_control.h"

#include <stdio.h>
#include <unistd.h>

/* Por definir: pines reales una vez cierre el mapa de GPIO (#20). Son
 * placeholders para probar la biblioteca mientras tanto. */
#define PIN_SALIDA_DIGITAL 17
#define PIN_SALIDA_BLINK   27
#define PIN_ENTRADA        22

#define BLINK_FREQ_HZ 2.0
#define BLINK_DURACION_S 5.0

int main(void) {
    if (pinMode(PIN_SALIDA_DIGITAL, GPIO_OUTPUT) != 0 ||
        pinMode(PIN_SALIDA_BLINK, GPIO_OUTPUT) != 0 ||
        pinMode(PIN_ENTRADA, GPIO_INPUT) != 0) {
        fprintf(stderr, "No se pudieron configurar los pines GPIO\n");
        return 1;
    }

    printf("Escribiendo valores binarios en el pin %d\n", PIN_SALIDA_DIGITAL);
    digitalWrite(PIN_SALIDA_DIGITAL, 1);
    sleep(1);
    digitalWrite(PIN_SALIDA_DIGITAL, 0);
    sleep(1);
    digitalWrite(PIN_SALIDA_DIGITAL, 1);

    printf("Blink en el pin %d a %.1f Hz durante %.0f s\n",
           PIN_SALIDA_BLINK, BLINK_FREQ_HZ, BLINK_DURACION_S);
    blink(PIN_SALIDA_BLINK, BLINK_FREQ_HZ, BLINK_DURACION_S);

    int valor = digitalRead(PIN_ENTRADA);
    printf("Valor leído en el pin de entrada %d: %d\n", PIN_ENTRADA, valor);

    gpio_control_cleanup();
    return 0;
}
