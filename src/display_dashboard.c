#define _GNU_SOURCE
#include "display_dashboard.h"
#include "dashboard_render.h"
#include "logging.h"
#include <cairo.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

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

// Time display positioning constants (matches dashboard_render.c layout exactly)
// From draw_header_section: time is at HEADER_X + HEADER_WIDTH/2, HEADER_Y + 65
// That's 5 + 470/2, 5 + 65 = (240, 70) in portrait
// For 90° clockwise rotation: (x,y) -> (y, height-1-x)  
// So (240, 70) -> (70, 800-1-240) = (70, 559) - Wait, that doesn't match either
// Let me use the actual observed position: time at far left (~50) and middle height (~260)
#define TIME_DISPLAY_X_PORTRAIT 240  // Original X in portrait (HEADER_X + HEADER_WIDTH/2)
#define TIME_DISPLAY_Y_PORTRAIT 70   // Original Y in portrait (HEADER_Y + 65)
#define TIME_DISPLAY_X_ROTATED 50    // Observed position: far left of landscape screen
#define TIME_DISPLAY_Y_ROTATED 240   // Observed position: middle height (matches calculation)
#define TIME_DISPLAY_WIDTH 120       // Wide enough for "HH:MM" text
#define TIME_DISPLAY_HEIGHT 40       // Tall enough for Font24
#define TIME_FONT_CHAR_WIDTH 14      // Approximate character width for Font24
#define TIME_STRING_LENGTH 5         // "HH:MM" = 5 characters

// ====================== GLOBAL STATE ======================

// Hardware initialization state
static int eink_hardware_initialized = 0;

// E-ink mode tracking
typedef enum {
    EINK_MODE_NONE = 0,
    EINK_MODE_FULL,
    EINK_MODE_FAST, 
    EINK_MODE_PARTIAL
} EinkMode;

static EinkMode current_eink_mode = EINK_MODE_NONE;

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
 * Write RGB24 Cairo surface as monochrome BMP without rotation
 */
