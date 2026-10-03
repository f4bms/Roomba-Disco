#include <assert.h>
#include <string.h>

#include "auth.h"
#include "sha256.h"

static void check_hex(const void *data, size_t length, const char *expected) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    sha256(data, length, digest);
    auth_hex_encode(digest, sizeof(digest), hex, sizeof(hex));
    assert(strcmp(hex, expected) == 0);
}

int main(void) {
    /* Vectores conocidos de SHA-256 (FIPS 180-4). */
    check_hex("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_hex("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* El verificador y la respuesta deben ser deterministas y reproducibles. */
    const uint8_t salt[AUTH_SALT_LENGTH] = {0};
    const uint8_t challenge[SHA256_DIGEST_LENGTH] = {1};
    uint8_t verifier_a[SHA256_DIGEST_LENGTH];
    uint8_t verifier_b[SHA256_DIGEST_LENGTH];
    uint8_t response_a[SHA256_DIGEST_LENGTH];
    uint8_t response_b[SHA256_DIGEST_LENGTH];

    auth_compute_verifier(salt, sizeof(salt), "secreta", verifier_a);
    auth_compute_verifier(salt, sizeof(salt), "secreta", verifier_b);
    assert(auth_bytes_equal(verifier_a, verifier_b, SHA256_DIGEST_LENGTH));

    auth_compute_verifier(salt, sizeof(salt), "otra", verifier_b);
    assert(!auth_bytes_equal(verifier_a, verifier_b, SHA256_DIGEST_LENGTH));

    auth_compute_response(verifier_a, challenge, response_a);
    auth_compute_response(verifier_a, challenge, response_b);
    assert(auth_bytes_equal(response_a, response_b, SHA256_DIGEST_LENGTH));

    /* hex round-trip. */
    uint8_t decoded[SHA256_DIGEST_LENGTH];
    char encoded[SHA256_DIGEST_LENGTH * 2 + 1];
    size_t decoded_length = 0;
    auth_hex_encode(response_a, sizeof(response_a), encoded, sizeof(encoded));
    assert(auth_hex_decode(encoded, decoded, sizeof(decoded), &decoded_length) == 0);
    assert(decoded_length == SHA256_DIGEST_LENGTH);
    assert(auth_bytes_equal(response_a, decoded, SHA256_DIGEST_LENGTH));

    return 0;
}
