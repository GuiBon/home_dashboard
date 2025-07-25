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

// ====================== CONSTANTS ======================

// BMP file format constants for monochrome (1-bit) bitmap
#define BMP_HEADER_SIZE 62  // 54 bytes + 8 bytes for color table
#define BMP_BITS_PER_PIXEL 1
#define BMP_COLOR_TABLE_SIZE 8  // 2 colors × 4 bytes each

// Time display positioning constants (matches dashboard_render.c layout)
// Time is centered at HEADER_X + HEADER_WIDTH/2, HEADER_Y + 65
// That's 5 + 470/2 = 240, 5 + 65 = 70
#define TIME_DISPLAY_X 190          // X position for time in header (centered around 240)
#define TIME_DISPLAY_Y 60           // Y position for time in header  
#define TIME_DISPLAY_WIDTH 100      // Width of time display area
#define TIME_DISPLAY_HEIGHT 30      // Height of time display area
#define TIME_FONT_CHAR_WIDTH 14     // Approximate character width for Font20
#define TIME_STRING_LENGTH 5        // "HH:MM" = 5 characters

// ====================== GLOBAL STATE ======================

// Hardware initialization state
static int eink_hardware_initialized = 0;

// Partial display state
static UBYTE *time_image_buffer = NULL;
static int partial_display_initialized = 0;

// ====================== BMP FILE GENERATION ======================

/**
 * Write little-endian 32-bit integer to buffer
 */
static void write_le32(unsigned char *buffer, int offset, int value) {
    buffer[offset] = value & 0xff;
    buffer[offset + 1] = (value >> 8) & 0xff;
    buffer[offset + 2] = (value >> 16) & 0xff;
    buffer[offset + 3] = (value >> 24) & 0xff;
}

/**
 * Write little-endian 16-bit integer to buffer
 */
static void write_le16(unsigned char *buffer, int offset, int value) {
    buffer[offset] = value & 0xff;
    buffer[offset + 1] = (value >> 8) & 0xff;
}

