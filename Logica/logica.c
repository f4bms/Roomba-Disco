#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"
#include "roombateca_control.h"

#define DEFAULT_SOCKET_PATH "/tmp/roomba-logica.sock"
#define DEFAULT_STATE_PATH "Logica/estado.json"
#define MESSAGE_CAPACITY (256 * 1024)

static volatile sig_atomic_t keep_running = 1;
static int listening_socket = -1;

static void stop_logic(int signal_number) {
    (void)signal_number;
    keep_running = 0;
    if (listening_socket >= 0) {
        close(listening_socket);
        listening_socket = -1;
    }
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long length;
    char *content;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    content = malloc((size_t)length + 1);
    if (content == NULL || fread(content, 1, (size_t)length, file) != (size_t)length) {
        free(content);
        fclose(file);
        return NULL;
    }
    content[length] = '\0';
    fclose(file);
    return content;
}

static bool write_state(const char *path, const cJSON *state) {
    char temporary_path[PATH_MAX];
    char *json = cJSON_Print(state);
    FILE *file;
    bool success;

    if (json == NULL || snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) >= (int)sizeof(temporary_path)) {
        free(json);
        return false;
    }
    file = fopen(temporary_path, "wb");
    if (file == NULL) {
        free(json);
        return false;
    }
    success = fprintf(file, "%s\n", json) >= 0 && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) success = false;
    if (success) success = rename(temporary_path, path) == 0;
    if (!success) unlink(temporary_path);
    free(json);
    return success;
}

static bool string_is_one_of(const cJSON *item, const char *const values[], size_t count) {
    size_t index;
    if (!cJSON_IsString(item)) return false;
    for (index = 0; index < count; ++index) {
        if (strcmp(item->valuestring, values[index]) == 0) return true;
    }
    return false;
}

static void replace_item(cJSON *object, const char *name, cJSON *replacement) {
    if (replacement != NULL) cJSON_ReplaceItemInObjectCaseSensitive(object, name, replacement);
}

static bool apply_desired_state(cJSON *state, const cJSON *patch) {
    static const char *const modes[] = {"AUTO", "MANUAL"};
    static const char *const directions[] = {"FWD", "BACK", "TURN_L", "TURN_R", "STOP"};
    static const char *const actions[] = {"PLAY", "PAUSE", "STOP", "NEXT", "PREV"};
    cJSON *desired = cJSON_GetObjectItemCaseSensitive(state, "desired");
    cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(patch, "mode");
    const cJSON *motion = cJSON_GetObjectItemCaseSensitive(patch, "motion");
    const cJSON *audio = cJSON_GetObjectItemCaseSensitive(patch, "audio");
    bool changed = false;

    if (!cJSON_IsObject(desired) || !cJSON_IsObject(reported)) return false;

    if (string_is_one_of(mode, modes, 2)) {
        replace_item(desired, "mode", cJSON_Duplicate(mode, true));
        replace_item(reported, "mode", cJSON_Duplicate(mode, true));
        changed = true;
    }

    if (cJSON_IsObject(motion)) {
        cJSON *desired_motion = cJSON_GetObjectItemCaseSensitive(desired, "motion");
        cJSON *reported_motion = cJSON_GetObjectItemCaseSensitive(reported, "motion");
        const cJSON *direction = cJSON_GetObjectItemCaseSensitive(motion, "direction");
        const cJSON *speed = cJSON_GetObjectItemCaseSensitive(motion, "speed");
        if (cJSON_IsObject(desired_motion) && cJSON_IsObject(reported_motion)) {
            bool valid_direction = string_is_one_of(direction, directions, 5);
            bool valid_speed = cJSON_IsNumber(speed) && speed->valuedouble >= 0 && speed->valuedouble <= 1000;
            cJSON *current_direction = cJSON_GetObjectItemCaseSensitive(desired_motion, "direction");
            cJSON *current_speed = cJSON_GetObjectItemCaseSensitive(desired_motion, "speed");
            const char *requested_direction = valid_direction
                ? direction->valuestring
                : (cJSON_IsString(current_direction) ? current_direction->valuestring : NULL);
            int requested_speed = valid_speed
                ? speed->valueint
                : (cJSON_IsNumber(current_speed) ? current_speed->valueint : -1);

            if ((valid_direction || valid_speed)
                    && requested_direction != NULL
                    && requested_speed >= 0
                    && roombateca_set_motion(requested_direction, requested_speed) == 0) {
                if (valid_direction) {
                    replace_item(desired_motion, "direction", cJSON_Duplicate(direction, true));
                    replace_item(reported_motion, "direction", cJSON_Duplicate(direction, true));
                }
                if (valid_speed) {
                    replace_item(desired_motion, "speed", cJSON_CreateNumber(speed->valueint));
                    replace_item(reported_motion, "speed", cJSON_CreateNumber(speed->valueint));
                }
                changed = true;
            }
        }
    }

    if (cJSON_IsObject(audio)) {
        cJSON *desired_audio = cJSON_GetObjectItemCaseSensitive(desired, "audio");
        cJSON *reported_audio = cJSON_GetObjectItemCaseSensitive(reported, "audio");
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(audio, "action");
        const cJSON *volume = cJSON_GetObjectItemCaseSensitive(audio, "volume");
        if (cJSON_IsObject(desired_audio) && cJSON_IsObject(reported_audio)) {
            if (string_is_one_of(action, actions, 5)) {
                const char *status = NULL;
                cJSON *track = cJSON_GetObjectItemCaseSensitive(reported_audio, "track");
                cJSON *tracks = cJSON_GetObjectItemCaseSensitive(reported_audio, "tracks");
                replace_item(desired_audio, "action", cJSON_Duplicate(action, true));
                if (strcmp(action->valuestring, "PLAY") == 0) status = "playing";
                else if (strcmp(action->valuestring, "PAUSE") == 0) status = "paused";
                else if (strcmp(action->valuestring, "STOP") == 0) status = "stopped";
                else if (cJSON_IsNumber(track) && cJSON_IsArray(tracks) && cJSON_GetArraySize(tracks) > 0) {
                    int count = cJSON_GetArraySize(tracks);
                    int next_track = strcmp(action->valuestring, "NEXT") == 0
                        ? (track->valueint + 1) % count
                        : (track->valueint + count - 1) % count;
                    replace_item(reported_audio, "track", cJSON_CreateNumber(next_track));
                }
                if (status != NULL) replace_item(reported_audio, "status", cJSON_CreateString(status));
                changed = true;
            }
            if (cJSON_IsNumber(volume) && volume->valuedouble >= 0 && volume->valuedouble <= 100) {
                replace_item(desired_audio, "volume", cJSON_CreateNumber(volume->valueint));
                replace_item(reported_audio, "volume", cJSON_CreateNumber(volume->valueint));
                changed = true;
            }
        }
    }

    if (changed) {
        cJSON *revision = cJSON_GetObjectItemCaseSensitive(state, "revision");
        replace_item(state, "revision", cJSON_CreateNumber(cJSON_IsNumber(revision) ? revision->valuedouble + 1 : 1));
    }
    return changed;
}

