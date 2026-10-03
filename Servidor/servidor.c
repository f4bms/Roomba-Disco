#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "civetweb.h"
#include "cJSON.h"
#include "auth.h"

#define DEFAULT_PORT "8080"
#define DEFAULT_WEB_ROOT "../Cliente/dist/scrap-e-controller/browser"
#define DEFAULT_LOGIC_SOCKET "/tmp/roomba-logica.sock"
#define DEFAULT_USERS_FILE "usuarios.conf"
#define MAX_CLIENTS 16
#define MESSAGE_CAPACITY (256 * 1024)

/* Estado por conexion WebSocket: una sesion solo habla con Logica tras autenticar. */
typedef struct {
    struct mg_connection *connection;
    bool authenticated;
    bool has_challenge;
    uint8_t challenge[SHA256_DIGEST_LENGTH];
    uint8_t verifier[SHA256_DIGEST_LENGTH];
} client_slot_t;

static volatile sig_atomic_t keep_running = 1;
static client_slot_t clients[MAX_CLIENTS];
static auth_store_t user_store;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t logic_mutex = PTHREAD_MUTEX_INITIALIZER;
static int logic_socket = -1;
static const char *logic_socket_path = DEFAULT_LOGIC_SOCKET;

static void stop_server(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

/* Busca el slot de una conexion. Llamar con clients_mutex tomado. */
static client_slot_t *find_slot(const struct mg_connection *connection) {
    size_t index;
    for (index = 0; index < MAX_CLIENTS; ++index) {
        if (clients[index].connection == connection) return &clients[index];
    }
    return NULL;
}

static void send_json_text(struct mg_connection *connection, const char *text) {
    mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, text, strlen(text));
}

static const char *default_web_root(char web_root[PATH_MAX]) {
    ssize_t executable_length = readlink("/proc/self/exe", web_root, PATH_MAX - 1);
    char *executable_directory;

    if (executable_length > 0) {
        web_root[executable_length] = '\0';
        executable_directory = strrchr(web_root, '/');
        if (executable_directory != NULL) {
            *executable_directory = '\0';
            if (snprintf(web_root + strlen(web_root),
                         PATH_MAX - strlen(web_root),
                         "/../share/roomba-disco/www") < PATH_MAX - (int)strlen(web_root)
                && access(web_root, R_OK) == 0) {
                return web_root;
            }
        }
    }

    return DEFAULT_WEB_ROOT;
}

static int websocket_connect(const struct mg_connection *connection, void *callback_data) {
    (void)connection;
    (void)callback_data;
    return 0;
}