/**
 * Write Cairo surface as monochrome BMP file (1-bit, compatible with e-ink)
 */
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
    
    // Calculate row padding for 1-bit bitmap (rows must be aligned to 4 bytes)
    int bits_per_row = width;
    int bytes_per_row = (bits_per_row + 7) / 8;  // Round up to nearest byte
    int row_padded = (bytes_per_row + 3) & (~3); // Align to 4-byte boundary
    int pixel_data_size = row_padded * height;
    int file_size = BMP_HEADER_SIZE + pixel_data_size;
    
    // Create BMP header with color table
    unsigned char bmp_header[BMP_HEADER_SIZE] = {0};
    
    // BMP File Header (14 bytes)
    bmp_header[0] = 'B';
    bmp_header[1] = 'M';
    write_le32(bmp_header, 2, file_size);           // File size
    // Bytes 6-9: Reserved (already zeroed)
    write_le32(bmp_header, 10, BMP_HEADER_SIZE);    // Data offset (header + color table)
    
    // BMP Info Header (40 bytes)
    write_le32(bmp_header, 14, 40);                 // Header size
    write_le32(bmp_header, 18, width);              // Width
    write_le32(bmp_header, 22, height);             // Height
    write_le16(bmp_header, 26, 1);                  // Planes
    write_le16(bmp_header, 28, BMP_BITS_PER_PIXEL); // Bits per pixel (1)
    write_le32(bmp_header, 30, 0);                  // Compression (none)
    write_le32(bmp_header, 34, pixel_data_size);    // Image size
    write_le32(bmp_header, 38, 0);                  // X pixels per meter (0 = unspecified)
    write_le32(bmp_header, 42, 0);                  // Y pixels per meter (0 = unspecified)
    write_le32(bmp_header, 46, 2);                  // Colors used (2: black and white)
    write_le32(bmp_header, 50, 2);                  // Important colors (2)
    
    // Color table (8 bytes: 2 colors × 4 bytes each, BGRA format)
    // Color 0: Black (0x00000000)
    bmp_header[54] = 0x00; // Blue
    bmp_header[55] = 0x00; // Green  
    bmp_header[56] = 0x00; // Red
    bmp_header[57] = 0x00; // Alpha
    // Color 1: White (0x00FFFFFF)
    bmp_header[58] = 0xFF; // Blue
    bmp_header[59] = 0xFF; // Green
    bmp_header[60] = 0xFF; // Red
    bmp_header[61] = 0x00; // Alpha
    
    if (fwrite(bmp_header, 1, BMP_HEADER_SIZE, f) != BMP_HEADER_SIZE) {
        LOG_ERROR("❌ Failed to write BMP header");
        fclose(f);
        return 0;
    }
    
    // Allocate row buffer
    unsigned char *row_data = malloc(row_padded);
    if (!row_data) {
        LOG_ERROR("❌ Failed to allocate row buffer");
        fclose(f);
        return 0;
    }
    
    // Create error diffusion buffer for dithering
    float *error_buffer = calloc(width + 2, sizeof(float)); // +2 for boundary handling
    if (!error_buffer) {
        LOG_ERROR("❌ Failed to allocate error buffer for dithering");
        free(row_data);
        fclose(f);
        return 0;
    }
    
    // Write pixel data (BMP is bottom-up, 1-bit packed format with Floyd-Steinberg dithering)
    for (int y = height - 1; y >= 0; y--) {
        // Clear row buffer and reset error buffer for new row
        memset(row_data, 0, row_padded);
        memset(error_buffer, 0, (width + 2) * sizeof(float));
        
        // Convert ARGB32 to 1-bit monochrome with dithering
        for (int x = 0; x < width; x++) {
            int cairo_offset = y * stride + x * 4;
            
            // Get RGB values from Cairo ARGB32 format: B-G-R-A
            unsigned char blue = data[cairo_offset];
            unsigned char green = data[cairo_offset + 1];
            unsigned char red = data[cairo_offset + 2];
            
            // Convert to grayscale using standard weights
            float gray = 0.299f * red + 0.587f * green + 0.114f * blue;
            
            // Apply accumulated error from previous pixels
            gray += error_buffer[x + 1]; // +1 for boundary offset
            
            // Clamp to valid range
            if (gray < 0) gray = 0;
            if (gray > 255) gray = 255;
            
            // Quantize: > 128 = white (255), <= 128 = black (0)
            int quantized = (gray > 128) ? 255 : 0;
            int bit_value = (quantized > 0) ? 1 : 0;
            
            // Calculate quantization error
            float error = gray - quantized;
            
            // Distribute error to neighboring pixels (Floyd-Steinberg pattern)
            // Current pixel is at (x, y), distribute error to:
            // - Right pixel: 7/16 of error
            // - Bottom-left pixel: 3/16 of error  
            // - Bottom pixel: 5/16 of error
            // - Bottom-right pixel: 1/16 of error
            if (x + 1 < width) {
                error_buffer[x + 2] += error * 7.0f / 16.0f; // Right pixel
            }
            // Note: For bottom row pixels, we'd need a 2D error buffer
            // For simplicity, we're only doing horizontal error diffusion
            // which still provides good dithering results
            
            // Pack into bit array (MSB first)
            int byte_index = x / 8;
            int bit_index = 7 - (x % 8);
            
            if (bit_value) {
                row_data[byte_index] |= (1 << bit_index);
            }
        }
        
        if (fwrite(row_data, 1, row_padded, f) != (size_t)row_padded) {
            LOG_ERROR("❌ Failed to write BMP row data");
            free(error_buffer);
            free(row_data);
            fclose(f);
            return 0;
        }
    }
    
    free(error_buffer);
    
    free(row_data);
    fclose(f);
    return 1;
}

/**
 * Generate dashboard as BMP file for e-ink display
 */
int generate_dashboard_bmp(const char *filename, time_t display_date, 
                          const WeatherData *weather_data, 
                          const MenuData *menu_data, 
                          const CalendarData *calendar_data) {
    
    if (!filename) {
        LOG_ERROR("❌ Invalid filename for BMP generation");
        return 0;
    }
    
    // Create surface in RGB24 format for BMP compatibility
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, EINK_WIDTH, EINK_HEIGHT);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        LOG_ERROR("❌ Failed to create Cairo surface for BMP generation");
        return 0;
    }
    
    cairo_t *cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        LOG_ERROR("❌ Failed to create Cairo context for BMP generation");
        cairo_surface_destroy(surface);
        return 0;
    }
    
    // Initialize fonts
    if (!init_dashboard_fonts()) {
        LOG_ERROR("❌ Failed to initialize fonts for BMP generation");
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return 0;
    }
    
    // Render dashboard to surface
    render_dashboard_to_surface(surface, display_date, weather_data, menu_data, calendar_data);
    
    // Ensure all drawing operations are completed
    cairo_surface_flush(surface);
    
    // Write as BMP
    int success = write_surface_as_bmp(surface, filename);
    
    // Cleanup Cairo objects
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    
    if (success) {
        LOG_INFO("✅ Dashboard BMP generated successfully: %s", filename);
    } else {
        LOG_ERROR("❌ Failed to write BMP file: %s", filename);
    }
    
    return success;
}

