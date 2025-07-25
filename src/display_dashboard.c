#define _GNU_SOURCE
#include "display_dashboard.h"
#include "dashboard_render.h"
#include "logging.h"
#include <cairo.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Waveshare e-ink includes
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"
#include "GUI_BMPfile.h"
#include "DEV_Config.h"
#include "fonts.h"


// Write Cairo surface as BMP file (for e-ink compatibility)
static int write_surface_as_bmp(cairo_surface_t *surface, const char *filename) {
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    
    FILE *f = fopen(filename, "wb");
    if (!f) {
        LOG_ERROR("❌ Failed to create BMP file: %s", filename);
        return 0;
    }
    
    // BMP Header (54 bytes total)
    int row_padded = (width * 3 + 3) & (~3); // Pad to 4 bytes
    int file_size = 54 + row_padded * height;
    
    // BMP File Header (14 bytes)
    unsigned char bmp_header[54] = {
        'B', 'M',                           // Signature
        file_size & 0xff, (file_size >> 8) & 0xff, (file_size >> 16) & 0xff, (file_size >> 24) & 0xff, // File size
        0, 0, 0, 0,                         // Reserved
        54, 0, 0, 0,                        // Data offset
        
        // BMP Info Header (40 bytes)
        40, 0, 0, 0,                        // Header size
        width & 0xff, (width >> 8) & 0xff, (width >> 16) & 0xff, (width >> 24) & 0xff, // Width
        height & 0xff, (height >> 8) & 0xff, (height >> 16) & 0xff, (height >> 24) & 0xff, // Height
        1, 0,                               // Planes
        24, 0,                              // Bits per pixel
        0, 0, 0, 0,                         // Compression
        0, 0, 0, 0,                         // Image size (can be 0 for uncompressed)
        0, 0, 0, 0,                         // X pixels per meter
        0, 0, 0, 0,                         // Y pixels per meter
        0, 0, 0, 0,                         // Colors used
        0, 0, 0, 0                          // Important colors
    };
    
    fwrite(bmp_header, 1, 54, f);
    
    // Write pixel data (BMP is bottom-up, BGR format)
    unsigned char *row_data = malloc(row_padded);
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            // Cairo ARGB32: B-G-R-A, convert to BGR
            int cairo_offset = y * stride + x * 4;
            int bmp_offset = x * 3;
            row_data[bmp_offset] = data[cairo_offset];     // Blue
            row_data[bmp_offset + 1] = data[cairo_offset + 1]; // Green  
            row_data[bmp_offset + 2] = data[cairo_offset + 2]; // Red
        }
        // Pad row to 4 bytes
        for (int x = width * 3; x < row_padded; x++) {
            row_data[x] = 0;
        }
        fwrite(row_data, 1, row_padded, f);
    }
    
    free(row_data);
    fclose(f);
    return 1;
}

// Generate dashboard as BMP
int generate_dashboard_bmp(const char *filename, time_t display_date, 
                          const WeatherData *weather_data, 
                          const MenuData *menu_data, 
                          const CalendarData *calendar_data) {
    
    // Create surface in RGB24 format for BMP compatibility
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, EINK_WIDTH, EINK_HEIGHT);
    cairo_t *cr = cairo_create(surface);
    
    // Initialize fonts
    if (!init_dashboard_fonts()) {
        LOG_ERROR("❌ Failed to initialize fonts for BMP generation");
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return 0;
    }
    
    // Render dashboard to surface
    render_dashboard_to_surface(surface, display_date, weather_data, menu_data, calendar_data);
    
    // Write as BMP
    cairo_surface_flush(surface);
    int success = write_surface_as_bmp(surface, filename);
    
    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    
    if (success) {
        LOG_INFO("✅ Dashboard BMP generated successfully: %s", filename);
    }
    
    return success;
}

// ====================== E-INK HARDWARE INITIALIZATION ======================

// Global variables for hardware state
static int eink_hardware_initialized = 0;

int init_eink_hardware(void) {
    if (eink_hardware_initialized) {
        return 0; // Already initialized
    }

    LOG_INFO("🔧 Initializing Waveshare e-ink hardware...");
    
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
    LOG_DEBUG("Clearing display for hardware initialization...");
    EPD_7IN5_V2_Clear();
    
    eink_hardware_initialized = 1;
    LOG_INFO("✅ E-ink hardware initialized successfully");
    return 0;
}

