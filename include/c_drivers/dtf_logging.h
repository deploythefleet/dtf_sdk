#ifndef DTF_C_LOGGER_H
#define DTF_C_LOGGER_H
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef enum dtf_log_level
{
    NONE = 0,
    ERROR,
    WARN,
    INFO,
    DEBUG,
    VERBOSE
}DTF_LogLevel;

typedef enum supported_loggers
{
    SERIAL_LOGGER = 0,
} DTF_Loggers;

void DTF_LOGV(const char *tag, const char *fmt, ...);
void DTF_LOGD(const char *tag, const char *fmt, ...);
void DTF_LOGI(const char *tag, const char *fmt, ...);
void DTF_LOGW(const char *tag, const char *fmt, ...);
void DTF_LOGE(const char *tag, const char *fmt, ...);

void dtf_set_log_level(DTF_LogLevel level);
DTF_LogLevel dtf_get_log_level();
void dtf_enable_logger(DTF_Loggers logger);
void dtf_disable_logger(DTF_Loggers logger);
#ifdef __cplusplus
}
#endif

#endif // DTF_C_LOGGER_H