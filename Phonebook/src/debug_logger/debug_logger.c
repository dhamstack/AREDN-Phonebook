#include "../common.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#define GEMINI_DEBUG_LOG_FILE "/tmp/gemini_debug.log"

void gemini_debug_log(const char *format, ...) {
    FILE *fp = fopen(GEMINI_DEBUG_LOG_FILE, "a"); // Open in append mode
    if (fp == NULL) {
        // Fallback to stderr if file cannot be opened
        fprintf(stderr, "GEMINI_DEBUG_ERROR: Could not open debug log file %s\n", GEMINI_DEBUG_LOG_FILE);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    fprintf(fp, "%s ", timestamp);

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}