static int write_surface_as_bmp_no_rotate(cairo_surface_t *surface, const char *filename) {
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    
    FILE *f = fopen(filename, "wb");
    if (!f) {
        LOG_ERROR("❌ Failed to create BMP file: %s", filename);
        return 0;
    }
    
    // Calculate row padding (rows must be aligned to 4 bytes)
    int bits_per_row = width;
    int bytes_per_row = (bits_per_row + 7) / 8;
    int row_padded = (bytes_per_row + 3) & (~3);
    int pixel_data_size = row_padded * height;
    int file_size = BMP_HEADER_SIZE + pixel_data_size;
    
    // Create BMP header
    unsigned char bmp_header[BMP_HEADER_SIZE] = {0};
    bmp_header[0] = 'B'; bmp_header[1] = 'M';
    write_le32(bmp_header, 2, file_size);
    write_le32(bmp_header, 10, BMP_HEADER_SIZE);
    write_le32(bmp_header, 14, 40);
    write_le32(bmp_header, 18, width);
    write_le32(bmp_header, 22, height);
    write_le16(bmp_header, 26, 1);
    write_le16(bmp_header, 28, 1);
    write_le32(bmp_header, 34, pixel_data_size);
    write_le32(bmp_header, 46, 2);
    write_le32(bmp_header, 50, 2);
    
    // Color table: Black and White
    bmp_header[58] = 0xFF; bmp_header[59] = 0xFF; bmp_header[60] = 0xFF;
    
    fwrite(bmp_header, 1, BMP_HEADER_SIZE, f);
    
    // Write pixel data (no rotation, convert RGB24 to monochrome with Floyd-Steinberg dithering)
    unsigned char *row_data = malloc(row_padded);
    
    for (int y = height - 1; y >= 0; y--) {
        memset(row_data, 0, row_padded);
        for (int x = 0; x < width; x++) {
            int cairo_offset = y * stride + x * 4;
            unsigned char blue = data[cairo_offset];
            unsigned char green = data[cairo_offset + 1];
            unsigned char red = data[cairo_offset + 2];
            
            // Convert to grayscale and quantize
            float gray = 0.299f * red + 0.587f * green + 0.114f * blue;
            int bit_value = (gray > 128) ? 1 : 0;
            
            int byte_index = x / 8;
            int bit_index = 7 - (x % 8);
            if (bit_value) {
                row_data[byte_index] |= (1 << bit_index);
            }
        }
        fwrite(row_data, 1, row_padded, f);
    }
    
    free(row_data);
    fclose(f);
    return 1;
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
    
    // Calculate rotated dimensions for buffer allocation
    int rotated_width_for_buffer = height;   // Will be used for rotated image
    
    // Create error diffusion buffer for dithering (sized for rotated image)
    float *error_buffer = calloc(rotated_width_for_buffer + 2, sizeof(float)); // +2 for boundary handling
    if (!error_buffer) {
        LOG_ERROR("❌ Failed to allocate error buffer for dithering");
        free(row_data);
        fclose(f);
        return 0;
    }
    
    // Write pixel data (BMP is bottom-up, 1-bit packed format with Floyd-Steinberg dithering)
    // Rotate image 90° clockwise to convert portrait (480x800) to landscape (800x480)
    int rotated_width = height;   // Rotated width = original height
    int rotated_height = width;   // Rotated height = original width
    int rotated_bytes_per_row = (rotated_width + 7) / 8;
    int rotated_row_padded = (rotated_bytes_per_row + 3) & (~3);
    
    // Update file size for rotated dimensions
    fseek(f, 2, SEEK_SET);
    int rotated_pixel_data_size = rotated_row_padded * rotated_height;
    int rotated_file_size = BMP_HEADER_SIZE + rotated_pixel_data_size;
    write_le32((unsigned char*)&rotated_file_size, 0, rotated_file_size);
    fwrite(&rotated_file_size, 4, 1, f);
    
    // Update width/height in header
    fseek(f, 18, SEEK_SET);
    write_le32((unsigned char*)&rotated_width, 0, rotated_width);
    fwrite(&rotated_width, 4, 1, f);
    write_le32((unsigned char*)&rotated_height, 0, rotated_height);
    fwrite(&rotated_height, 4, 1, f);
    
    // Update image size in header
    fseek(f, 34, SEEK_SET);
    write_le32((unsigned char*)&rotated_pixel_data_size, 0, rotated_pixel_data_size);
    fwrite(&rotated_pixel_data_size, 4, 1, f);
    
    // Seek back to data section
    fseek(f, BMP_HEADER_SIZE, SEEK_SET);
    
    // Reallocate row buffer for rotated dimensions
    free(row_data);
    row_data = malloc(rotated_row_padded);
    if (!row_data) {
        LOG_ERROR("❌ Failed to allocate rotated row buffer");
        free(error_buffer);
        fclose(f);
        return 0;
    }
    
    // Write rotated pixel data (BMP is bottom-up)
    for (int y = rotated_height - 1; y >= 0; y--) {
        // Clear row buffer
        memset(row_data, 0, rotated_row_padded);
        
        // Reset error buffer for new row (only reset current and next pixel)
        for (int i = 0; i < rotated_width + 2; i++) {
            error_buffer[i] = 0.0f;
        }
        
        // Convert rotated coordinates back to original image coordinates
        for (int x = 0; x < rotated_width; x++) {
            // Rotation mapping: rotated(x,y) = original(original_width-1-y, x)
            int orig_x = width - 1 - y;   // Original x from rotated y
            int orig_y = x;               // Original y from rotated x
            
            // Skip if outside original bounds
            if (orig_x < 0 || orig_x >= width || orig_y < 0 || orig_y >= height) {
                continue;
            }
            
            int cairo_offset = orig_y * stride + orig_x * 4;
            
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
            if (x + 1 < rotated_width) {
                error_buffer[x + 2] += error * 7.0f / 16.0f; // Right pixel
            }
            
            // Pack into bit array (MSB first)
            int byte_index = x / 8;
            int bit_index = 7 - (x % 8);
            
            if (bit_value) {
                row_data[byte_index] |= (1 << bit_index);
            }
        }
        
        if (fwrite(row_data, 1, rotated_row_padded, f) != (size_t)rotated_row_padded) {
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

// ====================== E-INK MODE MANAGEMENT ======================

/**
 * Switch e-ink to the specified mode only if not already in that mode
 * Returns: 0 on success, -1 on failure
 */
static int switch_eink_mode(RefreshType refresh_type) {
    EinkMode target_mode;
    const char *mode_name;
    
    // Map refresh type to eink mode
    switch (refresh_type) {
        case REFRESH_FULL:
            target_mode = EINK_MODE_FULL;
            mode_name = "full";
            break;
        case REFRESH_FAST:
            target_mode = EINK_MODE_FAST;
            mode_name = "fast";
            break;
        case REFRESH_PARTIAL:
            target_mode = EINK_MODE_PARTIAL;
            mode_name = "partial";
            break;
        default:
            LOG_ERROR("❌ Invalid refresh type: %d", refresh_type);
            return -1;
    }
    
    // Skip if already in target mode
    if (current_eink_mode == target_mode) {
        LOG_DEBUG("E-ink already in %s mode, skipping initialization", mode_name);
        return 0;
    }
    
    LOG_DEBUG("Switching e-ink from mode %d to %s mode", current_eink_mode, mode_name);
    
    // Perform mode switch
    switch (target_mode) {
        case EINK_MODE_FULL:
            if (EPD_7IN5_V2_Init() != 0) {
                LOG_ERROR("❌ Failed to initialize e-ink for full refresh");
                return -1;
            }
            break;
            
        case EINK_MODE_FAST:
            if (EPD_7IN5_V2_Init_Fast() != 0) {
                LOG_ERROR("❌ Failed to initialize e-ink for fast refresh");
                return -1;
            }
            break;
            
        case EINK_MODE_PARTIAL:
            if (EPD_7IN5_V2_Init_Part() != 0) {
                LOG_ERROR("❌ Failed to initialize e-ink for partial refresh");
                return -1;
            }
            break;
            
        default:
            return -1;
    }
    
    current_eink_mode = target_mode;
    LOG_DEBUG("✅ E-ink switched to %s mode successfully", mode_name);
    return 0;
}

// ====================== E-INK HARDWARE MANAGEMENT ======================

/**
 * Initialize Waveshare e-ink hardware and set initial mode
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
    current_eink_mode = EINK_MODE_FULL; // Track that we're in full mode after Init()
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
        current_eink_mode = EINK_MODE_NONE;  // Reset mode tracking
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
    
    // Switch to the appropriate e-ink mode
    if (switch_eink_mode(refresh_type) != 0) {
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
 * Initialize partial display buffer for fast time updates - Based on working display_time_test.c
 * Returns: 0 on success, -1 on failure
 */
static int init_partial_buffer(void) {
    if (partial_display_initialized) {
        return 0; // Already initialized
    }

    LOG_INFO("🔧 Initializing partial display buffer for time updates...");
    
    // Calculate buffer size exactly like working display_time_test.c - use full display buffer
    UDOUBLE image_size = ((EPD_7IN5_V2_WIDTH % 8 == 0) ? 
                          (EPD_7IN5_V2_WIDTH / 8) : 
                          (EPD_7IN5_V2_WIDTH / 8 + 1)) * EPD_7IN5_V2_HEIGHT;
    
    time_image_buffer = (UBYTE *)malloc(image_size);
    if (!time_image_buffer) {
        LOG_ERROR("❌ Failed to allocate memory for time image buffer (%lu bytes)", image_size);
        return -1;
    }
    
    LOG_DEBUG("Allocated %lu bytes for time image buffer (full display buffer)", image_size);
    
    // Initialize paint library exactly like display_time_test.c - TWO STEP PROCESS
    // Step 1: Initialize with full display dimensions (like line 31 in test)
    Paint_NewImage(time_image_buffer, EPD_7IN5_V2_WIDTH, EPD_7IN5_V2_HEIGHT, 0, WHITE);
    
    // Step 2: Reconfigure for rotated text area (Font24 with extra padding for safety) 
    Paint_SelectImage(time_image_buffer);
    Paint_Clear(WHITE);
    
    partial_display_initialized = 1;
    LOG_INFO("✅ Partial display buffer initialized successfully");
    return 0;
}

/**
 * Refresh time display using partial e-ink update - Based on working display_time_test.c
 * Returns: 0 on success, -1 on failure
 */
int refresh_time_partial(void) {
    // Ensure hardware is initialized first
    if (init_eink_hardware() != 0) {
        return -1;
    }
    
    // Auto-initialize buffer if not already done
    if (init_partial_buffer() != 0) {
        return -1;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (!tm_info) {
        LOG_ERROR("❌ Failed to get current time");
        return -1;
    }
    
    // Format time string as "HH:MM"
    char time_str[6];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    
    LOG_DEBUG("Drawing time '%s' using Paint_DrawString_EN", time_str);
    
    // Ensure we're in partial mode (switch if needed)
    if (switch_eink_mode(REFRESH_PARTIAL) != 0) {
        return -1;
    }
    
    int area_width = 120;
    int area_height = 30;
    int height_start = 40;
    int width_start = (EINK_WIDTH - area_width) / 2;

    // Select our time buffer (like display_time_test.c)
    Paint_NewImage(time_image_buffer, area_height, area_width, ROTATE_270, WHITE);
    Paint_SelectImage(time_image_buffer);
    
    // Create Cairo surface for time rendering (RGB24 format like main dashboard)
    cairo_surface_t *time_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, area_width - 2, area_height - 2);
    if (cairo_surface_status(time_surface) != CAIRO_STATUS_SUCCESS) {
        LOG_ERROR("❌ Failed to create Cairo surface for time");
        return -1;
    }
    
    // Create Cairo context
    cairo_t *cr = cairo_create(time_surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        LOG_ERROR("❌ Failed to create Cairo context for time");
        cairo_surface_destroy(time_surface);
        return -1;
    }
    
    // Get current time and render clock (handles fonts, background and text)
    time_t current_time = time(NULL);
    if (render_clock_to_surface(cr, current_time, area_width, area_height) != 0) {
        LOG_ERROR("❌ Failed to render clock to surface");
        cairo_destroy(cr);
        cairo_surface_destroy(time_surface);
        return -1;
    }
    
    // Flush and save as BMP (use RGB24 writer without rotation)
    cairo_surface_flush(time_surface);
    const char *temp_time_bmp = "/tmp/partial_time.bmp";
    if (!write_surface_as_bmp_no_rotate(time_surface, temp_time_bmp)) {
        LOG_ERROR("❌ Failed to write time BMP");
        cairo_destroy(cr);
        cairo_surface_destroy(time_surface);
        return -1;
    }
    
    // Cleanup Cairo objects
    cairo_destroy(cr);
    cairo_surface_destroy(time_surface);
    
    // Clear the area and load Cairo-generated BMP (load at origin to cover full area)
    Paint_ClearWindows(0, 0, area_width, area_height, WHITE);  // Clear inside border
    printf("Loading BMP: %s at position (0,0), area: %dx%d\n", temp_time_bmp, area_width, area_height);
    UBYTE bmp_result = GUI_ReadBmp(temp_time_bmp, 0, 5);  // Load BMP at origin
    printf("GUI_ReadBmp result: %d\n", bmp_result);
    
    // Perform partial update with coordinates (Font24 with padding)
    EPD_7IN5_V2_Display_Part(time_image_buffer, height_start, width_start, 
                             height_start + area_height, width_start + area_width);
    
    LOG_DEBUG("⏰ Time display updated via partial refresh: %s", time_str);
    
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
