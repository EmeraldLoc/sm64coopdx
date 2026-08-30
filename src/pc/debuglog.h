#pragma once

#include "pc/terminal.h"
#include "types.h"

enum LogType {
    LOG_TYPE_DEBUG,
    LOG_TYPE_INFO,
    LOG_TYPE_WARNING,
    LOG_TYPE_ERROR
};

#define MAX_LOG_SIZE 8192

bool log_type_should_print(enum LogType logType);
const char *log_type_ansi_color(enum LogType logType);
const char *log_type_hex_color_code(enum LogType logType);
const char *log_type_name(enum LogType logType);
void debuglog_print_log(enum LogType logType, const char *filename, const char *fmt, ...);

#if defined(DISABLE_MODULE_LOG)
#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_WARNING(...)
#define LOG_ERROR(...)
#else
#define LOG_DEBUG(...)   (debuglog_print_log(LOG_TYPE_DEBUG,   __FILE__, __VA_ARGS__))
#define LOG_INFO(...)    (debuglog_print_log(LOG_TYPE_INFO,    __FILE__, __VA_ARGS__))
#define LOG_WARNING(...) (debuglog_print_log(LOG_TYPE_WARNING, __FILE__, __VA_ARGS__))
#define LOG_ERROR(...)   (debuglog_print_log(LOG_TYPE_ERROR,   __FILE__, __VA_ARGS__))
#endif

#define LOG_CONSOLE(...)  { snprintf(gDjuiConsoleTmpBuffer, CONSOLE_MAX_TMP_BUFFER, __VA_ARGS__), djui_console_message_create(gDjuiConsoleTmpBuffer, LOG_TYPE_INFO); }
