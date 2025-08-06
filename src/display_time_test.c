#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"
#include "GUI_BMPfile.h"
#include "DEV_Config.h"

// Display dimensions - native landscape is 800x480
// For potrait we'll use full dimensions and rotate content
#define EPD_WIDTH_NATIVE    800
#define EPD_HEIGHT_NATIVE   480

// Portrait orientation dimensions (when held vertically)
#define EPD_WIDTH_PORTRAIT    480
#define EPD_HEIGHT_PORTRAIT   800

// Time display area (centered at top of screen in portrait coordinates)
#define TIME_X      80
#define TIME_Y      100
#define TIME_WIDTH  320
#define TIME_HEIGHT 100

// Global variables
static UBYTE *ImageBuffer;
static UWORD ImageSize;
static int partial_update_count = 0;

// Function to initialize the display buffer for portrait mode
int init_portrait_display() {
    // Calculate image size based on native display dimensions
    ImageSize = ((EPD_WIDTH_NATIVE % 8 == 0) ? (EPD_WIDTH_NATIVE / 8) : (EPD_WIDTH_NATIVE / 8 + 1)) * EPD_HEIGHT_NATIVE;

    ImageBuffer = (UBYTE *)malloc(ImageSize);
    if (ImageBuffer == NULL) {
        printf("Failed to allocate memory for image buffer\r\n");
        return -1;
    }

    // Initialize paint with native dimensions but rotate 270 degrees
    // This makes the content appear horizontal when screen is held in portrait
    Paint_NewImage(ImageBuffer, EPD_WIDTH_NATIVE, EPD_HEIGHT_NATIVE, ROTATE_270, WHITE);
    Paint_SelectImage(ImageBuffer);
    
    // Explicitly clear the entire buffer to WHITE to ensure proper initialization
    Paint_Clear(WHITE);
    printf("DEBUG: Buffer initialized and cleared to WHITE\r\n");

    return 0;
}

// Function to clear a specific area
void clear_area(UWORD x, UWORD y, UWORD width, UWORD height) {
    Paint_ClearWindows(x, y, x + width, y + height, WHITE);
}

// Function to draw time with large font - only HH:MM format
void draw_time(struct tm *timeinfo) {
    char time_str[8];

    // Format time in 24-hour format (HH:MM)
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);

    printf("DEBUG: Drawing time '%s'\r\n", time_str);
    printf("DEBUG: Clear area: (%d,%d) size %dx%d\r\n", TIME_X, TIME_Y, TIME_WIDTH, TIME_HEIGHT);

    // Clear the time area first
    clear_area(TIME_X, TIME_Y, TIME_WIDTH, TIME_HEIGHT);

    printf("DEBUG: Drawing text at position (%d,%d)\r\n", TIME_X + 50, TIME_Y + 25);

    // Draw time centered (large font) - BLACK text on WHITE background
    Paint_DrawString_EN(TIME_X + 50, TIME_Y + 25, time_str, &Font24, BLACK, WHITE);
    
    printf("DEBUG: Time drawing completed\r\n");
}

// Function to perform partial update - FIXED VERSION
void partial_update_display() {
    // Since full display works, the issue is coordinate transformation for partial updates
    // The time appears at top-left in portrait mode (TIME_X=80, TIME_Y=100)
    // With ROTATE_270, we need to map this correctly to native landscape coordinates
    
    // For ROTATE_270: portrait(x,y) -> native(HEIGHT-1-y, x) 
    // But for regions, we need to account for the actual drawing area
    
    printf("DEBUG: Using corrected coordinate transformation...\r\n");
    
    // Since the time displays correctly at top-left, let's use direct native coordinates
    // where we know the time appears in landscape mode
    
    // Time appears at top-left in portrait, which in native landscape should be:
    // - Left edge of landscape = top of portrait
    // - Top edge of landscape = left side of portrait  
    
    // Based on your description: narrow vertical stripe instead of wide horizontal rectangle
    // This means width/height are swapped. Let me fix this:
    
    UWORD native_x = 80;  // Position where narrow stripe appeared
    UWORD native_y = 100;   // Left edge in landscape (top in portrait)
    UWORD native_width = 100;   // Short dimension (was showing as narrow)
    UWORD native_height = 400; // Long dimension (was showing top-to-bottom)
    
    printf("DEBUG: Using direct native coordinates: x=%d, y=%d, w=%d, h=%d\r\n", 
           native_x, native_y, native_width, native_height);
    
    // Initialize partial mode
    EPD_7IN5_V2_Init_Part();
    
    // Perform partial update
    EPD_7IN5_V2_Display_Part(ImageBuffer, native_x, native_y, native_x + native_width, native_y + native_height);
    
    partial_update_count++;
}

// Function to draw static elements (only needed once or after full refresh)
void draw_static_element() {
    // Draw decorative border around time area
    Paint_DrawRectangle(TIME_X - 10, TIME_Y - 10, TIME_X + TIME_WIDTH + 10, TIME_Y + TIME_HEIGHT + 10, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
}

int main(void) {
    printf("Starting Waveshare 7.5\" E-ink Portrait Clock\r\n");

    // Initialize device
    if (DEV_Module_Init() != 0) {
        printf("Device initialization failed\r\n");
        return -1;
    }

    // Initialize display buffer
    if (init_portrait_display() != 0) {
        DEV_Module_Exit();
        return -1;
    }

    printf("Initializing e-Paper display...\r\n");
    EPD_7IN5_V2_Init();
    EPD_7IN5_V2_Clear();

    // Initial full screen setup
    Paint_Clear(WHITE);
    draw_static_element();

    // Get initial time and draw it
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    draw_time(timeinfo);

    // Initial full display
    printf("Displaying initial screen...\r\n");
    EPD_7IN5_V2_Display(ImageBuffer);

    printf("Starting time update loop (Ctrl+C to exit)...\r\n");

    // Main loop - update time every minute (since we only show HH:MM)
    while (1) {
        DEV_Delay_ms(60000); // Wait 1 minute

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        // Update time display
        draw_time(timeinfo);

        // Use partial update for time changes
        partial_update_display();

        // Print current time to console
        printf("Updated: %02d:%02d\r\n", timeinfo->tm_hour, timeinfo->tm_min);
    }

    // Cleanup (this code won't be reached due to infinite loop)
    printf("Cleaning up...\r\n");
    free(ImageBuffer);
    EPD_7IN5_V2_Sleep();
    DEV_Module_Exit();

    return 0;
}

// Signal handler for graceful shutdown (optional enhancement)
#include <signal.h>

void signal_handler(int sig) {
    printf("\nReceived signal %d, cleaning up...\r\n", sig);
    if (ImageBuffer) {
        free(ImageBuffer);
    }
    EPD_7IN5_V2_Sleep();
    DEV_Module_Exit();
    exit(0);
}

// Enhanced main with signal handling
void setup_signal_handlers() {
    signal(SIGINT, signal_handler);     // Ctrl+C
    signal(SIGTERM, signal_handler);    // Termination request
}