static bool send_state(int client_socket, const cJSON *state) {
    char *json = cJSON_PrintUnformatted(state);
    size_t total;
    size_t sent = 0;
    if (json == NULL) return false;
    total = strlen(json);
    while (sent < total) {
        ssize_t result = send(client_socket, json + sent, total - sent, MSG_NOSIGNAL);
        if (result <= 0) {
            free(json);
            return false;
        }
        sent += (size_t)result;
    }
    if (send(client_socket, "\n", 1, MSG_NOSIGNAL) != 1) {
        free(json);
        return false;
    }
    free(json);
    return true;
}

static void process_message(int client_socket, cJSON *state, const char *state_path, const char *message) {
    cJSON *request = cJSON_Parse(message);
    const cJSON *type;
    const cJSON *patch;

    if (request == NULL) return;
    type = cJSON_GetObjectItemCaseSensitive(request, "type");
    patch = cJSON_GetObjectItemCaseSensitive(request, "desired");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "get_state") == 0) {
        send_state(client_socket, state);
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "set_state") == 0 && cJSON_IsObject(patch)) {
        if (apply_desired_state(state, patch)) {
            if (!write_state(state_path, state)) perror("no se pudo guardar estado.json");
            send_state(client_socket, state);
        }
    }
    cJSON_Delete(request);
}

static void serve_client(int client_socket, cJSON *state, const char *state_path) {
    char buffer[MESSAGE_CAPACITY];
    size_t used = 0;

    if (!send_state(client_socket, state)) return;
    while (keep_running) {
        ssize_t received = recv(client_socket, buffer + used, sizeof(buffer) - used - 1, 0);
        char *line_start;
        char *newline;
        if (received <= 0) break;
        used += (size_t)received;
        buffer[used] = '\0';
        line_start = buffer;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            if (*line_start != '\0') process_message(client_socket, state, state_path, line_start);
            line_start = newline + 1;
        }
        used -= (size_t)(line_start - buffer);
        memmove(buffer, line_start, used);
        if (used == sizeof(buffer) - 1) used = 0;
    }
}

int main(int argc, char **argv) {
    const char *state_path = argc > 1 ? argv[1] : DEFAULT_STATE_PATH;
    const char *socket_path = argc > 2 ? argv[2] : DEFAULT_SOCKET_PATH;
    struct sockaddr_un address = {0};
    char *state_text = read_file(state_path);
    cJSON *state;

    if (state_text == NULL || (state = cJSON_Parse(state_text)) == NULL) {
        fprintf(stderr, "no se pudo leer el estado JSON: %s\n", state_path);
        free(state_text);
        return EXIT_FAILURE;
    }
    free(state_text);

    if (roombateca_control_init() != 0) {
        fprintf(stderr, "no se pudo inicializar el control de motores\n");
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }

    signal(SIGINT, stop_logic);
    signal(SIGTERM, stop_logic);
    signal(SIGPIPE, SIG_IGN);

    listening_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listening_socket < 0) {
        perror("socket");
        roombateca_control_cleanup();
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }
    address.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "ruta de socket demasiado larga\n");
        roombateca_control_cleanup();
        cJSON_Delete(state);
        close(listening_socket);
        return EXIT_FAILURE;
    }
    strcpy(address.sun_path, socket_path);
    unlink(socket_path);
    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listening_socket, 4) != 0) {
        perror("bind/listen");
        roombateca_control_cleanup();
        cJSON_Delete(state);
        close(listening_socket);
        unlink(socket_path);
        return EXIT_FAILURE;
    }

    printf("Logica disponible en %s usando %s\n", socket_path, state_path);
    fflush(stdout);
    while (keep_running) {
        int client_socket = accept(listening_socket, NULL, NULL);
        if (client_socket < 0) {
            if (keep_running && errno != EINTR) perror("accept");
            continue;
        }
        serve_client(client_socket, state, state_path);
        close(client_socket);
    }

    cJSON_Delete(state);
    roombateca_control_cleanup();
    if (listening_socket >= 0) close(listening_socket);
    unlink(socket_path);
    return EXIT_SUCCESS;
}
