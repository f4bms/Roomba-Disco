#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "auto.h"
#include "cJSON.h"
#include "leds.h"
#include "mapa.h"
#include "odometria.h"
#include "roombateca_control.h"

#define DEFAULT_SOCKET_PATH "/tmp/roomba-logica.sock"
#define DEFAULT_STATE_PATH "Logica/estado.json"
#define MESSAGE_CAPACITY (256 * 1024)
#define CONTROL_TICK_MS 100
#define WATCHDOG_TIMEOUT_NS 2000000000ULL

static volatile sig_atomic_t keep_running = 1;
static int listening_socket = -1;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t last_heartbeat_ns;
static unsigned long state_generation = 1;
static bool watchdog_stopped;
//activar en cada cambio de modo para que AUTO arranque de 0
static bool auto_reset_pending;

struct control_context {
    cJSON *state;
    odometria_t odometria;
    mapa_t mapa;
    bool obstacle_was_active;
    auto_t auto_estado;
    bool auto_activo;
};

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

static void set_number(cJSON *object, const char *name, double value) {
    cJSON *item = cJSON_CreateNumber(value);

    if (item == NULL) return;
    if (cJSON_GetObjectItemCaseSensitive(object, name) != NULL) {
        replace_item(object, name, item);
    } else if (!cJSON_AddItemToObject(object, name, item)) {
        cJSON_Delete(item);
    }
}

static void sync_reported_indicators(cJSON *state) {
    int power = led_get(LED_ENCENDIDO);
    int manual = led_get(LED_MANUAL);
    int autonomous = led_get(LED_AUTONOMO);
    int alert = led_get(LED_ALERTA);
    cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
    const char *mode_indicator = manual == 1 && autonomous == 0 ? "MANUAL"
        : autonomous == 1 && manual == 0 ? "AUTO" : "OFF";
    if (!cJSON_IsObject(reported)) return;
    if (cJSON_GetObjectItemCaseSensitive(reported, "power"))
        replace_item(reported, "power", cJSON_CreateBool(power == 1));
    else cJSON_AddBoolToObject(reported, "power", power == 1);
    if (cJSON_GetObjectItemCaseSensitive(reported, "modeIndicator"))
        replace_item(reported, "modeIndicator", cJSON_CreateString(mode_indicator));
    else cJSON_AddStringToObject(reported, "modeIndicator", mode_indicator);
    if (cJSON_GetObjectItemCaseSensitive(reported, "alertIndicator"))
        replace_item(reported, "alertIndicator", cJSON_CreateBool(alert == 1));
    else cJSON_AddBoolToObject(reported, "alertIndicator", alert == 1);
}

static uint64_t monotonic_now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void stop_state(cJSON *state) {
    cJSON *desired = cJSON_GetObjectItemCaseSensitive(state, "desired");
    cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
    cJSON *desired_motion = cJSON_GetObjectItemCaseSensitive(desired, "motion");
    cJSON *reported_motion = cJSON_GetObjectItemCaseSensitive(reported, "motion");

    roombateca_set_motion("STOP", 0);
    roombateca_aspiradora_set(false);
    if (cJSON_IsObject(desired_motion)) {
        replace_item(desired_motion, "direction", cJSON_CreateString("STOP"));
        replace_item(desired_motion, "speed", cJSON_CreateNumber(0));
    }
    if (cJSON_IsObject(reported_motion)) {
        replace_item(reported_motion, "direction", cJSON_CreateString("STOP"));
        replace_item(reported_motion, "speed", cJSON_CreateNumber(0));
    }
}

static void update_reported_map(cJSON *reported, const mapa_t *mapa) {
    cJSON *reported_map = cJSON_GetObjectItemCaseSensitive(reported, "map");
    cJSON *cells;
    size_t index;

    if (!cJSON_IsObject(reported_map)) {
        reported_map = cJSON_AddObjectToObject(reported, "map");
    }
    if (reported_map == NULL) return;
    set_number(reported_map, "width", mapa->config.width);
    set_number(reported_map, "height", mapa->config.height);
    cells = cJSON_CreateArray();
    if (cells == NULL) return;
    for (index = 0; index < mapa_cantidad_celdas(mapa); ++index) {
        cJSON_AddItemToArray(cells, cJSON_CreateNumber(mapa_obtener_celdas(mapa)[index]));
    }
    if (cJSON_GetObjectItemCaseSensitive(reported_map, "cells") != NULL) {
        replace_item(reported_map, "cells", cells);
    } else if (!cJSON_AddItemToObject(reported_map, "cells", cells)) {
        cJSON_Delete(cells);
    }
}

