#include "history_logger.h"

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

#include "history_fs.h"

static const char *TAG = "history_logger";

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
    int written = snprintf(filename, sizeof(filename), "history-%s.csv", identifier);
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

    bool new_file = access(path, F_OK) != 0;

    FILE *file = fopen(path, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Unable to open history file %s", path);
        return ESP_FAIL;
    }

    if (new_file) {
        fprintf(file, "timestamp_iso,timestamp_ms,pack_voltage_v,pack_current_a,state_of_charge_pct,state_of_health_pct,average_temperature_c\n");
        fflush(file);
    }

    strlcpy(s_active_filename, filename, sizeof(s_active_filename));
    s_active_file = file;
    s_active_day = current_day;

    return ESP_OK;
}

static void history_logger_format_iso(time_t now, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    if (now <= 0) {
        snprintf(buffer, size, "1970-01-01T00:00:00Z");
        return;
    }

    struct tm tm_now;
    gmtime_r(&now, &tm_now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tm_now);
}

static void history_logger_write_sample(FILE *file, time_t now, const uart_bms_live_data_t *sample)
{
    if (file == NULL || sample == NULL) {
        return;
    }

    char iso[32];
    history_logger_format_iso(now, iso, sizeof(iso));

    fprintf(file,
            "%s,%" PRIu64 ",%.3f,%.3f,%.2f,%.2f,%.2f\n",
            iso,
            sample->timestamp_ms,
            sample->pack_voltage_v,
            sample->pack_current_a,
            sample->state_of_charge_pct,
            sample->state_of_health_pct,
            sample->average_temperature_c);
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

    while (true) {
        if (xQueueReceive(s_queue, &sample, portMAX_DELAY) == pdTRUE) {
            history_logger_process_sample(&sample);
        }
    }
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

    for (const char *ptr = filename; *ptr != '\0'; ++ptr) {
        if (*ptr == '/' || *ptr == '\\') {
            return ESP_ERR_INVALID_ARG;
        }
    }

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
    if (len < 4) {
        return false;
    }

    const char *suffix = name + len - 4;
    if (strcasecmp(suffix, ".csv") != 0) {
        return false;
    }

    return strncmp(name, "history-", 8) == 0;
}

esp_err_t history_logger_list_files(history_logger_file_info_t **out_files,
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

void history_logger_free_file_list(history_logger_file_info_t *files)
{
    free(files);
}

static bool history_logger_parse_line(const char *line, history_logger_archive_sample_t *out_sample)
{
    if (line == NULL || out_sample == NULL) {
        return false;
    }

    char buffer[256];
    strlcpy(buffer, line, sizeof(buffer));

    char *saveptr = NULL;
    char *token = strtok_r(buffer, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    strlcpy(out_sample->timestamp_iso, token, sizeof(out_sample->timestamp_iso));

    token = strtok_r(NULL, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    out_sample->timestamp_ms = strtoull(token, NULL, 10);

    token = strtok_r(NULL, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    out_sample->pack_voltage_v = strtof(token, NULL);

    token = strtok_r(NULL, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    out_sample->pack_current_a = strtof(token, NULL);

    token = strtok_r(NULL, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    out_sample->state_of_charge_pct = strtof(token, NULL);

    token = strtok_r(NULL, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    out_sample->state_of_health_pct = strtof(token, NULL);

    token = strtok_r(NULL, ",", &saveptr);
    if (token == NULL) {
        return false;
    }
    out_sample->average_temperature_c = strtof(token, NULL);

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

    char line[256];
    size_t total = 0;
    bool header_skipped = false;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (!header_skipped) {
            header_skipped = true;
            if (strstr(line, "timestamp_iso") != NULL) {
                continue;
            }
        }

        history_logger_archive_sample_t sample;
        if (!history_logger_parse_line(line, &sample)) {
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

