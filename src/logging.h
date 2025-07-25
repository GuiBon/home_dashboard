#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

// Global log file pointer
extern FILE *g_log_file;
extern int g_debug_mode;

// Initialize logging system
int init_logging(int debug_mode);

// Close logging system
void close_logging(void);

// Log functions
void log_info(const char *format, ...);
void log_error(const char *format, ...);
void log_debug(const char *format, ...);

// Macros for easier logging
#define LOG_INFO(fmt, ...) log_info(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_error(fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) log_debug(fmt, ##__VA_ARGS__)

#endif // LOGGING_H