#ifndef DISPLAY_DASHBOARD_H
#define DISPLAY_DASHBOARD_H

#include <time.h>
#include "weather.h"
#include "menu.h"
#include "calendar.h"

// Main dashboard generation functions
int generate_dashboard_bmp(const char *filename, time_t display_date, 
                          const WeatherData *weather_data, 
                          const MenuData *menu_data, 
                          const CalendarData *calendar_data);

// Waveshare e-ink hardware initialization
int init_eink_hardware(void);
void cleanup_eink_hardware(void);

// E-ink display refresh types
typedef enum {
    REFRESH_FULL,    // Full refresh - best quality, ~10-15 seconds (for menu/calendar updates)
    REFRESH_FAST,    // Fast refresh - faster update, ~2-3 seconds (for weather updates)
    REFRESH_PARTIAL  // Partial refresh - region update, ~1-2 seconds (for time updates)
} RefreshType;

// E-ink display functions
int display_image_on_eink(const char *image_path);
int display_image_on_eink_with_refresh_type(const char *image_path, RefreshType refresh_type);

// Partial e-ink display functionality for time updates
// Uses Waveshare 7.5" V2 e-paper display for fast partial refresh of time display

/**
 * Initialize the partial display system for time updates
 * This sets up the Waveshare library and allocates buffers for partial refresh
 * 
 * Returns:
 *   0 on success
 *   -1 on failure
 */
int init_partial_display(void);

/**
 * Refresh only the time display area using partial refresh
 * This is much faster than full screen refresh (1-2 seconds vs 10-15 seconds)
 * 
 * Returns:
 *   0 on success
 *   -1 on failure
 */
int refresh_time_partial(void);

/**
 * Clean up partial display resources
 * Call this when shutting down the application
 */
void cleanup_partial_display(void);

/**
 * Check if partial display is available and initialized
 * 
 * Returns:
 *   1 if available
 *   0 if not available
 */
int is_partial_display_available(void);

#endif // DISPLAY_DASHBOARD_H