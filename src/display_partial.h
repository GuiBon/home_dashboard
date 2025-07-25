#ifndef DISPLAY_PARTIAL_H
#define DISPLAY_PARTIAL_H

/**
 * Partial e-ink display functionality for time updates
 * Uses Waveshare 7.5" V2 e-paper display for fast partial refresh of time display
 */

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

#endif // DISPLAY_PARTIAL_H