void cleanup_eink_hardware(void) {
    if (eink_hardware_initialized) {
        LOG_INFO("🧹 Cleaning up e-ink hardware...");
        
        // Put display to sleep
        EPD_7IN5_V2_Sleep();
        
        // Exit device module
        DEV_Module_Exit();
        
        eink_hardware_initialized = 0;
        LOG_INFO("✅ E-ink hardware cleanup completed");
    }
}

// Display image on e-ink
int display_image_on_eink(const char *image_path) {
    LOG_INFO("🖥️  Displaying image on e-ink: %s", image_path);
    
    // Ensure hardware is initialized
    if (init_eink_hardware() != 0) {
        LOG_ERROR("❌ Failed to initialize e-ink hardware");
        return -1;
    }
    
    // Create e-ink buffer using Waveshare method (same as working example)
    UDOUBLE imagesize = ((EPD_7IN5_V2_WIDTH % 8 == 0) ? (EPD_7IN5_V2_WIDTH / 8) : (EPD_7IN5_V2_WIDTH / 8 + 1)) * EPD_7IN5_V2_HEIGHT;
    UBYTE *BlackImage = (UBYTE *)malloc(imagesize);
    if (!BlackImage) {
        LOG_ERROR("❌ Failed to allocate e-ink buffer");
        return -1;
    }
    
    // Initialize Paint library with the buffer (same as working example)
    Paint_NewImage(BlackImage, EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT, 0, WHITE);
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    
    // Check file extension to determine how to load it
    const char *ext = strrchr(image_path, '.');
    if (ext && strcmp(ext, ".bmp") == 0) {
        // Use Waveshare's proven GUI_ReadBmp function for BMP files
        LOG_DEBUG("Loading BMP using GUI_ReadBmp: %s", image_path);
        GUI_ReadBmp(image_path, 0, 0);
    } else {
        // For other formats, we'd need conversion, but for now just error
        LOG_ERROR("❌ Unsupported image format: %s (only BMP supported for e-ink)", image_path);
        free(BlackImage);
        return -1;
    }
    
    // Display on e-ink using Waveshare method
    LOG_INFO("🖥️  Sending image to e-ink display...");
    EPD_7IN5_V2_Display(BlackImage);
    
    // Cleanup
    free(BlackImage);
    
    LOG_INFO("✅ Image displayed successfully on e-ink");
    return 0;
}

// ====================== PARTIAL DISPLAY FUNCTIONALITY ======================

// Global variables for partial refresh
static UBYTE *time_image_buffer = NULL;
static int partial_display_initialized = 0;

// Time display position in header section (matches dashboard_render.c layout)
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

    // Ensure hardware is initialized first
    if (!eink_hardware_initialized) {
        LOG_ERROR("❌ E-ink hardware must be initialized before partial display");
        return -1;
    }

    LOG_INFO("🔧 Initializing partial e-ink display for time updates...");
    
    // Switch to partial refresh mode
    if (EPD_7IN5_V2_Init_Part() != 0) {
        LOG_ERROR("❌ Failed to initialize e-paper for partial refresh");
        return -1;
    }
    
    // Allocate image buffer for time display area (with proper alignment)
    UDOUBLE image_size = ((TIME_WIDTH % 8 == 0) ? (TIME_WIDTH / 8) : (TIME_WIDTH / 8 + 1)) * TIME_HEIGHT;
    if ((time_image_buffer = (UBYTE *)malloc(image_size)) == NULL) {
        LOG_ERROR("❌ Failed to allocate memory for time image buffer");
        return -1;
    }
    LOG_DEBUG("Allocated %d bytes for time image buffer (%dx%d)", image_size, TIME_WIDTH, TIME_HEIGHT);
    
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
        // Ensure hardware is initialized first
        if (!eink_hardware_initialized && init_eink_hardware() != 0) {
            LOG_ERROR("❌ Failed to initialize e-ink hardware for partial refresh");
            return -1;
        }
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
        
        // Free image buffer
        if (time_image_buffer) {
            free(time_image_buffer);
            time_image_buffer = NULL;
        }
        
        partial_display_initialized = 0;
        LOG_INFO("✅ Partial display cleanup completed");
    }
}

int is_partial_display_available(void) {
    return partial_display_initialized;
}