static void websocket_ready(struct mg_connection *connection, void *callback_data) {
    size_t index;
    (void)callback_data;

    pthread_mutex_lock(&clients_mutex);
    for (index = 0; index < MAX_CLIENTS; ++index) {
        if (clients[index].connection == NULL) {
            clients[index].connection = connection;
            clients[index].authenticated = false;
            clients[index].has_challenge = false;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    printf("cliente conectado\n");
    fflush(stdout);
}

static bool send_to_logic(const char *data, size_t data_length) {
    size_t sent = 0;
    bool success = false;

    pthread_mutex_lock(&logic_mutex);
    if (logic_socket >= 0) {
        while (sent < data_length) {
            ssize_t result = send(logic_socket, data + sent, data_length - sent, MSG_NOSIGNAL);
            if (result <= 0) break;
            sent += (size_t)result;
        }
        success = sent == data_length && send(logic_socket, "\n", 1, MSG_NOSIGNAL) == 1;
    }
    pthread_mutex_unlock(&logic_mutex);
    return success;
}

static void broadcast_to_clients(const char *data, size_t data_length) {
    size_t index;
    pthread_mutex_lock(&clients_mutex);
    for (index = 0; index < MAX_CLIENTS; ++index) {
        if (clients[index].connection != NULL && clients[index].authenticated) {
            mg_websocket_write(clients[index].connection, MG_WEBSOCKET_OPCODE_TEXT, data, data_length);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static int connect_to_logic(void) {
    struct sockaddr_un address = {0};
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (socket_fd < 0) return -1;
    address.sun_family = AF_UNIX;
    if (strlen(logic_socket_path) >= sizeof(address.sun_path)) {
        close(socket_fd);
        return -1;
    }
    strcpy(address.sun_path, logic_socket_path);
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static void *logic_reader(void *callback_data) {
    char buffer[MESSAGE_CAPACITY];
    size_t used = 0;
    (void)callback_data;

    while (keep_running) {
        int current_socket;
        pthread_mutex_lock(&logic_mutex);
        if (logic_socket < 0) logic_socket = connect_to_logic();
        current_socket = logic_socket;
        pthread_mutex_unlock(&logic_mutex);

        if (current_socket < 0) {
            sleep(1);
            continue;
        }

        ssize_t received = recv(current_socket, buffer + used, sizeof(buffer) - used - 1, 0);
        if (received <= 0) {
            pthread_mutex_lock(&logic_mutex);
            if (logic_socket == current_socket) {
                close(logic_socket);
                logic_socket = -1;
            }
            pthread_mutex_unlock(&logic_mutex);
            used = 0;
            continue;
        }

        used += (size_t)received;
        buffer[used] = '\0';
        char *line_start = buffer;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            if (*line_start != '\0') broadcast_to_clients(line_start, (size_t)(newline - line_start));
            line_start = newline + 1;
        }
        used -= (size_t)(line_start - buffer);
        memmove(buffer, line_start, used);
        if (used == sizeof(buffer) - 1) used = 0;
    }
    return NULL;
}

static void handle_auth_init(struct mg_connection *connection, const cJSON *message) {
    const cJSON *user = cJSON_GetObjectItemCaseSensitive(message, "user");
    const auth_user_t *entry;
    client_slot_t *slot;
    uint8_t challenge[SHA256_DIGEST_LENGTH];
    uint8_t salt[AUTH_SALT_LENGTH];
    char salt_hex[AUTH_SALT_LENGTH * 2 + 1];
    char challenge_hex[SHA256_DIGEST_LENGTH * 2 + 1];
    char response[160];

    if (!cJSON_IsString(user) || auth_random_bytes(challenge, sizeof(challenge)) != 0) {
        send_json_text(connection, "{\"type\":\"auth_result\",\"ok\":false}");
        return;
    }

    /* Usuario desconocido recibe un salt señuelo para no revelar su ausencia. */
    entry = auth_store_find(&user_store, user->valuestring);
    if (entry != NULL) {
        memcpy(salt, entry->salt, sizeof(salt));
    } else if (auth_random_bytes(salt, sizeof(salt)) != 0) {
        send_json_text(connection, "{\"type\":\"auth_result\",\"ok\":false}");
        return;
    }

    pthread_mutex_lock(&clients_mutex);
    slot = find_slot(connection);
    if (slot != NULL) {
        memcpy(slot->challenge, challenge, sizeof(challenge));
        if (entry != NULL) {
            memcpy(slot->verifier, entry->verifier, sizeof(slot->verifier));
        } else {
            auth_random_bytes(slot->verifier, sizeof(slot->verifier));
        }
        slot->has_challenge = true;
        slot->authenticated = false;
    }
    pthread_mutex_unlock(&clients_mutex);

    auth_hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    auth_hex_encode(challenge, sizeof(challenge), challenge_hex, sizeof(challenge_hex));
    snprintf(response, sizeof(response),
             "{\"type\":\"auth_challenge\",\"salt\":\"%s\",\"challenge\":\"%s\"}",
             salt_hex, challenge_hex);
    send_json_text(connection, response);
}

static void handle_auth_response(struct mg_connection *connection, const cJSON *message) {
    const cJSON *response = cJSON_GetObjectItemCaseSensitive(message, "response");
    uint8_t response_bytes[SHA256_DIGEST_LENGTH];
    uint8_t expected[SHA256_DIGEST_LENGTH];
    uint8_t verifier[SHA256_DIGEST_LENGTH];
    uint8_t challenge[SHA256_DIGEST_LENGTH];
    size_t response_length = 0;
    client_slot_t *slot;
    bool have_challenge = false;
    bool ok = false;

    if (!cJSON_IsString(response)
            || auth_hex_decode(response->valuestring, response_bytes, sizeof(response_bytes), &response_length) != 0
            || response_length != SHA256_DIGEST_LENGTH) {
        send_json_text(connection, "{\"type\":\"auth_result\",\"ok\":false}");
        return;
    }

    pthread_mutex_lock(&clients_mutex);
    slot = find_slot(connection);
    if (slot != NULL && slot->has_challenge) {
        memcpy(verifier, slot->verifier, sizeof(verifier));
        memcpy(challenge, slot->challenge, sizeof(challenge));
        have_challenge = true;
    }
    pthread_mutex_unlock(&clients_mutex);

    if (have_challenge) {
        auth_compute_response(verifier, challenge, expected);
        ok = auth_bytes_equal(expected, response_bytes, SHA256_DIGEST_LENGTH);
    }

    pthread_mutex_lock(&clients_mutex);
    slot = find_slot(connection);
    if (slot != NULL) {
        slot->has_challenge = false;
        slot->authenticated = ok;
    }
    pthread_mutex_unlock(&clients_mutex);

    send_json_text(connection, ok
        ? "{\"type\":\"auth_result\",\"ok\":true}"
        : "{\"type\":\"auth_result\",\"ok\":false}");
}

static bool connection_is_authenticated(const struct mg_connection *connection) {
    client_slot_t *slot;
    bool authenticated;

    pthread_mutex_lock(&clients_mutex);
    slot = find_slot(connection);
    authenticated = slot != NULL && slot->authenticated;
    pthread_mutex_unlock(&clients_mutex);
    return authenticated;
}

static int websocket_data(struct mg_connection *connection,
                          int bits,
                          char *data,
                          size_t data_length,
                          void *callback_data) {
    cJSON *message;
    const cJSON *type;
    (void)callback_data;

    if ((bits & 0x0F) != MG_WEBSOCKET_OPCODE_TEXT) return 1;

    if (data_length >= MESSAGE_CAPACITY) {
        send_json_text(connection, "{\"type\":\"error\",\"message\":\"mensaje demasiado grande\"}");
        return 1;
    }

    message = cJSON_ParseWithLength(data, data_length);
    type = message != NULL ? cJSON_GetObjectItemCaseSensitive(message, "type") : NULL;
    if (!cJSON_IsString(type)) {
        send_json_text(connection, "{\"type\":\"error\",\"message\":\"mensaje invalido\"}");
        cJSON_Delete(message);
        return 1;
    }

    if (strcmp(type->valuestring, "auth_init") == 0) {
        handle_auth_init(connection, message);
    } else if (strcmp(type->valuestring, "auth_response") == 0) {
        handle_auth_response(connection, message);
    } else if (!connection_is_authenticated(connection)) {
        send_json_text(connection, "{\"type\":\"error\",\"message\":\"no autenticado\"}");
    } else if (!send_to_logic(data, data_length)) {
        send_json_text(connection, "{\"type\":\"error\",\"message\":\"Logica no disponible\"}");
    }

    cJSON_Delete(message);
    return 1;
}

static void websocket_close(const struct mg_connection *connection, void *callback_data) {
    size_t index;
    (void)callback_data;

    pthread_mutex_lock(&clients_mutex);
    for (index = 0; index < MAX_CLIENTS; ++index) {
        if (clients[index].connection == connection) {
            clients[index].connection = NULL;
            clients[index].authenticated = false;
            clients[index].has_challenge = false;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    printf("cliente desconectado\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    char installed_web_root[PATH_MAX];
    const char *web_root = argc > 2 ? argv[2] : default_web_root(installed_web_root);
    pthread_t logic_thread;
    const char *options[] = {
        "listening_ports", argc > 1 ? argv[1] : DEFAULT_PORT,
        "document_root", web_root,
        "enable_directory_listing", "no",
        "enable_keep_alive", "yes",
        "num_threads", "8",
        NULL,
    };
    struct mg_context *context;

    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);
    logic_socket_path = argc > 3 ? argv[3] : DEFAULT_LOGIC_SOCKET;

    const char *users_file = argc > 4 ? argv[4] : DEFAULT_USERS_FILE;
    if (auth_store_load(users_file, &user_store) != 0) {
        fprintf(stderr, "aviso: no se pudo leer %s; se rechazaran todos los inicios de sesion\n", users_file);
    }

    if (mg_init_library(MG_FEATURES_WEBSOCKET) == 0) {
        fprintf(stderr, "no se pudo inicializar CivetWeb\n");
        return EXIT_FAILURE;
    }

    context = mg_start(NULL, NULL, options);
    if (context == NULL) {
        fprintf(stderr, "no se pudo iniciar el servidor\n");
        mg_exit_library();
        return EXIT_FAILURE;
    }

    mg_set_websocket_handler(context,
                             "/ws",
                             websocket_connect,
                             websocket_ready,
                             websocket_data,
                             websocket_close,
                             NULL);

    if (pthread_create(&logic_thread, NULL, logic_reader, NULL) != 0) {
        fprintf(stderr, "no se pudo iniciar el puente con Logica\n");
        mg_stop(context);
        mg_exit_library();
        return EXIT_FAILURE;
    }

    printf("Servidor disponible en http://0.0.0.0:%s\n", options[1]);
    printf("WebSocket disponible en ws://0.0.0.0:%s/ws\n", options[1]);
    printf("IPC de Logica en %s\n", logic_socket_path);
    printf("Usuarios cargados desde %s: %zu\n", users_file, user_store.count);
    fflush(stdout);

    while (keep_running) {
        sleep(1);
        static const char heartbeat[] = "{\"type\":\"heartbeat\"}";
        send_to_logic(heartbeat, sizeof(heartbeat) - 1);
    }

    pthread_mutex_lock(&logic_mutex);
    if (logic_socket >= 0) shutdown(logic_socket, SHUT_RDWR);
    pthread_mutex_unlock(&logic_mutex);
    pthread_join(logic_thread, NULL);
    mg_stop(context);
    mg_exit_library();
    return EXIT_SUCCESS;
}