#include "display_partial.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Waveshare library includes
#include "DEV_Config.h"
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"
#include "fonts.h"

// Global variables for partial refresh
static UBYTE *time_image_buffer = NULL;
static int partial_display_initialized = 0;

// Time display position in header section (matches display_eink.c layout)
// Time is centered at HEADER_X + HEADER_WIDTH/2, HEADER_Y + 65
// That's 5 + 470/2 = 240, 5 + 65 = 70
#define TIME_X 190          // X position for time in header (centered around 240)
#define TIME_Y 60           // Y position for time in header  
#define TIME_WIDTH 100      // Width of time display area
#define TIME_HEIGHT 30      // Height of time display area

int init_partial_display(void) {
    if (partial_display_initialized) {
        return 0; // Already initialized
    }

    LOG_INFO("🔧 Initializing partial e-ink display for time updates...");
    
    // Initialize Waveshare device configuration
    if (DEV_Module_Init() != 0) {
        LOG_ERROR("❌ Failed to initialize device module");
        return -1;
    }
    
    // Initialize e-paper display with full initialization first
    if (EPD_7IN5_V2_Init() != 0) {
        LOG_ERROR("❌ Failed to initialize e-paper display");
        DEV_Module_Exit();
        return -1;
    }
    
    // Clear display once to ensure known state
    LOG_DEBUG("Clearing display for partial refresh initialization...");
    EPD_7IN5_V2_Clear();
    
    // Now switch to partial refresh mode
    if (EPD_7IN5_V2_Init_Part() != 0) {
        LOG_ERROR("❌ Failed to initialize e-paper for partial refresh");
        DEV_Module_Exit();
        return -1;
    }
    
    // Allocate image buffer for time display area
    UDOUBLE image_size = TIME_WIDTH * TIME_HEIGHT / 8; // 1 bit per pixel
    if ((time_image_buffer = (UBYTE *)malloc(image_size)) == NULL) {
        LOG_ERROR("❌ Failed to allocate memory for time image buffer");
        DEV_Module_Exit();
        return -1;
    }
    
    // Initialize paint library with time buffer
    Paint_NewImage(time_image_buffer, TIME_WIDTH, TIME_HEIGHT, 0, WHITE);
    Paint_SelectImage(time_image_buffer);
    Paint_Clear(WHITE);
    
    partial_display_initialized = 1;
    LOG_INFO("✅ Partial display initialized successfully");
    return 0;
}

int refresh_time_partial(void) {
    if (!partial_display_initialized) {
        LOG_ERROR("⚠️  Partial display not initialized, initializing now...");
        if (init_partial_display() != 0) {
            return -1;
        }
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    // Format time string
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M", tm_info);
    
    // Clear the time display area in buffer
    Paint_Clear(WHITE);
    
    // Draw the updated time (centered in buffer)
    // Font20 is about 14 pixels wide per character, "HH:MM" is 5 characters
    int text_width_approx = 5 * 14;
    int text_x = (TIME_WIDTH - text_width_approx) / 2;
    Paint_DrawString_EN(text_x, 5, time_str, &Font20, WHITE, BLACK);
    
    // Perform partial refresh of the time area only
    EPD_7IN5_V2_Display_Part(time_image_buffer, TIME_X, TIME_Y, TIME_X + TIME_WIDTH, TIME_Y + TIME_HEIGHT);
    
    return 0;
}

void cleanup_partial_display(void) {
    if (partial_display_initialized) {
        LOG_INFO("🧹 Cleaning up partial display resources...");
        
        // Put display to sleep
        EPD_7IN5_V2_Sleep();
        
        // Free image buffer
        if (time_image_buffer) {
            free(time_image_buffer);
            time_image_buffer = NULL;
        }
        
        // Exit device module
        DEV_Module_Exit();
        
        partial_display_initialized = 0;
        LOG_INFO("✅ Partial display cleanup completed");
    }
}

int is_partial_display_available(void) {
    return partial_display_initialized;
}