static bool obstacle_detected(const float distances[SENSOR_CANTIDAD],
                              const bool valid[SENSOR_CANTIDAD]);

static void sync_reported_audio(cJSON *reported) {
    cJSON *audio = cJSON_GetObjectItemCaseSensitive(reported, "audio");
    const char *status;
    audio_estado_t state;

    if (!roombateca_audio_available() || !cJSON_IsObject(audio)) return;
    state = roombateca_audio_get_state();
    status = state == AUDIO_PLAY ? "playing"
        : state == AUDIO_PAUSA ? "paused" : "stopped";
    replace_item(audio, "status", cJSON_CreateString(status));
    if (roombateca_audio_get_volume() >= 0) {
        replace_item(audio, "volume", cJSON_CreateNumber(roombateca_audio_get_volume()));
    }
}
//Parte del modo autónomo
//pregunta si estamos en el estado autónomo y revisa el JSON
static bool state_is_auto(const cJSON *state) {
    const cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(reported, "mode");
    return cJSON_IsString(mode) && strcmp(mode->valuestring, "AUTO") == 0;
}
//para enviar al cliente que se está moviendo, porque sino no se daría cuenta
static void report_motion(cJSON *reported, const auto_orden_t *orden) {
    cJSON *motion = cJSON_GetObjectItemCaseSensitive(reported, "motion");
    if (!cJSON_IsObject(motion)) return;
    replace_item(motion, "direction", cJSON_CreateString(orden->direccion));
    replace_item(motion, "speed", cJSON_CreateNumber(orden->velocidad));
}
//Cuando ya "encuentra" el final del recorrido y se pasa a modo manual 
static void finish_auto(cJSON *state) {
    cJSON *desired = cJSON_GetObjectItemCaseSensitive(state, "desired");
    cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
    cJSON *revision = cJSON_GetObjectItemCaseSensitive(state, "revision");
    stop_state(state);
    if (cJSON_IsObject(desired)) replace_item(desired, "mode", cJSON_CreateString("MANUAL"));
    if (cJSON_IsObject(reported)) replace_item(reported, "mode", cJSON_CreateString("MANUAL"));
    roombateca_set_mode_leds("MANUAL");
    replace_item(state, "revision",
                 cJSON_CreateNumber(cJSON_IsNumber(revision) ? revision->valuedouble + 1 : 1));
}
//se le agrega al control del tick la parte del manejo en automático
static void control_tick(cJSON *state, odometria_t *odometria, mapa_t *mapa,
                         bool *obstacle_was_active,
                         auto_t *auto_estado, bool *auto_activo) { //acá
    float distances[SENSOR_CANTIDAD];
    bool sensor_valid[SENSOR_CANTIDAD];
    encoder_lectura_t encoder_readings[ENCODER_CANTIDAD];
    cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
    cJSON *sensors = cJSON_GetObjectItemCaseSensitive(reported, "sensors");
    int sensor;

    if (!cJSON_IsObject(reported) || !cJSON_IsArray(sensors)) return;
        if (roombateca_read_sensors(distances, sensor_valid) != 0
            || roombateca_read_encoders(encoder_readings) != 0) return;

        {
            bool obstacle_active = obstacle_detected(distances, sensor_valid);
            bool trigger_alert = obstacle_active && !*obstacle_was_active;
            *obstacle_was_active = obstacle_active;
            led_set(LED_ALERTA, obstacle_active);
            if (trigger_alert) {
                //para el mutex de alerta obstáculo
                if (state_is_auto(state)) roombateca_set_motion("STOP", 0);
                pthread_mutex_unlock(&state_mutex);
                roombateca_audio_obstacle_alert();
                pthread_mutex_lock(&state_mutex);
            }
        }

    if (odometria_actualizar(odometria,
                             &encoder_readings[ENCODER_IZQUIERDO],
                             &encoder_readings[ENCODER_DERECHO]) != 0) return;

    const odometria_pose_t *pose = odometria_obtener_pose(odometria);
    if (mapa_actualizar_pose(mapa, pose) != 0) return;
    if (sensor_valid[SENSOR_FRONTAL]) {
        mapa_observar(mapa, pose, 0.0, distances[SENSOR_FRONTAL] * 10.0,
                      distances[SENSOR_FRONTAL] < 20.0f);
    }
    if (sensor_valid[SENSOR_TRASERO]) {
        mapa_observar(mapa, pose, 3.14159265358979323846, distances[SENSOR_TRASERO] * 10.0,
                      distances[SENSOR_TRASERO] < 20.0f);
    }
	//para manejo de bandera de cambio de modo
    if (auto_reset_pending) {
        *auto_activo = false;
        auto_reset_pending = false;
    }
	//Si todavía esta en modo Auto y el cliente está up, aunque este en Auto no se mueve
    if (state_is_auto(state) && !watchdog_stopped) {
        auto_orden_t orden;
        if (!*auto_activo) {
            auto_reset(auto_estado);
            *auto_activo = true;
        }
		//para el auto paso recibe todas las lecturas de los sensores
        orden = auto_paso(auto_estado, pose,
                              distances[SENSOR_FRONTAL], sensor_valid[SENSOR_FRONTAL],
                              monotonic_now_ns());
        if (auto_fin(auto_estado)) {
            // TErmina el recorrido, entonces frena y devuelve el control al modo manual.
            finish_auto(state);
            *auto_activo = false;
        } else if (roombateca_set_motion(orden.direccion, orden.velocidad) == 0) {
            report_motion(reported, &orden);
            roombateca_aspiradora_set(orden.aspirar);
        }
    } else {
        *auto_activo = false;
    }

    for (sensor = 0; sensor < SENSOR_CANTIDAD && sensor < cJSON_GetArraySize(sensors); ++sensor) {
        cJSON *reading = cJSON_GetArrayItem(sensors, sensor);
        if (!cJSON_IsObject(reading)) continue;
        if (sensor_valid[sensor]) {
            replace_item(reading, "distanceCm", cJSON_CreateNumber(distances[sensor]));
            replace_item(reading, "obstacle", cJSON_CreateBool(distances[sensor] < 20.0f));
        }
    }

    cJSON *reported_pose = cJSON_GetObjectItemCaseSensitive(reported, "pose");
    if (!cJSON_IsObject(reported_pose)) {
        reported_pose = cJSON_AddObjectToObject(reported, "pose");
    }
    if (reported_pose != NULL && pose != NULL) {
        set_number(reported_pose, "xMm", pose->x_mm);
        set_number(reported_pose, "yMm", pose->y_mm);
        set_number(reported_pose, "thetaRad", pose->theta_rad);
    }
    sync_reported_audio(reported);
    update_reported_map(reported, mapa);
}

