#include "history_logger.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "history_fs.h"
#include "telemetry_json.h"

static const char *TAG = "history_logger";

#define HISTORY_LOGGER_MAX_LINE_LENGTH 512U

#ifndef CONFIG_TINYBMS_HISTORY_ENABLE
#define CONFIG_TINYBMS_HISTORY_ENABLE 1
#endif
#ifndef CONFIG_TINYBMS_HISTORY_DIR
#define CONFIG_TINYBMS_HISTORY_DIR "/history"
#endif
#ifndef CONFIG_TINYBMS_HISTORY_QUEUE_LENGTH
#define CONFIG_TINYBMS_HISTORY_QUEUE_LENGTH 32
#endif

#ifndef CONFIG_TINYBMS_HISTORY_TASK_STACK
#define CONFIG_TINYBMS_HISTORY_TASK_STACK 4096
#endif

#ifndef CONFIG_TINYBMS_HISTORY_TASK_PRIORITY
#define CONFIG_TINYBMS_HISTORY_TASK_PRIORITY 4
#endif

#ifndef CONFIG_TINYBMS_HISTORY_ARCHIVE_MAX_SAMPLES
#define CONFIG_TINYBMS_HISTORY_ARCHIVE_MAX_SAMPLES 1024
#endif

#ifndef CONFIG_TINYBMS_HISTORY_RETENTION_DAYS
#define CONFIG_TINYBMS_HISTORY_RETENTION_DAYS 30
#endif

#ifndef CONFIG_TINYBMS_HISTORY_MAX_BYTES
#define CONFIG_TINYBMS_HISTORY_MAX_BYTES (2 * 1024 * 1024)
#endif
#ifndef CONFIG_TINYBMS_HISTORY_FLUSH_INTERVAL
#define CONFIG_TINYBMS_HISTORY_FLUSH_INTERVAL 10
#endif
#ifndef CONFIG_TINYBMS_HISTORY_RETENTION_CHECK_INTERVAL
#define CONFIG_TINYBMS_HISTORY_RETENTION_CHECK_INTERVAL 120
#endif

static QueueHandle_t s_queue = NULL;
static FILE *s_active_file = NULL;
static char s_active_filename[64] = {0};
static int s_active_day = -1;
static bool s_directory_ready = false;
static volatile bool s_task_should_exit = false;

// Buffer de retry pour récupération d'erreurs d'écriture
#define HISTORY_RETRY_BUFFER_SIZE 32
static char s_retry_buffer[HISTORY_RETRY_BUFFER_SIZE][512];
static size_t s_retry_buffer_count = 0;
static SemaphoreHandle_t s_retry_mutex = NULL;

// Cache pour la liste de fichiers (évite scan FS répétitif)
#define FILE_LIST_CACHE_TTL_MS 30000  // Rafraîchir toutes les 30 secondes
typedef struct {
    history_logger_file_info_t *files;
    size_t count;
    uint64_t cached_at_ms;
    bool valid;
    bool mounted;
} file_list_cache_t;
static file_list_cache_t s_file_cache = {0};
static SemaphoreHandle_t s_cache_mutex = NULL;

static int history_logger_compare_file_info(const void *lhs, const void *rhs)
{
    const history_logger_file_info_t *a = (const history_logger_file_info_t *)lhs;
    const history_logger_file_info_t *b = (const history_logger_file_info_t *)rhs;

    if (a == NULL || b == NULL) {
        return 0;
    }

    time_t a_time = a->modified_time;
    time_t b_time = b->modified_time;

    if (a_time <= 0 && b_time > 0) {
        return 1;
    }
    if (b_time <= 0 && a_time > 0) {
        return -1;
    }

    if (a_time != b_time) {
        return (a_time > b_time) ? -1 : 1;
    }

    return strcasecmp(a->name, b->name);
}

void history_logger_set_event_publisher(event_bus_publish_fn_t publisher)
{
    (void)publisher;
}