// ====================== E-INK HARDWARE MANAGEMENT ======================

/**
 * Initialize Waveshare e-ink hardware
 * Returns: 0 on success, -1 on failure
 */
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
    
    // Initialize e-paper display with full initialization
    if (EPD_7IN5_V2_Init() != 0) {
        LOG_ERROR("❌ Failed to initialize e-paper display");
        DEV_Module_Exit();
        return -1;
    }
    
    // Clear display to ensure known state
    LOG_DEBUG("Clearing display for hardware initialization...");
    EPD_7IN5_V2_Clear();
    
    eink_hardware_initialized = 1;
    LOG_INFO("✅ E-ink hardware initialized successfully");
    return 0;
}

/**
 * Cleanup Waveshare e-ink hardware
 */
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

/**
 * Display BMP image on e-ink screen with specified refresh type
 * Returns: 0 on success, -1 on failure
 */
int display_image_on_eink_with_refresh_type(const char *image_path, RefreshType refresh_type) {
    if (!image_path) {
        LOG_ERROR("❌ Invalid image path");
        return -1;
    }
    
    const char *refresh_names[] = {"full", "fast", "partial"};
    LOG_INFO("🖥️  Displaying image on e-ink (%s refresh): %s", refresh_names[refresh_type], image_path);
    
    // Ensure hardware is initialized
    if (init_eink_hardware() != 0) {
        LOG_ERROR("❌ Failed to initialize e-ink hardware");
        return -1;
    }
    
    // Validate file format
    const char *ext = strrchr(image_path, '.');
    if (!ext || strcmp(ext, ".bmp") != 0) {
        LOG_ERROR("❌ Unsupported image format: %s (only BMP supported for e-ink)", image_path);
        return -1;
    }
    
    // Initialize display for the specified refresh type
    switch (refresh_type) {
        case REFRESH_FULL:
            LOG_DEBUG("Initializing e-ink for full refresh...");
            if (EPD_7IN5_V2_Init() != 0) {
                LOG_ERROR("❌ Failed to initialize e-ink for full refresh");
                return -1;
            }
            break;
            
        case REFRESH_FAST:
            LOG_DEBUG("Initializing e-ink for fast refresh...");
            if (EPD_7IN5_V2_Init_Fast() != 0) {
                LOG_ERROR("❌ Failed to initialize e-ink for fast refresh");
                return -1;
            }
            break;
            
        case REFRESH_PARTIAL:
            LOG_DEBUG("Initializing e-ink for partial refresh...");
            if (EPD_7IN5_V2_Init_Part() != 0) {
                LOG_ERROR("❌ Failed to initialize e-ink for partial refresh");
                return -1;
            }
            break;
            
        default:
            LOG_ERROR("❌ Invalid refresh type: %d", refresh_type);
            return -1;
    }
    
    // Calculate buffer size for e-ink display
    UDOUBLE imagesize = ((EPD_7IN5_V2_WIDTH % 8 == 0) ? 
                         (EPD_7IN5_V2_WIDTH / 8) : 
                         (EPD_7IN5_V2_WIDTH / 8 + 1)) * EPD_7IN5_V2_HEIGHT;
    
    UBYTE *BlackImage = (UBYTE *)malloc(imagesize);
    if (!BlackImage) {
        LOG_ERROR("❌ Failed to allocate e-ink buffer (%lu bytes)", imagesize);
        return -1;
    }
    
    // Initialize Paint library with the buffer
    Paint_NewImage(BlackImage, EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT, 0, WHITE);
    Paint_SelectImage(BlackImage);
    Paint_Clear(WHITE);
    
    // Load BMP file using Waveshare's GUI_ReadBmp function
    LOG_DEBUG("Loading BMP using GUI_ReadBmp: %s", image_path);
    GUI_ReadBmp(image_path, 0, 0);
    
    // Display on e-ink using appropriate method
    LOG_INFO("🖥️  Sending image to e-ink display (%s refresh)...", refresh_names[refresh_type]);
    
    if (refresh_type == REFRESH_PARTIAL) {
        // For partial refresh, update entire screen (could be optimized to update specific regions)
        EPD_7IN5_V2_Display_Part(BlackImage, 0, 0, EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT);
    } else {
        // For full and fast refresh, use standard display method
        EPD_7IN5_V2_Display(BlackImage);
    }
    
    // Cleanup
    free(BlackImage);
    
    LOG_INFO("✅ Image displayed successfully on e-ink (%s refresh)", refresh_names[refresh_type]);
    return 0;
}

