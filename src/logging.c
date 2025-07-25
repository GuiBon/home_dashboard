#include "logging.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

// Global variables
FILE *g_log_file = NULL;
int g_debug_mode = 0;

int init_logging(int debug_mode) {
    g_debug_mode = debug_mode;
    
    if (debug_mode) {
        // Debug mode: log to stdout
        g_log_file = stdout;
        return 0;
    } else {
        // Production mode: log to file
        // Create log directory if it doesn't exist
        if (mkdir(PROJECT_ROOT "/log", 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "Failed to create log directory: %s\n", strerror(errno));
            return -1;
        }
        
        g_log_file = fopen(PROJECT_ROOT "/log/dashboard.log", "a");
        if (!g_log_file) {
            fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
            return -1;
        }
        
        // Write session start marker
        time_t now = time(NULL);
        fprintf(g_log_file, "\n=== Dashboard Session Started: %s", ctime(&now));
        fflush(g_log_file);
        return 0;
    }
}

void close_logging(void) {
    if (g_log_file && g_log_file != stdout && g_log_file != stderr) {
        time_t now = time(NULL);
        fprintf(g_log_file, "=== Dashboard Session Ended: %s\n", ctime(&now));
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

static void write_log(const char *level, const char *format, va_list args) {
    if (!g_log_file) return;
    
    // Get timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    // Write timestamp and level
    if (g_debug_mode) {
        // Debug mode: simplified format for stdout
        vfprintf(g_log_file, format, args);
        fprintf(g_log_file, "\n");
    } else {
        // Production mode: full timestamp for log file
        fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, level);
        vfprintf(g_log_file, format, args);
        fprintf(g_log_file, "\n");
    }
    
    fflush(g_log_file);
}

void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    write_log("INFO", format, args);
    va_end(args);
}

void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    write_log("ERROR", format, args);
    va_end(args);
    
    // Also write errors to stderr in production mode
    if (!g_debug_mode) {
        va_start(args, format);
        fprintf(stderr, "ERROR: ");
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
}

void log_debug(const char *format, ...) {
    // Only log debug messages in debug mode
    if (g_debug_mode) {
        va_list args;
        va_start(args, format);
        write_log("DEBUG", format, args);
        va_end(args);
    }
    // If not in debug mode, do nothing (write nothing)
}