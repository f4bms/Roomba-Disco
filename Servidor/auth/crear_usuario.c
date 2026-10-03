#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "auth.h"

/* Herramienta administrativa: genera la linea "usuario:salt:verifier" para el
 * archivo de usuarios. La contrasena solo se usa aqui, nunca se guarda. */
int main(int argc, char **argv) {
    uint8_t salt[AUTH_SALT_LENGTH];
    uint8_t verifier[SHA256_DIGEST_LENGTH];
    char salt_hex[AUTH_SALT_LENGTH * 2 + 1];
    char verifier_hex[SHA256_DIGEST_LENGTH * 2 + 1];

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "uso: %s <usuario> <contrasena> [archivo]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (strchr(argv[1], ':') != NULL || strlen(argv[1]) >= AUTH_MAX_USERNAME) {
        fprintf(stderr, "usuario invalido (sin ':' y menor a %d caracteres)\n", AUTH_MAX_USERNAME);
        return EXIT_FAILURE;
    }

    if (auth_random_bytes(salt, sizeof(salt)) != 0) {
        fprintf(stderr, "no se pudo generar el salt aleatorio\n");
        return EXIT_FAILURE;
    }
    auth_compute_verifier(salt, sizeof(salt), argv[2], verifier);
    auth_hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    auth_hex_encode(verifier, sizeof(verifier), verifier_hex, sizeof(verifier_hex));

    if (argc == 4) {
        umask(0077);
        FILE *file = fopen(argv[3], "a");
        if (file == NULL) {
            fprintf(stderr, "no se pudo abrir %s\n", argv[3]);
            return EXIT_FAILURE;
        }
        fprintf(file, "%s:%s:%s\n", argv[1], salt_hex, verifier_hex);
        if (fclose(file) != 0 || chmod(argv[3], S_IRUSR | S_IWUSR) != 0) {
            fprintf(stderr, "no se pudieron restringir los permisos de %s\n", argv[3]);
            return EXIT_FAILURE;
        }
        fprintf(stderr, "usuario '%s' agregado a %s\n", argv[1], argv[3]);
    } else {
        printf("%s:%s:%s\n", argv[1], salt_hex, verifier_hex);
    }
    return EXIT_SUCCESS;
}
