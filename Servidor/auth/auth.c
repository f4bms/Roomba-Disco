#define _POSIX_C_SOURCE 200809L

#include "auth.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int auth_hex_encode(const uint8_t *input, size_t length, char *output, size_t output_size) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    if (output_size < length * 2 + 1) return -1;
    for (i = 0; i < length; ++i) {
        output[i * 2] = digits[(input[i] >> 4) & 0x0f];
        output[i * 2 + 1] = digits[input[i] & 0x0f];
    }
    output[length * 2] = '\0';
    return 0;
}

int auth_hex_decode(const char *input, uint8_t *output, size_t output_size, size_t *decoded_length) {
    size_t length = strlen(input);
    size_t i;

    if (length % 2 != 0 || length / 2 > output_size) return -1;
    for (i = 0; i < length; i += 2) {
        int high = hex_value(input[i]);
        int low = hex_value(input[i + 1]);
        if (high < 0 || low < 0) return -1;
        output[i / 2] = (uint8_t)((high << 4) | low);
    }
    if (decoded_length != NULL) *decoded_length = length / 2;
    return 0;
}

bool auth_bytes_equal(const uint8_t *a, const uint8_t *b, size_t length) {
    uint8_t diff = 0;
    size_t i;

    for (i = 0; i < length; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

int auth_random_bytes(void *buffer, size_t length) {
    FILE *source = fopen("/dev/urandom", "rb");
    size_t read_count;

    if (source == NULL) return -1;
    read_count = fread(buffer, 1, length, source);
    fclose(source);
    return read_count == length ? 0 : -1;
}

void auth_compute_verifier(const uint8_t *salt, size_t salt_length,
                           const char *password,
                           uint8_t verifier[SHA256_DIGEST_LENGTH]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, salt, salt_length);
    sha256_update(&ctx, password, strlen(password));
    sha256_final(&ctx, verifier);
}

void auth_compute_response(const uint8_t verifier[SHA256_DIGEST_LENGTH],
                           const uint8_t challenge[SHA256_DIGEST_LENGTH],
                           uint8_t response[SHA256_DIGEST_LENGTH]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, verifier, SHA256_DIGEST_LENGTH);
    sha256_update(&ctx, challenge, SHA256_DIGEST_LENGTH);
    sha256_final(&ctx, response);
}

static void trim_newline(char *line) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

int auth_store_load(const char *path, auth_store_t *store) {
    FILE *file;
    char line[256];

    store->count = 0;
    file = fopen(path, "r");
    if (file == NULL) return -1;

    while (fgets(line, sizeof(line), file) != NULL && store->count < AUTH_MAX_USERS) {
        char *salt_field;
        char *verifier_field;
        size_t salt_length = 0;
        size_t verifier_length = 0;
        auth_user_t *entry;

        trim_newline(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        salt_field = strchr(line, ':');
        if (salt_field == NULL) continue;
        *salt_field++ = '\0';
        verifier_field = strchr(salt_field, ':');
        if (verifier_field == NULL) continue;
        *verifier_field++ = '\0';

        if (line[0] == '\0' || strlen(line) >= AUTH_MAX_USERNAME) continue;

        entry = &store->users[store->count];
        if (auth_hex_decode(salt_field, entry->salt, sizeof(entry->salt), &salt_length) != 0
                || salt_length != AUTH_SALT_LENGTH) continue;
        if (auth_hex_decode(verifier_field, entry->verifier, sizeof(entry->verifier), &verifier_length) != 0
                || verifier_length != SHA256_DIGEST_LENGTH) continue;

        strncpy(entry->username, line, sizeof(entry->username) - 1);
        entry->username[sizeof(entry->username) - 1] = '\0';
        store->count++;
    }

    fclose(file);
    return 0;
}

const auth_user_t *auth_store_find(const auth_store_t *store, const char *username) {
    size_t i;
    for (i = 0; i < store->count; ++i) {
        if (strcmp(store->users[i].username, username) == 0) return &store->users[i];
    }
    return NULL;
}
