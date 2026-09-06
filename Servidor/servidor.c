#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define REQUEST_SIZE 8192
#define MAX_PAYLOAD 65535

struct sha1 {
    uint32_t state[5];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
};

static uint32_t rotate_left(uint32_t value, unsigned int count) {
    return (value << count) | (value >> (32 - count));
}

static void sha1_transform(struct sha1 *hash, const unsigned char block[64]) {
    uint32_t words[80];
    uint32_t a, b, c, d, e;

    for (int i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 80; ++i) {
        words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }

    a = hash->state[0];
    b = hash->state[1];
    c = hash->state[2];
    d = hash->state[3];
    e = hash->state[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t function;
        uint32_t constant;
        uint32_t temporary;

        if (i < 20) {
            function = (b & c) | ((~b) & d);
            constant = 0x5A827999;
        } else if (i < 40) {
            function = b ^ c ^ d;
            constant = 0x6ED9EBA1;
        } else if (i < 60) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDC;
        } else {
            function = b ^ c ^ d;
            constant = 0xCA62C1D6;
        }

        temporary = rotate_left(a, 5) + function + e + constant + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temporary;
    }

    hash->state[0] += a;
    hash->state[1] += b;
    hash->state[2] += c;
    hash->state[3] += d;
    hash->state[4] += e;
}

static void sha1_init(struct sha1 *hash) {
    hash->state[0] = 0x67452301;
    hash->state[1] = 0xEFCDAB89;
    hash->state[2] = 0x98BADCFE;
    hash->state[3] = 0x10325476;
    hash->state[4] = 0xC3D2E1F0;
    hash->bits = 0;
    hash->used = 0;
}

static void sha1_update(struct sha1 *hash, const unsigned char *data, size_t length) {
    hash->bits += (uint64_t)length * 8;
    while (length > 0) {
        size_t available = sizeof(hash->block) - hash->used;
        size_t amount = length < available ? length : available;
        memcpy(hash->block + hash->used, data, amount);
        hash->used += amount;
        data += amount;
        length -= amount;

        if (hash->used == sizeof(hash->block)) {
            sha1_transform(hash, hash->block);
            hash->used = 0;
        }
    }
}