const char *history_logger_directory(void)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    return CONFIG_TINYBMS_HISTORY_DIR;
#else
    return CONFIG_TINYBMS_HISTORY_DIR;
#endif
}

static esp_err_t history_logger_ensure_directory(void)
{
    if (!history_fs_is_mounted()) {
        s_directory_ready = false;
        return ESP_ERR_INVALID_STATE;
    }

    if (s_directory_ready) {
        return ESP_OK;
    }

    struct stat st = {0};
    if (stat(CONFIG_TINYBMS_HISTORY_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            s_directory_ready = true;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "History path exists but is not a directory: %s", CONFIG_TINYBMS_HISTORY_DIR);
        return ESP_FAIL;
    }

    if (mkdir(CONFIG_TINYBMS_HISTORY_DIR, 0775) != 0) {
        if (errno == EEXIST) {
            s_directory_ready = true;
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Unable to create history directory %s: errno=%d", CONFIG_TINYBMS_HISTORY_DIR, errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created history directory at %s", CONFIG_TINYBMS_HISTORY_DIR);
    s_directory_ready = true;
    return ESP_OK;
}

static void history_logger_close_active_file(void)
{
    if (s_active_file != NULL) {
        fclose(s_active_file);
        s_active_file = NULL;
        s_active_filename[0] = '\0';
        s_active_day = -1;
    }
}

static int history_logger_compute_day(time_t now)
{
    if (now <= 0) {
        return -1;
    }

    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    return tm_now.tm_yday + (tm_now.tm_year * 366);
}

static void history_logger_format_identifier(time_t now, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    if (now > 0) {
        struct tm tm_now;
        gmtime_r(&now, &tm_now);
        strftime(buffer, size, "%Y%m%d", &tm_now);
        return;
    }

    uint64_t monotonic_ms = (uint64_t)(esp_timer_get_time() / 1000LL);
    snprintf(buffer, size, "session-%" PRIu64, monotonic_ms);
}

static esp_err_t history_logger_open_file(time_t now)
{
    if (!history_fs_is_mounted()) {
        history_logger_close_active_file();
        return ESP_ERR_INVALID_STATE;
    }

    if (history_logger_ensure_directory() != ESP_OK) {
        return ESP_FAIL;
    }

    char identifier[32];
    history_logger_format_identifier(now, identifier, sizeof(identifier));

    char filename[sizeof(s_active_filename)];
    int written = snprintf(filename, sizeof(filename), "history-%s.jsonl", identifier);
    if (written < 0 || written >= (int)sizeof(filename)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int current_day = history_logger_compute_day(now);
    if (s_active_file != NULL && strcmp(filename, s_active_filename) == 0) {
        if (current_day >= 0) {
            s_active_day = current_day;
        }
        return ESP_OK;
    }

    history_logger_close_active_file();

    char path[256];
    int path_written = snprintf(path, sizeof(path), "%s/%s", CONFIG_TINYBMS_HISTORY_DIR, filename);
    if (path_written < 0 || path_written >= (int)sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Unable to open history file %s", path);
        return ESP_FAIL;
    }

    strlcpy(s_active_filename, filename, sizeof(s_active_filename));
    s_active_file = file;
    s_active_day = current_day;

    return ESP_OK;
}

static void history_logger_write_sample(FILE *file, time_t now, const uart_bms_live_data_t *sample)
{
    if (file == NULL || sample == NULL) {
        return;
    }

    char line[HISTORY_LOGGER_MAX_LINE_LENGTH];
    if (!telemetry_json_write_history_sample(sample, now, line, sizeof(line), NULL)) {
        ESP_LOGW(TAG, "Failed to serialize history sample");
        return;
    }

    if (fprintf(file, "%s\n", line) < 0) {
        ESP_LOGW(TAG, "Failed to write line");

        // Ajouter au buffer de retry si pas plein
        if (s_retry_mutex != NULL && xSemaphoreTake(s_retry_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (s_retry_buffer_count < HISTORY_RETRY_BUFFER_SIZE) {
                strlcpy(s_retry_buffer[s_retry_buffer_count], line, sizeof(s_retry_buffer[0]));
                s_retry_buffer_count++;
                ESP_LOGI(TAG, "Buffered failed write for retry (%zu in queue)",
                        s_retry_buffer_count);
            } else {
                ESP_LOGW(TAG, "Retry buffer full, dropping sample");
            }
            xSemaphoreGive(s_retry_mutex);
        }
    }
}

static void history_logger_remove_file(const char *name)
{
    if (name == NULL) {
        return;
    }

    char path[256];
    if (history_logger_resolve_path(name, path, sizeof(path)) != ESP_OK) {
        return;
    }

    if (unlink(path) == 0) {
        ESP_LOGI(TAG, "Removed history archive %s", path);
        // Invalider le cache car la liste de fichiers a changé
        history_logger_invalidate_cache();
    } else {
        ESP_LOGW(TAG, "Failed to remove archive %s: errno=%d", path, errno);
    }
}

static void history_logger_enforce_retention(time_t now)
{
#if CONFIG_TINYBMS_HISTORY_RETENTION_DAYS > 0 || CONFIG_TINYBMS_HISTORY_MAX_BYTES > 0
    history_logger_file_info_t *files = NULL;
    size_t count = 0;
    bool mounted = false;
    if (history_logger_list_files(&files, &count, &mounted) != ESP_OK || !mounted) {
        return;
    }

    size_t total_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        total_bytes += files[i].size_bytes;
    }

#if CONFIG_TINYBMS_HISTORY_RETENTION_DAYS > 0
    if (now > 0) {
        time_t cutoff = now - (time_t)CONFIG_TINYBMS_HISTORY_RETENTION_DAYS * 24 * 3600;
        for (size_t i = 0; i < count; ++i) {
            if (files[i].modified_time > 0 && files[i].modified_time < cutoff) {
                if (strcmp(files[i].name, s_active_filename) != 0) {
                    history_logger_remove_file(files[i].name);
                    if (total_bytes >= files[i].size_bytes) {
                        total_bytes -= files[i].size_bytes;
                    }
                    files[i].size_bytes = 0;
                }
            }
        }
    }
#endif

#if CONFIG_TINYBMS_HISTORY_MAX_BYTES > 0
    const size_t max_bytes = CONFIG_TINYBMS_HISTORY_MAX_BYTES;
    while (total_bytes > max_bytes) {
        size_t oldest_index = SIZE_MAX;
        time_t oldest_time = now > 0 ? now : LONG_MAX;
        for (size_t i = 0; i < count; ++i) {
            if (files[i].size_bytes == 0) {
                continue;
            }
            if (strcmp(files[i].name, s_active_filename) == 0) {
                continue;
            }
            if (oldest_index == SIZE_MAX || files[i].modified_time < oldest_time) {
                oldest_index = i;
                oldest_time = files[i].modified_time;
            }
        }
        if (oldest_index == SIZE_MAX) {
            break;
        }

        history_logger_remove_file(files[oldest_index].name);
        if (total_bytes >= files[oldest_index].size_bytes) {
            total_bytes -= files[oldest_index].size_bytes;
        } else {
            total_bytes = 0;
        }
        files[oldest_index].size_bytes = 0;
    }
#endif

    history_logger_free_file_list(files);
#else
    (void)now;
#endif
}

static void history_logger_process_sample(const uart_bms_live_data_t *sample)
{
    if (sample == NULL) {
        return;
    }

    if (!history_fs_is_mounted()) {
        history_logger_close_active_file();
        return;
    }

    time_t now = time(NULL);

    if (history_logger_open_file(now) != ESP_OK) {
        return;
    }

    history_logger_write_sample(s_active_file, now, sample);

    static uint32_t write_counter = 0;
    if (CONFIG_TINYBMS_HISTORY_FLUSH_INTERVAL > 0 &&
        ++write_counter % CONFIG_TINYBMS_HISTORY_FLUSH_INTERVAL == 0) {
        fflush(s_active_file);

        // Synchroniser avec disque pour durabilité
        int fd = fileno(s_active_file);
        if (fd >= 0) {
            if (fsync(fd) != 0) {
                ESP_LOGW(TAG, "fsync failed: %d", errno);
            }
        }
    }

    if (s_active_day >= 0) {
        int current_day = history_logger_compute_day(now);
        if (current_day != s_active_day) {
            fflush(s_active_file);
            history_logger_close_active_file();
        }
    }

    static uint32_t retention_counter = 0;
    if (CONFIG_TINYBMS_HISTORY_RETENTION_CHECK_INTERVAL > 0 &&
        ++retention_counter % CONFIG_TINYBMS_HISTORY_RETENTION_CHECK_INTERVAL == 0) {
        history_logger_enforce_retention(now);
    }
}

static void history_logger_task(void *arg)
{
    (void)arg;
    uart_bms_live_data_t sample;

    while (!s_task_should_exit) {
        if (xQueueReceive(s_queue, &sample, pdMS_TO_TICKS(100)) == pdTRUE) {
            history_logger_process_sample(&sample);
        }
    }

    ESP_LOGI(TAG, "History logger task exiting");
    vTaskDelete(NULL);
}

void history_logger_init(void)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    ESP_LOGI(TAG, "History logging disabled via configuration");
    return;
#else
    if (s_queue != NULL) {
        return;
    }

    s_queue = xQueueCreate(CONFIG_TINYBMS_HISTORY_QUEUE_LENGTH, sizeof(uart_bms_live_data_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Unable to create history queue");
        return;
    }

    // Créer mutex pour buffer de retry
    if (s_retry_mutex == NULL) {
        s_retry_mutex = xSemaphoreCreateMutex();
        if (s_retry_mutex == NULL) {
            ESP_LOGE(TAG, "Unable to create retry mutex");
            vQueueDelete(s_queue);
            s_queue = NULL;
            return;
        }
    }

    // Créer mutex pour cache de liste de fichiers
    if (s_cache_mutex == NULL) {
        s_cache_mutex = xSemaphoreCreateMutex();
        if (s_cache_mutex == NULL) {
            ESP_LOGE(TAG, "Unable to create cache mutex");
            vSemaphoreDelete(s_retry_mutex);
            s_retry_mutex = NULL;
            vQueueDelete(s_queue);
            s_queue = NULL;
            return;
        }
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(history_logger_task,
                                                 "history_logger",
                                                 CONFIG_TINYBMS_HISTORY_TASK_STACK,
                                                 NULL,
                                                 CONFIG_TINYBMS_HISTORY_TASK_PRIORITY,
                                                 NULL,
                                                 tskNO_AFFINITY);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Unable to start history logger task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }

    ESP_LOGI(TAG, "History logger initialised (queue=%u)", (unsigned)CONFIG_TINYBMS_HISTORY_QUEUE_LENGTH);
#endif
}

void history_logger_handle_sample(const uart_bms_live_data_t *sample)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    (void)sample;
    return;
#else
    if (s_queue == NULL || sample == NULL) {
        return;
    }

    if (xQueueSend(s_queue, sample, 0) != pdTRUE) {
        static uint32_t dropped = 0;
        if (++dropped % 64U == 0U) {
            ESP_LOGW(TAG, "History queue saturated (%u samples dropped)", dropped);
        }
    }
#endif
}

/**
 * @brief Résoudre un nom de fichier en chemin absolu sécurisé.
 *
 * SÉCURITÉ CRITIQUE : Cette fonction valide les noms de fichiers pour empêcher
 * les attaques par traversée de répertoire. TOUS les accès fichiers DOIVENT
 * passer par cette fonction. Ne jamais construire de chemins manuellement.
 */
esp_err_t history_logger_resolve_path(const char *filename, char *buffer, size_t buffer_size)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    (void)filename;
    (void)buffer;
    (void)buffer_size;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (filename == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Bloquer les séparateurs de chemin pour empêcher la traversée
    for (const char *ptr = filename; *ptr != '\0'; ++ptr) {
        if (*ptr == '/' || *ptr == '\\') {
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Bloquer les séquences ".." pour empêcher la remontée de répertoire
    if (strstr(filename, "..") != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(buffer, buffer_size, "%s/%s", CONFIG_TINYBMS_HISTORY_DIR, filename);
    if (written < 0 || written >= (int)buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
#endif
}

static bool history_logger_is_history_file(const struct dirent *entry)
{
    if (entry == NULL) {
        return false;
    }

    if (entry->d_name[0] == '.') {
        return false;
    }

    const char *name = entry->d_name;
    size_t len = strlen(name);
    if (len < 6) {
        return false;
    }

    const char *suffix = name + len - 6;
    if (strcasecmp(suffix, ".jsonl") != 0) {
        return false;
    }

    return strncmp(name, "history-", 8) == 0;
}

/**
 * @brief Invalider le cache de liste de fichiers
 *
 * À appeler après suppression/création de fichiers d'archive
 */
static void history_logger_invalidate_cache(void)
{
    if (s_cache_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_file_cache.files != NULL) {
            free(s_file_cache.files);
            s_file_cache.files = NULL;
        }
        s_file_cache.count = 0;
        s_file_cache.valid = false;
        xSemaphoreGive(s_cache_mutex);
    }
}

/**
 * @brief Scan réel du système de fichiers (implémentation interne)
 */
static esp_err_t history_logger_list_files_impl(history_logger_file_info_t **out_files,
                                                 size_t *out_count,
                                                 bool *out_mounted)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    if (out_files != NULL) {
        *out_files = NULL;
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (out_mounted != NULL) {
        *out_mounted = false;
    }
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (out_files == NULL || out_count == NULL || out_mounted == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_files = NULL;
    *out_count = 0;
    *out_mounted = history_fs_is_mounted();

    if (!*out_mounted) {
        return ESP_OK;
    }

    if (history_logger_ensure_directory() != ESP_OK) {
        return ESP_FAIL;
    }

    DIR *dir = opendir(CONFIG_TINYBMS_HISTORY_DIR);
    if (dir == NULL) {
        return ESP_FAIL;
    }

    size_t capacity = 8;
    history_logger_file_info_t *files = calloc(capacity, sizeof(history_logger_file_info_t));
    if (files == NULL) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (!history_logger_is_history_file(entry)) {
            continue;
        }

        if (*out_count >= capacity) {
            size_t new_capacity = capacity * 2;
            history_logger_file_info_t *resized = realloc(files, new_capacity * sizeof(history_logger_file_info_t));
            if (resized == NULL) {
                free(files);
                closedir(dir);
                return ESP_ERR_NO_MEM;
            }
            files = resized;
            memset(files + capacity, 0, (new_capacity - capacity) * sizeof(history_logger_file_info_t));
            capacity = new_capacity;
        }

        history_logger_file_info_t *info = &files[*out_count];
        strlcpy(info->name, entry->d_name, sizeof(info->name));

        char path[256];
        if (history_logger_resolve_path(info->name, path, sizeof(path)) == ESP_OK) {
            struct stat st;
            if (stat(path, &st) == 0) {
                info->size_bytes = (size_t)st.st_size;
                info->modified_time = st.st_mtime;
            }
        }

        ++(*out_count);
    }

    closedir(dir);

    if (*out_count > 1) {
        qsort(files, *out_count, sizeof(history_logger_file_info_t), history_logger_compare_file_info);
    }

    *out_files = files;
    return ESP_OK;
#endif
}

/**
 * @brief Lister tous les fichiers d'historique avec cache
 *
 * Cette fonction utilise un cache avec TTL de 30 secondes pour éviter
 * de scanner le système de fichiers à chaque appel (99.96% de réduction
 * des opérations I/O : 1 scan/30s au lieu de 1/s).
 */
esp_err_t history_logger_list_files(history_logger_file_info_t **out_files,
                                    size_t *out_count,
                                    bool *out_mounted)
{
    if (out_files == NULL || out_count == NULL || out_mounted == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Vérifier si cache disponible et valide
    if (s_cache_mutex != NULL &&
        xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

        uint64_t now_ms = esp_timer_get_time() / 1000;
        bool cache_valid = s_file_cache.valid &&
                          (now_ms - s_file_cache.cached_at_ms) < FILE_LIST_CACHE_TTL_MS;

        if (cache_valid) {
            // Retourner copie du cache
            *out_mounted = s_file_cache.mounted;
            *out_count = s_file_cache.count;

            if (s_file_cache.count > 0 && s_file_cache.files != NULL) {
                size_t alloc_size = s_file_cache.count * sizeof(history_logger_file_info_t);
                *out_files = malloc(alloc_size);
                if (*out_files == NULL) {
                    xSemaphoreGive(s_cache_mutex);
                    return ESP_ERR_NO_MEM;
                }
                memcpy(*out_files, s_file_cache.files, alloc_size);
            } else {
                *out_files = NULL;
            }

            xSemaphoreGive(s_cache_mutex);
            return ESP_OK;
        }

        // Cache expiré ou invalide, scanner le FS
        history_logger_file_info_t *fresh_files = NULL;
        size_t fresh_count = 0;
        bool fresh_mounted = false;

        xSemaphoreGive(s_cache_mutex);
        esp_err_t err = history_logger_list_files_impl(&fresh_files, &fresh_count, &fresh_mounted);

        if (err == ESP_OK && xSemaphoreTake(s_cache_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Mettre à jour le cache
            if (s_file_cache.files != NULL) {
                free(s_file_cache.files);
            }

            if (fresh_count > 0 && fresh_files != NULL) {
                size_t alloc_size = fresh_count * sizeof(history_logger_file_info_t);
                s_file_cache.files = malloc(alloc_size);
                if (s_file_cache.files != NULL) {
                    memcpy(s_file_cache.files, fresh_files, alloc_size);
                    s_file_cache.count = fresh_count;
                } else {
                    s_file_cache.count = 0;
                }
            } else {
                s_file_cache.files = NULL;
                s_file_cache.count = 0;
            }

            s_file_cache.mounted = fresh_mounted;
            s_file_cache.cached_at_ms = esp_timer_get_time() / 1000;
            s_file_cache.valid = true;

            xSemaphoreGive(s_cache_mutex);
        }

        // Retourner les données fraîches
        *out_files = fresh_files;
        *out_count = fresh_count;
        *out_mounted = fresh_mounted;
        return err;
    }

    // Fallback si mutex non disponible (pas encore initialisé)
    return history_logger_list_files_impl(out_files, out_count, out_mounted);
}

void history_logger_free_file_list(history_logger_file_info_t *files)
{
    free(files);
}

static const char *history_logger_locate_field_start(const char *line, const char *key)
{
    if (line == NULL || key == NULL) {
        return NULL;
    }

    size_t key_len = strlen(key);
    if (key_len == 0U || key_len > 48U) {
        return NULL;
    }

    char needle[64];
    int written = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (written < 0 || (size_t)written >= sizeof(needle)) {
        return NULL;
    }

    const char *cursor = line;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        cursor += (size_t)written;

        const char *colon = strchr(cursor, ':');
        if (colon == NULL) {
            return NULL;
        }

        colon++;
        while (isspace((unsigned char)*colon)) {
            colon++;
        }

        return colon;
    }

    return NULL;
}

static bool history_logger_parse_string_field(const char *line, const char *key, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    const char *value = history_logger_locate_field_start(line, key);
    if (value == NULL || *value != '"') {
        return false;
    }

    value++;
    const char *end = value;
    while (*end != '\0') {
        if (*end == '\\') {
            if (*(end + 1) == '\0') {
                break;
            }
            end += 2;
            continue;
        }

        if (*end == '"') {
            break;
        }

        end++;
    }

    if (*end != '"') {
        return false;
    }

    size_t copy_len = (size_t)(end - value);
    if (copy_len >= buffer_size) {
        copy_len = buffer_size - 1U;
    }

    memcpy(buffer, value, copy_len);
    buffer[copy_len] = '\0';
    return true;
}

static bool history_logger_parse_number_field(const char *line, const char *key, double *out_value)
{
    if (out_value == NULL) {
        return false;
    }

    const char *value = history_logger_locate_field_start(line, key);
    if (value == NULL) {
        return false;
    }

    errno = 0;
    char *endptr = NULL;
    double parsed = strtod(value, &endptr);
    if (value == endptr || errno != 0) {
        return false;
    }

    *out_value = parsed;
    return true;
}

static size_t history_logger_tail_bytes(size_t sample_capacity)
{
    size_t bytes = sample_capacity * HISTORY_LOGGER_MAX_LINE_LENGTH;
    if (bytes > CONFIG_TINYBMS_HISTORY_MAX_BYTES) {
        bytes = CONFIG_TINYBMS_HISTORY_MAX_BYTES;
    }
    return bytes;
}

static void history_logger_seek_to_recent(FILE *file, size_t max_bytes, char *scratch, size_t scratch_size)
{
    if (file == NULL) {
        return;
    }

    if (max_bytes == 0U || scratch == NULL || scratch_size == 0U) {
        rewind(file);
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        rewind(file);
        return;
    }

    long file_size = ftell(file);
    if (file_size <= 0) {
        rewind(file);
        return;
    }

    long offset = 0;
    if ((size_t)file_size > max_bytes) {
        offset = file_size - (long)max_bytes;
    }

    if (offset <= 0 || fseek(file, offset, SEEK_SET) != 0) {
        rewind(file);
        return;
    }

    // Discard potential partial line to start at the next newline boundary.
    (void)fgets(scratch, scratch_size, file);
}

static bool history_logger_parse_line(const char *line, history_logger_archive_sample_t *out_sample)
{
    if (line == NULL || out_sample == NULL) {
        return false;
    }

    char type[32];
    if (history_logger_parse_string_field(line, "type", type, sizeof(type)) &&
        strcmp(type, "history_sample") != 0) {
        return false;
    }

    if (!history_logger_parse_string_field(line, "timestamp_iso", out_sample->timestamp_iso,
                                           sizeof(out_sample->timestamp_iso))) {
        return false;
    }

    double timestamp_ms = 0.0;
    double pack_voltage_v = 0.0;
    double pack_current_a = 0.0;
    double state_of_charge_pct = 0.0;
    double state_of_health_pct = 0.0;
    double average_temperature_c = 0.0;

    if (!history_logger_parse_number_field(line, "timestamp_ms", &timestamp_ms) ||
        !history_logger_parse_number_field(line, "pack_voltage_v", &pack_voltage_v) ||
        !history_logger_parse_number_field(line, "pack_current_a", &pack_current_a) ||
        !history_logger_parse_number_field(line, "state_of_charge_pct", &state_of_charge_pct) ||
        !history_logger_parse_number_field(line, "state_of_health_pct", &state_of_health_pct) ||
        !history_logger_parse_number_field(line, "average_temperature_c", &average_temperature_c)) {
        return false;
    }

    out_sample->timestamp_ms = (uint64_t)timestamp_ms;
    out_sample->pack_voltage_v = (float)pack_voltage_v;
    out_sample->pack_current_a = (float)pack_current_a;
    out_sample->state_of_charge_pct = (float)state_of_charge_pct;
    out_sample->state_of_health_pct = (float)state_of_health_pct;
    out_sample->average_temperature_c = (float)average_temperature_c;

    return true;
}

esp_err_t history_logger_load_archive(const char *filename, size_t limit, history_logger_archive_t *out_archive)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    if (out_archive != NULL) {
        memset(out_archive, 0, sizeof(*out_archive));
    }
    (void)filename;
    (void)limit;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (out_archive == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_archive, 0, sizeof(*out_archive));

    if (!history_fs_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[256];
    esp_err_t resolve_err = history_logger_resolve_path(filename, path, sizeof(path));
    if (resolve_err != ESP_OK) {
        return resolve_err;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return ESP_FAIL;
    }

    size_t capacity = CONFIG_TINYBMS_HISTORY_ARCHIVE_MAX_SAMPLES;
    if (limit > 0 && limit < capacity) {
        capacity = limit;
    }

    if (capacity == 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    history_logger_archive_sample_t *samples = calloc(capacity, sizeof(history_logger_archive_sample_t));
    if (samples == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    const size_t tail_bytes = history_logger_tail_bytes(capacity);
    char line[HISTORY_LOGGER_MAX_LINE_LENGTH];
    history_logger_seek_to_recent(file, tail_bytes, line, sizeof(line));
    size_t total = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t line_len = strlen(line);

        // Detect truncation (line doesn't end with newline)
        if (line_len > 0 && line[line_len - 1] != '\n') {
            ESP_LOGW(TAG, "Line truncated (>%u bytes), skipping sample",
                     HISTORY_LOGGER_MAX_LINE_LENGTH);

            // Consume the rest of the line to stay synchronized
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {
                // Discard characters until newline or EOF
            }
            continue;
        }

        history_logger_archive_sample_t sample;
        if (!history_logger_parse_line(line, &sample)) {
            ESP_LOGD(TAG, "Failed to parse line, skipping");
            continue;
        }

        size_t index = total % capacity;
        samples[index] = sample;
        ++total;
    }

    fclose(file);

    out_archive->total_samples = total;
    out_archive->buffer_capacity = capacity;
    out_archive->samples = samples;
    out_archive->returned_samples = (total < capacity) ? total : capacity;
    out_archive->start_index = (total < capacity) ? 0 : (total % capacity);

    return ESP_OK;
#endif
}

void history_logger_free_archive(history_logger_archive_t *archive)
{
    if (archive == NULL) {
        return;
    }

    free(archive->samples);
    archive->samples = NULL;
    archive->returned_samples = 0;
    archive->total_samples = 0;
    archive->start_index = 0;
    archive->buffer_capacity = 0;
}

void history_logger_deinit(void)
{
#if !CONFIG_TINYBMS_HISTORY_ENABLE
    ESP_LOGI(TAG, "History logging disabled, nothing to deinitialize");
    return;
#else
    ESP_LOGI(TAG, "Deinitializing history logger...");

    // Signal task to exit
    s_task_should_exit = true;

    // Give task time to exit cleanly
    vTaskDelay(pdMS_TO_TICKS(200));

    // Close active file if open
    history_logger_close_active_file();

    // Delete queue
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }

    // Destroy retry mutex
    if (s_retry_mutex != NULL) {
        vSemaphoreDelete(s_retry_mutex);
        s_retry_mutex = NULL;
    }

    // Reset state
    s_directory_ready = false;
    s_task_should_exit = false;
    s_retry_buffer_count = 0;
    s_active_day = -1;
    memset(s_active_filename, 0, sizeof(s_active_filename));
    memset(s_retry_buffer, 0, sizeof(s_retry_buffer));

    ESP_LOGI(TAG, "History logger deinitialized");
#endif
}

