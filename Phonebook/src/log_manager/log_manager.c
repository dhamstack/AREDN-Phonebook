// log_manager.c
#include "../common.h"
#include "log_manager.h"
#include <syslog.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h> 
#include <sys/syscall.h> 

#define MODULE_NAME "LOG" // Corrected MODULE_NAME

// Define the desired compile-time log level here.
// Set to DEBUG during UAC development phase for detailed logging
#define LOG_COMPILE_LEVEL LOG_LEVEL_DEBUG

void log_init(const char* app_name) {
    openlog(app_name, LOG_PID | LOG_CONS | LOG_NDELAY, LOG_DAEMON);
}

void log_shutdown(void) {
    closelog();
}

void log_message(int level, const char* app_name_in, const char* module_name_in, const char *format, ...) {
    if (LOG_COMPILE_LEVEL == LOG_LEVEL_NONE) {
        if (level != LOG_LEVEL_ERROR && level != LOG_LEVEL_WARNING) {
            return;
        }
    } else {
        if (level > LOG_COMPILE_LEVEL) {
            return;
        }
    }

    int syslog_level;
    switch (level) {
        case LOG_LEVEL_ERROR:   syslog_level = LOG_ERR;     break;
        case LOG_LEVEL_WARNING: syslog_level = LOG_WARNING; break;
        case LOG_LEVEL_INFO:    syslog_level = LOG_INFO;    break;
        case LOG_LEVEL_DEBUG:   syslog_level = LOG_DEBUG;   break;
        default:                syslog_level = LOG_NOTICE;  break;
    }

    pid_t current_pid = getpid();
    pid_t current_tid = syscall(SYS_gettid); 

    char final_log_message[MAX_SIP_MSG_LEN + 64];

    va_list args;
    va_start(args, format);

    char original_message_content[MAX_SIP_MSG_LEN];
    vsnprintf(original_message_content, sizeof(original_message_content), format, args);
    original_message_content[sizeof(original_message_content) - 1] = '\0';

    snprintf(final_log_message, sizeof(final_log_message),
             "%s [%d/%d]: %s: %s",
             app_name_in, 
             current_pid,
             current_tid,
             module_name_in, 
             original_message_content);
    final_log_message[sizeof(final_log_message) - 1] = '\0';

    syslog(syslog_level, "%s", final_log_message);
    va_end(args);
}