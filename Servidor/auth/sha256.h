#ifndef ROOMBA_SHA256_H
#define ROOMBA_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LENGTH 32

/* Implementacion propia de SHA-256 (sin bibliotecas externas), pensada como
 * ejercicio academico. No usar como unica proteccion en un sistema real. */
typedef struct {
    uint32_t state[8];
    uint64_t bit_length;
    uint8_t buffer[64];
    size_t buffer_length;
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t length);
void sha256_final(sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_LENGTH]);

/* Atajo: calcula el digest de un bloque de datos completo. */
void sha256(const void *data, size_t length, uint8_t digest[SHA256_DIGEST_LENGTH]);

#endif
