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

#define DEFAULT_PORT "8080"
#define DEFAULT_WEB_ROOT "../Cliente/dist/scrap-e-controller/browser"
#define DEFAULT_LOGIC_SOCKET "/tmp/roomba-logica.sock"
#define MAX_CLIENTS 16
#define MESSAGE_CAPACITY (256 * 1024)

static volatile sig_atomic_t keep_running = 1;
static struct mg_connection *websocket_clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t logic_mutex = PTHREAD_MUTEX_INITIALIZER;
static int logic_socket = -1;
static const char *logic_socket_path = DEFAULT_LOGIC_SOCKET;

static void stop_server(int signal_number) {
    (void)signal_number;
    keep_running = 0;
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
        if (websocket_clients[index] == NULL) {
            websocket_clients[index] = connection;
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
        if (websocket_clients[index] != NULL) {
            mg_websocket_write(websocket_clients[index], MG_WEBSOCKET_OPCODE_TEXT, data, data_length);
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

static int websocket_data(struct mg_connection *connection,
                          int bits,
                          char *data,
                          size_t data_length,
                          void *callback_data) {
    (void)callback_data;

    if ((bits & 0x0F) == MG_WEBSOCKET_OPCODE_TEXT) {
        if (data_length >= MESSAGE_CAPACITY || !send_to_logic(data, data_length)) {
            static const char unavailable[] = "{\"type\":\"error\",\"message\":\"Logica no disponible\"}";
            mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, unavailable, sizeof(unavailable) - 1);
        }
    }
    return 1;
}

static void websocket_close(const struct mg_connection *connection, void *callback_data) {
    size_t index;
    (void)callback_data;

    pthread_mutex_lock(&clients_mutex);
    for (index = 0; index < MAX_CLIENTS; ++index) {
        if (websocket_clients[index] == connection) websocket_clients[index] = NULL;
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
    fflush(stdout);

    while (keep_running) {
        sleep(1);
    }

    pthread_mutex_lock(&logic_mutex);
    if (logic_socket >= 0) shutdown(logic_socket, SHUT_RDWR);
    pthread_mutex_unlock(&logic_mutex);
    pthread_join(logic_thread, NULL);
    mg_stop(context);
    mg_exit_library();
    return EXIT_SUCCESS;
}