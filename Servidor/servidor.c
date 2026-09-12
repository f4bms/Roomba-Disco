#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "civetweb.h"

#define DEFAULT_PORT "8080"
#define DEFAULT_WEB_ROOT "../Cliente/dist/scrap-e-controller/browser"

static volatile sig_atomic_t keep_running = 1;

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
    (void)connection;
    (void)callback_data;
    printf("cliente conectado\n");
    fflush(stdout);
}

static int message_starts_with(const char *data, size_t data_length, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    return data_length >= prefix_length && memcmp(data, prefix, prefix_length) == 0;
}

static void log_client_action(const char *data, size_t data_length) {
    if (message_starts_with(data, data_length, "LOGIN:")) {
        printf("[debug] solicitud de inicio de sesion recibida\n");
    } else if (message_starts_with(data, data_length, "MODE:")) {
        printf("[debug] boton de modo presionado: %.*s\n",
               (int)(data_length - strlen("MODE:")),
               data + strlen("MODE:"));
    } else if (message_starts_with(data, data_length, "CMD:")) {
        printf("[debug] control direccional: %.*s\n",
               (int)(data_length - strlen("CMD:")),
               data + strlen("CMD:"));
    } else if (message_starts_with(data, data_length, "AUDIO:")) {
        printf("[debug] boton de audio presionado: %.*s\n",
               (int)(data_length - strlen("AUDIO:")),
               data + strlen("AUDIO:"));
    } else {
        printf("[debug] mensaje no reconocido: %.*s\n", (int)data_length, data);
    }
    fflush(stdout);
}

static int websocket_data(struct mg_connection *connection,
                          int bits,
                          char *data,
                          size_t data_length,
                          void *callback_data) {
    (void)callback_data;

    if ((bits & 0x0F) == MG_WEBSOCKET_OPCODE_TEXT) {
        log_client_action(data, data_length);
        mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, data, data_length);
    }
    return 1;
}

static void websocket_close(const struct mg_connection *connection, void *callback_data) {
    (void)connection;
    (void)callback_data;
    printf("cliente desconectado\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    char installed_web_root[PATH_MAX];
    const char *web_root = argc > 2 ? argv[2] : default_web_root(installed_web_root);
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

    printf("Servidor disponible en http://0.0.0.0:%s\n", options[1]);
    printf("WebSocket disponible en ws://0.0.0.0:%s/ws\n", options[1]);
    fflush(stdout);

    while (keep_running) {
        sleep(1);
    }

    mg_stop(context);
    mg_exit_library();
    return EXIT_SUCCESS;
}