static void sha1_final(struct sha1 *hash, unsigned char digest[20]) {
    unsigned char length[8];

    for (int i = 0; i < 8; ++i) {
        length[7 - i] = (unsigned char)(hash->bits >> (i * 8));
    }

    sha1_update(hash, (const unsigned char *)"\x80", 1);
    while (hash->used != 56) {
        sha1_update(hash, (const unsigned char *)"\0", 1);
    }
    sha1_update(hash, length, sizeof(length));

    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = (unsigned char)(hash->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(hash->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(hash->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)hash->state[i];
    }
}

static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t encode_base64(const unsigned char *input, size_t length, char *output) {
    size_t position = 0;
    size_t written = 0;

    while (position < length) {
        uint32_t value = (uint32_t)input[position++] << 16;
        int remaining = (int)(length - position + 1);

        if (position < length) {
            value |= (uint32_t)input[position] << 8;
        }
        if (position + 1 < length) {
            value |= input[position + 1];
        }
        if (remaining > 1) {
            ++position;
        }
        if (remaining > 2) {
            ++position;
        }

        output[written++] = base64[(value >> 18) & 63];
        output[written++] = base64[(value >> 12) & 63];
        output[written++] = remaining > 1 ? base64[(value >> 6) & 63] : '=';
        output[written++] = remaining > 2 ? base64[value & 63] : '=';
    }

    output[written] = '\0';
    return written;
}

static int send_all(int socket_fd, const void *buffer, size_t length) {
    const unsigned char *data = buffer;
    while (length > 0) {
        ssize_t sent = send(socket_fd, data, length, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static const char *header_value(const char *request, const char *name) {
    size_t name_length = strlen(name);
    const char *line = request;

    while (*line != '\0') {
        const char *end = strstr(line, "\r\n");
        size_t line_length = end ? (size_t)(end - line) : strlen(line);
        if (line_length > name_length + 1 && strncasecmp(line, name, name_length) == 0 && line[name_length] == ':') {
            const char *value = line + name_length + 1;
            while (*value == ' ') {
                ++value;
            }
            return value;
        }
        if (!end) {
            break;
        }
        line = end + 2;
    }
    return NULL;
}

static int websocket_accept_key(const char *client_key, char output[29]) {
    unsigned char digest[20];
    char source[128];
    char key[64];
    struct sha1 hash;
    size_t key_length = strcspn(client_key, "\r\n \t");
    int written;

    if (key_length == 0 || key_length >= sizeof(key)) {
        return -1;
    }
    memcpy(key, client_key, key_length);
    key[key_length] = '\0';
    written = snprintf(source, sizeof(source), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);

    if (written < 0 || (size_t)written >= sizeof(source)) {
        return -1;
    }
    sha1_init(&hash);
    sha1_update(&hash, (const unsigned char *)source, (size_t)written);
    sha1_final(&hash, digest);
    encode_base64(digest, sizeof(digest), output);
    return 0;
}

static int receive_request(int socket_fd, char request[REQUEST_SIZE]) {
    size_t used = 0;
    while (used + 1 < REQUEST_SIZE) {
        ssize_t received = recv(socket_fd, request + used, REQUEST_SIZE - used - 1, 0);
        if (received <= 0) {
            return -1;
        }
        used += (size_t)received;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) {
            return 0;
        }
    }
    return -1;
}

static int send_frame(int socket_fd, unsigned char opcode, const unsigned char *payload, size_t length) {
    unsigned char header[10];
    size_t header_length = 2;

    if (length > MAX_PAYLOAD) {
        return -1;
    }
    header[0] = (unsigned char)(0x80 | (opcode & 0x0F));
    if (length < 126) {
        header[1] = (unsigned char)length;
    } else {
        header[1] = 126;
        header[2] = (unsigned char)(length >> 8);
        header[3] = (unsigned char)length;
        header_length = 4;
    }
    if (send_all(socket_fd, header, header_length) < 0 || send_all(socket_fd, payload, length) < 0) {
        return -1;
    }
    return 0;
}

static int websocket_session(int socket_fd) {
    unsigned char header[2];

    while (1) {
        unsigned char frame[MAX_PAYLOAD + 1];
        unsigned char mask[4];
        uint64_t length;
        size_t header_length;

        if (recv(socket_fd, header, sizeof(header), MSG_WAITALL) != (ssize_t)sizeof(header)) {
            return 0;
        }
        length = header[1] & 0x7F;
        header_length = 2;
        if (length == 126) {
            unsigned char extended[2];
            if (recv(socket_fd, extended, sizeof(extended), MSG_WAITALL) != (ssize_t)sizeof(extended)) {
                return -1;
            }
            length = ((uint64_t)extended[0] << 8) | extended[1];
            header_length += 2;
        } else if (length == 127) {
            return -1;
        }

        if (!(header[1] & 0x80) || length > MAX_PAYLOAD) {
            return -1;
        }
        if (recv(socket_fd, mask, sizeof(mask), MSG_WAITALL) != (ssize_t)sizeof(mask)) {
            return -1;
        }
        if (recv(socket_fd, frame, (size_t)length, MSG_WAITALL) != (ssize_t)length) {
            return -1;
        }
        for (size_t i = 0; i < (size_t)length; ++i) {
            frame[i] ^= mask[i % 4];
        }

        switch (header[0] & 0x0F) {
            case 0x1:
                frame[length] = '\0';
                printf("mensaje recibido: %s\n", frame);
                fflush(stdout);
                if (send_frame(socket_fd, 0x1, frame, (size_t)length) < 0) {
                    return -1;
                }
                break;
            case 0x8:
                send_frame(socket_fd, 0x8, NULL, 0);
                return 0;
            case 0x9:
                if (send_frame(socket_fd, 0xA, frame, (size_t)length) < 0) {
                    return -1;
                }
                break;
            default:
                break;
        }
        (void)header_length;
    }
}

static int create_server_socket(int port) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    struct sockaddr_in address;

    if (socket_fd < 0) {
        return -1;
    }
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(socket_fd, 8) < 0) {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
    int server_socket;

    if (port < 1 || port > 65535) {
        fprintf(stderr, "uso: %s [puerto]\n", argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);
    server_socket = create_server_socket(port);
    if (server_socket < 0) {
        perror("no se pudo iniciar el servidor");
        return EXIT_FAILURE;
    }

    printf("Servidor WebSocket escuchando en ws://localhost:%d\n", port);
    fflush(stdout);

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        char request[REQUEST_SIZE];
        const char *key;
        char accept_key[29];
        char response[256];
        int response_length;

        if (client_socket < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        if (receive_request(client_socket, request) < 0) {
            close(client_socket);
            continue;
        }
        key = header_value(request, "Sec-WebSocket-Key");
        if (key == NULL || websocket_accept_key(key, accept_key) < 0) {
            const char response[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
            send_all(client_socket, response, sizeof(response) - 1);
            close(client_socket);
            continue;
        }

        response_length = snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n", accept_key);
        if (send_all(client_socket, response, (size_t)response_length) < 0) {
            close(client_socket);
            continue;
        }

        printf("cliente conectado\n");
        fflush(stdout);
        websocket_session(client_socket);
        printf("cliente desconectado\n");
        fflush(stdout);
        close(client_socket);
    }

    close(server_socket);
    return EXIT_SUCCESS;
}
