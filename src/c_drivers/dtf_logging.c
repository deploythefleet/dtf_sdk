#include "../../include/c_drivers/dtf_c_logging.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

typedef void (*log_fn)(const char *tag, const char *fmt, ...);
typedef void (*log_handler_fn)(const char *tag, const DTF_LogLevel level, const char *fmt, va_list args);

static SemaphoreHandle_t xSemaphore = NULL;
static DTF_LogLevel _log_level = ERROR;

char get_error_char(const DTF_LogLevel level)
{
    switch(level)
    {
        case ERROR:
            return 'E';
        case WARN:
            return 'W';
        case INFO:
            return 'I';
        case DEBUG:
            return 'D';
        case VERBOSE:
            return 'V';
        default:
            return 'X';
    }
}

#if defined(CONFIG_DTF_LOGGING_USE_COLOR)
#define RED_COLOR_CODE 31
#define GREEN_COLOR_CODE 32
#define YELLOW_COLOR_CODE 33
#define MAGENTA_COLOR_CODE 35
#define CYAN_COLOR_CODE 36
int get_console_color_code(const DTF_LogLevel level)
{
    switch(level)
    {
        case ERROR:
            return RED_COLOR_CODE;
        case WARN:
            return YELLOW_COLOR_CODE;
        case INFO:
            return GREEN_COLOR_CODE;
        case DEBUG:
            return CYAN_COLOR_CODE;
        case VERBOSE:
            return MAGENTA_COLOR_CODE;
        default:
            return GREEN_COLOR_CODE;
    }
}
#endif

int map_log_level(const DTF_LogLevel level)
{
    switch(level)
    {
        case NONE:
            return ESP_LOG_NONE;
        case ERROR:
            return ESP_LOG_ERROR;
        case WARN:
            return ESP_LOG_WARN;
        case INFO:
            return ESP_LOG_INFO;
        case DEBUG:
            return ESP_LOG_DEBUG;
        case VERBOSE:
            return ESP_LOG_VERBOSE;
        default:
            return ESP_LOG_INFO;
    }
}


#if defined(CONFIG_DTF_LOGGING_UART_ENABLED)
void serial_logger_log_message(const char *tag, DTF_LogLevel level, const char *fmt, va_list args)
{
    if(xSemaphore == NULL)
    {
        xSemaphore = xSemaphoreCreateMutex();
    }

    if(xSemaphore != NULL && xSemaphoreTake(xSemaphore, (TickType_t) 20))
    {
        esp_log_level_t esp_log_level = (esp_log_level_t)map_log_level(level);
        #if defined(CONFIG_DTF_LOGGING_USE_COLOR)
        esp_log_write(esp_log_level, tag, "\033[0;%dm%c (%ld) %s: ", get_console_color_code(level), get_error_char(level), esp_log_timestamp(), tag);
        esp_log_writev(esp_log_level, tag, fmt, args);
        esp_log_write(esp_log_level, tag, "\033[0m\n");
        #else
        esp_log_write(esp_log_level, tag, "%c (%ld) %s: ", get_error_char(level), esp_log_timestamp(), tag);
        esp_log_writev(esp_log_level, tag, fmt, args);
        esp_log_write(esp_log_level, tag, "\n");
        #endif
        xSemaphoreGive(xSemaphore);
    }
    else{
        printf("could not get logging semaphore\n");
    }
}
#endif

static void dispatch_log_message(const char *tag, const DTF_LogLevel level, const char *fmt, va_list args)
{
    if(_log_level >= level)
    {

        #if defined(CONFIG_DTF_LOGGING_UART_ENABLED)
        serial_logger_log_message(tag, level, fmt, args); 
        #endif
    }
}

void DTF_LOGV(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch_log_message(tag, VERBOSE, fmt, args);
    va_end(args);
}

void DTF_LOGD(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch_log_message(tag, DEBUG, fmt, args);
    va_end(args);
}

void DTF_LOGI(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch_log_message(tag, INFO, fmt, args);
    va_end(args);
}

void DTF_LOGW(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch_log_message(tag, WARN, fmt, args);
    va_end(args);
}

void DTF_LOGE(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dispatch_log_message(tag, ERROR, fmt, args);
    va_end(args);
}

void dtf_set_log_level(DTF_LogLevel level)
{
    _log_level = level;
}

DTF_LogLevel dtf_get_log_level()
{
    return _log_level;
}

void dtf_enable_logger(DTF_Loggers logger)
{
    // Not implemented
    return;
}

void dtf_disable_logger(DTF_Loggers logger)
{
    // Not implemented
    return;
}