static bool obstacle_detected(const float distances[SENSOR_CANTIDAD],
                              const bool valid[SENSOR_CANTIDAD]) {
    int sensor;

    for (sensor = 0; sensor < SENSOR_CANTIDAD; ++sensor) {
        if (valid[sensor] && distances[sensor] < 20.0f) return true;
    }
    return false;
}

static void *control_thread_main(void *argument) {
    struct control_context *context = argument;

    while (keep_running) {
        struct timespec interval = {
            .tv_sec = CONTROL_TICK_MS / 1000,
            .tv_nsec = (CONTROL_TICK_MS % 1000) * 1000000L,
        };

        nanosleep(&interval, NULL);
        if (!keep_running) break;
        pthread_mutex_lock(&state_mutex);
        {
            char *before = cJSON_PrintUnformatted(context->state);
            if (!watchdog_stopped && monotonic_now_ns() - last_heartbeat_ns > WATCHDOG_TIMEOUT_NS) {
                stop_state(context->state);
                watchdog_stopped = true;
            }
            control_tick(context->state, &context->odometria, &context->mapa,
                         &context->obstacle_was_active,
                         &context->auto_estado, &context->auto_activo);
            sync_reported_indicators(context->state);
            char *after = cJSON_PrintUnformatted(context->state);
            if (before != NULL && after != NULL && strcmp(before, after) != 0) state_generation++;
            free(before);
            free(after);
        }
        pthread_mutex_unlock(&state_mutex);
    }
    return NULL;
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
		//compara el modo del JSON vs el pedido a ver si es MANUAL o AUTO
        const cJSON *current_mode = cJSON_GetObjectItemCaseSensitive(reported, "mode");
        bool mode_changed = !cJSON_IsString(current_mode)
            || strcmp(current_mode->valuestring, mode->valuestring) != 0;
        // Cambio de modo, se frena luego se actualiza: el recorrido automatico empieza en cero
        if (mode_changed) {
            stop_state(state);
            auto_reset_pending = true;
            roombateca_audio_notify_mode(mode->valuestring);
        }
        replace_item(desired, "mode", cJSON_Duplicate(mode, true));
        replace_item(reported, "mode", cJSON_Duplicate(mode, true));
        roombateca_set_mode_leds(mode->valuestring);
        changed = true;
    }

    //En AUTO el movimiento lo decide auto_paso() y se ignoran los comandos manuales.
    if (cJSON_IsObject(motion) && !state_is_auto(state)) {
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
                cJSON *track = cJSON_GetObjectItemCaseSensitive(reported_audio, "track");
                cJSON *tracks = cJSON_GetObjectItemCaseSensitive(reported_audio, "tracks");
                int requested_track = cJSON_IsNumber(track) ? track->valueint : 0;
                int count = cJSON_IsArray(tracks) ? cJSON_GetArraySize(tracks) : 0;
                int audio_result = -1;
                const char *status = NULL;

                if (strcmp(action->valuestring, "PLAY") == 0 && count > 0) {
                    audio_result = roombateca_audio_play_track(requested_track);
                    status = "playing";
                } else if (strcmp(action->valuestring, "PAUSE") == 0) {
                    audio_result = roombateca_audio_pause();
                    status = "paused";
                } else if (strcmp(action->valuestring, "STOP") == 0) {
                    audio_result = roombateca_audio_stop();
                    status = "stopped";
                } else if ((strcmp(action->valuestring, "NEXT") == 0
                            || strcmp(action->valuestring, "PREV") == 0)
                        && count > 0) {
                    int count = cJSON_GetArraySize(tracks);
                    int next_track = strcmp(action->valuestring, "NEXT") == 0
                        ? (track->valueint + 1) % count
                        : (track->valueint + count - 1) % count;
                    audio_result = roombateca_audio_play_track(next_track);
                    if (audio_result == 0) replace_item(reported_audio, "track", cJSON_CreateNumber(next_track));
                    status = "playing";
                }
                if (audio_result == 0) {
                    replace_item(desired_audio, "action", cJSON_Duplicate(action, true));
                    if (status != NULL) replace_item(reported_audio, "status", cJSON_CreateString(status));
                    changed = true;
                }
            }
            if (cJSON_IsNumber(volume) && volume->valuedouble >= 0 && volume->valuedouble <= 100) {
                if (roombateca_audio_set_volume(volume->valueint) == 0) {
                    replace_item(desired_audio, "volume", cJSON_CreateNumber(volume->valueint));
                    replace_item(reported_audio, "volume", cJSON_CreateNumber(volume->valueint));
                    changed = true;
                }
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
    pthread_mutex_lock(&state_mutex);
    type = cJSON_GetObjectItemCaseSensitive(request, "type");
    patch = cJSON_GetObjectItemCaseSensitive(request, "desired");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "heartbeat") == 0) {
        last_heartbeat_ns = monotonic_now_ns();
        watchdog_stopped = false;
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "get_state") == 0) {
        sync_reported_indicators(state);
        send_state(client_socket, state);
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "set_state") == 0 && cJSON_IsObject(patch)) {
        if (apply_desired_state(state, patch)) {
            sync_reported_indicators(state);
            if (!write_state(state_path, state)) perror("no se pudo guardar estado.json");
            state_generation++;
            send_state(client_socket, state);
        }
    }
    pthread_mutex_unlock(&state_mutex);
    cJSON_Delete(request);
}