/**
 * Display BMP image on e-ink screen (default: full refresh)
 * Returns: 0 on success, -1 on failure
 */
int display_image_on_eink(const char *image_path) {
    return display_image_on_eink_with_refresh_type(image_path, REFRESH_FULL);
}

// ====================== PARTIAL DISPLAY FUNCTIONALITY ======================

/**
 * Initialize partial display system for fast time updates
 * Returns: 0 on success, -1 on failure
 */
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
    
    // Calculate buffer size for time display area (with proper 8-bit alignment)
    UDOUBLE image_size = ((TIME_DISPLAY_WIDTH % 8 == 0) ? 
                          (TIME_DISPLAY_WIDTH / 8) : 
                          (TIME_DISPLAY_WIDTH / 8 + 1)) * TIME_DISPLAY_HEIGHT;
    
    time_image_buffer = (UBYTE *)malloc(image_size);
    if (!time_image_buffer) {
        LOG_ERROR("❌ Failed to allocate memory for time image buffer (%lu bytes)", image_size);
        return -1;
    }
    
    LOG_DEBUG("Allocated %lu bytes for time image buffer (%dx%d)", 
              image_size, TIME_DISPLAY_WIDTH, TIME_DISPLAY_HEIGHT);
    
    // Initialize paint library with time buffer
    Paint_NewImage(time_image_buffer, TIME_DISPLAY_WIDTH, TIME_DISPLAY_HEIGHT, 0, WHITE);
    Paint_SelectImage(time_image_buffer);
    Paint_Clear(WHITE);
    
    partial_display_initialized = 1;
    LOG_INFO("✅ Partial display initialized successfully");
    return 0;
}

/**
 * Refresh time display using partial e-ink update
 * Returns: 0 on success, -1 on failure
 */
int refresh_time_partial(void) {
    // Auto-initialize if not already done
    if (!partial_display_initialized) {
        LOG_DEBUG("⚠️  Partial display not initialized, initializing now...");
        
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
    if (!tm_info) {
        LOG_ERROR("❌ Failed to get current time");
        return -1;
    }
    
    // Format time string
    char time_str[16];
    if (strftime(time_str, sizeof(time_str), "%H:%M", tm_info) == 0) {
        LOG_ERROR("❌ Failed to format time string");
        return -1;
    }
    
    // Clear the time display area in buffer
    Paint_Clear(WHITE);
    
    // Calculate centered position for time text
    int text_width_approx = TIME_STRING_LENGTH * TIME_FONT_CHAR_WIDTH;
    int text_x = (TIME_DISPLAY_WIDTH - text_width_approx) / 2;
    int text_y = 5; // Small margin from top
    
    // Draw the updated time (centered in buffer)
    Paint_DrawString_EN(text_x, text_y, time_str, &Font20, WHITE, BLACK);
    
    // Perform partial refresh of the time area only
    EPD_7IN5_V2_Display_Part(time_image_buffer, 
                             TIME_DISPLAY_X, TIME_DISPLAY_Y, 
                             TIME_DISPLAY_X + TIME_DISPLAY_WIDTH, 
                             TIME_DISPLAY_Y + TIME_DISPLAY_HEIGHT);
    
    return 0;
}

/**
 * Cleanup partial display resources
 */
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

/**
 * Check if partial display is available and initialized
 * Returns: 1 if available, 0 if not available
 */
int is_partial_display_available(void) {
    return partial_display_initialized;
}