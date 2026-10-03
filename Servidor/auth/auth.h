#ifndef ROOMBA_AUTH_H
#define ROOMBA_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#define AUTH_SALT_LENGTH 16
#define AUTH_MAX_USERNAME 64
#define AUTH_MAX_USERS 16

/* Un usuario registrado: nombre, salt aleatorio y verificador.
 * verifier = SHA256(salt || password). La contrasena nunca se guarda. */
typedef struct {
    char username[AUTH_MAX_USERNAME];
    uint8_t salt[AUTH_SALT_LENGTH];
    uint8_t verifier[SHA256_DIGEST_LENGTH];
} auth_user_t;

typedef struct {
    auth_user_t users[AUTH_MAX_USERS];
    size_t count;
} auth_store_t;

/* Carga el archivo de usuarios (lineas "usuario:salt_hex:verifier_hex").
 * Devuelve 0 si pudo abrirlo, -1 si no. Un archivo ausente deja count = 0. */
int auth_store_load(const char *path, auth_store_t *store);

/* Busca un usuario por nombre. Devuelve NULL si no existe. */
const auth_user_t *auth_store_find(const auth_store_t *store, const char *username);

/* Llena buffer con bytes aleatorios desde /dev/urandom. 0 ok, -1 error. */
int auth_random_bytes(void *buffer, size_t length);

/* verifier = SHA256(salt || password). */
void auth_compute_verifier(const uint8_t *salt, size_t salt_length,
                           const char *password,
                           uint8_t verifier[SHA256_DIGEST_LENGTH]);

/* response = SHA256(verifier || challenge). Mismo calculo en cliente y servidor. */
void auth_compute_response(const uint8_t verifier[SHA256_DIGEST_LENGTH],
                           const uint8_t challenge[SHA256_DIGEST_LENGTH],
                           uint8_t response[SHA256_DIGEST_LENGTH]);

int auth_hex_encode(const uint8_t *input, size_t length, char *output, size_t output_size);
int auth_hex_decode(const char *input, uint8_t *output, size_t output_size, size_t *decoded_length);

/* Comparacion en tiempo constante para no filtrar informacion por timing. */
bool auth_bytes_equal(const uint8_t *a, const uint8_t *b, size_t length);

#endif