static void serve_client(int client_socket, cJSON *state, const char *state_path) {
    char buffer[MESSAGE_CAPACITY];
    size_t used = 0;
    struct pollfd client_poll = {
        .fd = client_socket,
        .events = POLLIN,
    };
    unsigned long last_sent_generation;

    pthread_mutex_lock(&state_mutex);
    last_heartbeat_ns = monotonic_now_ns();
    watchdog_stopped = false;
    sync_reported_indicators(state);
    if (!send_state(client_socket, state)) {
        pthread_mutex_unlock(&state_mutex);
        return;
    }
    pthread_mutex_unlock(&state_mutex);
    last_sent_generation = state_generation;
    while (keep_running) {
        int poll_result = poll(&client_poll, 1, CONTROL_TICK_MS);
        char *line_start;
        char *newline;

        if (poll_result < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_result == 0) {
            bool pushed;
            pthread_mutex_lock(&state_mutex);
            pushed = state_generation != last_sent_generation
                ? send_state(client_socket, state)
                : true;
            if (pushed) last_sent_generation = state_generation;
            pthread_mutex_unlock(&state_mutex);
            if (!pushed) break;
            continue;
        }
        if ((client_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) break;

        ssize_t received = recv(client_socket, buffer + used, sizeof(buffer) - used - 1, 0);
        if (received <= 0) break;
        used += (size_t)received;
        buffer[used] = '\0';
        line_start = buffer;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            if (*line_start != '\0') process_message(client_socket, state, state_path, line_start);
            pthread_mutex_lock(&state_mutex);
            last_sent_generation = state_generation;
            pthread_mutex_unlock(&state_mutex);
            line_start = newline + 1;
        }
        used -= (size_t)(line_start - buffer);
        memmove(buffer, line_start, used);
        if (used == sizeof(buffer) - 1) used = 0;
    }
    pthread_mutex_lock(&state_mutex);
    stop_state(state);
	//si el cliente no hace ping el modo AUTO no se mueve
    watchdog_stopped = true;  
    state_generation++;
    pthread_mutex_unlock(&state_mutex);
}

int main(int argc, char **argv) {
    const char *state_path = argc > 1 ? argv[1] : DEFAULT_STATE_PATH;
    const char *socket_path = argc > 2 ? argv[2] : DEFAULT_SOCKET_PATH;
    struct sockaddr_un address = {0};
    pthread_t control_thread;
    struct control_context control = {0};
    bool control_thread_started = false;
    char *state_text = read_file(state_path);
    cJSON *state;

    if (state_text == NULL || (state = cJSON_Parse(state_text)) == NULL) {
        fprintf(stderr, "no se pudo leer el estado JSON: %s\n", state_path);
        free(state_text);
        return EXIT_FAILURE;
    }
    free(state_text);
    control.state = state;
    last_heartbeat_ns = monotonic_now_ns();
    if (odometria_init(&control.odometria,
                       (odometria_config_t){.distancia_ruedas_mm = 200.0}) != 0) {
        fprintf(stderr, "no se pudo inicializar la odometria\n");
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }
    if (mapa_init(&control.mapa, (mapa_config_t){
            .width = 8,
            .height = 6,
            .resolution_mm = 100.0,
            .origin_x = 4,
            .origin_y = 3,
        }) != 0) {
        fprintf(stderr, "no se pudo inicializar el mapa\n");
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }
    {
        cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
        cJSON *pose = cJSON_GetObjectItemCaseSensitive(reported, "pose");
        if (cJSON_IsObject(reported)) {
            update_reported_map(reported, &control.mapa);
            if (!cJSON_IsObject(pose)) pose = cJSON_AddObjectToObject(reported, "pose");
            if (cJSON_IsObject(pose)) {
                set_number(pose, "xMm", 0.0);
                set_number(pose, "yMm", 0.0);
                set_number(pose, "thetaRad", 0.0);
            }
        }
    }

    if (roombateca_control_init() != 0) {
        fprintf(stderr, "no se pudo inicializar el control de motores\n");
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }
    auto_init(&control.auto_estado);

    {
        cJSON *reported = cJSON_GetObjectItemCaseSensitive(state, "reported");
        cJSON *mode = cJSON_GetObjectItemCaseSensitive(reported, "mode");
        if (cJSON_IsString(mode)) roombateca_set_mode_leds(mode->valuestring);
    }
    sync_reported_indicators(state);

    signal(SIGINT, stop_logic);
    signal(SIGTERM, stop_logic);
    signal(SIGPIPE, SIG_IGN);

    if (pthread_create(&control_thread, NULL, control_thread_main, &control) != 0) {
        fprintf(stderr, "no se pudo iniciar el ciclo de control\n");
        roombateca_control_cleanup();
        mapa_cleanup(&control.mapa);
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }
    control_thread_started = true;

    listening_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listening_socket < 0) {
        perror("socket");
        keep_running = 0;
        if (control_thread_started) pthread_join(control_thread, NULL);
        roombateca_control_cleanup();
        mapa_cleanup(&control.mapa);
        cJSON_Delete(state);
        return EXIT_FAILURE;
    }
    address.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "ruta de socket demasiado larga\n");
        keep_running = 0;
        if (control_thread_started) pthread_join(control_thread, NULL);
        roombateca_control_cleanup();
        mapa_cleanup(&control.mapa);
        cJSON_Delete(state);
        close(listening_socket);
        return EXIT_FAILURE;
    }
    strcpy(address.sun_path, socket_path);
    unlink(socket_path);
    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listening_socket, 4) != 0) {
        perror("bind/listen");
        keep_running = 0;
        if (control_thread_started) pthread_join(control_thread, NULL);
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

    keep_running = 0;
    if (control_thread_started) pthread_join(control_thread, NULL);
    cJSON_Delete(state);
    mapa_cleanup(&control.mapa);
    roombateca_control_cleanup();
    if (listening_socket >= 0) close(listening_socket);
    unlink(socket_path);
    return EXIT_SUCCESS;
}
