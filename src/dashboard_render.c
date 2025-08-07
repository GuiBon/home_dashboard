#define _GNU_SOURCE
#include "dashboard_render.h"
#include "logging.h"
#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ====================== CONSTANTS ======================

// Layout constants
#define SECTION_MARGIN 10
#define SECTION_TITLE_Y_OFFSET 22
#define SECTION_TITLE_SEPARATOR_Y 30
#define ICON_VERTICAL_OFFSET 3

// Text buffer sizes
#define MAX_TEXT_BUFFER 1024
#define MAX_LINE_BUFFER 512
#define MAX_ICON_BUFFER 16
#define MAX_TIME_BUFFER 16
#define MAX_DATE_BUFFER 128
#define MAX_TITLE_BUFFER 64

// Weather section layout
#define WEATHER_LEFT_SECTION_WIDTH 220
#define WEATHER_ICON_TEMP_SPACING 15
#define WEATHER_FORECAST_COLUMNS 2
#define WEATHER_FORECAST_ITEMS_PER_COL 6
#define WEATHER_FORECAST_LINE_HEIGHT 18

// Menu/Calendar layout
#define COLUMN_PADDING 5
#define COLUMN_GAP 20
#define MENU_ITEM_LINE_HEIGHT 16
#define CALENDAR_ITEM_LINE_HEIGHT 18
#define CALENDAR_EVENT_GAP 3

// ====================== LOCALIZATION DATA ======================

// French localization arrays
const char* const french_days[7] = {
    "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"
};

const char* const french_months[12] = {
    "janvier", "février", "mars", "avril", "mai", "juin",
    "juillet", "août", "septembre", "octobre", "novembre", "décembre"
};

// ====================== FONT MANAGEMENT ======================

// Font structure for caching loaded fonts
typedef struct {
    cairo_font_face_t *regular;
    cairo_font_face_t *bold;
    cairo_font_face_t *material;
    FT_Library ft_library;
    FT_Face ft_regular;
    FT_Face ft_bold;
    FT_Face ft_material;
    int initialized;
} FontManager;

static FontManager g_fonts = {0};

/**
 * Initialize font manager and load all required fonts
 * Returns: 1 on success, 0 on failure
 */
int init_dashboard_fonts(void) {
    if (g_fonts.initialized) {
        return 1; // Already initialized
    }
    
    LOG_DEBUG("🔤 Loading fonts...");
    
    // Initialize FreeType library
    if (FT_Init_FreeType(&g_fonts.ft_library)) {
        LOG_ERROR("❌ Failed to initialize FreeType library");
        return 0;
    }
    
    // Load Liberation Sans Regular
    if (FT_New_Face(g_fonts.ft_library, FONT_LIBERATION_REGULAR, 0, &g_fonts.ft_regular)) {
        LOG_ERROR("❌ Failed to load Liberation Sans Regular: %s", FONT_LIBERATION_REGULAR);
        FT_Done_FreeType(g_fonts.ft_library);
        return 0;
    }
    
    // Load Liberation Sans Bold
    if (FT_New_Face(g_fonts.ft_library, FONT_LIBERATION_BOLD, 0, &g_fonts.ft_bold)) {
        LOG_ERROR("❌ Failed to load Liberation Sans Bold: %s", FONT_LIBERATION_BOLD);
        FT_Done_Face(g_fonts.ft_regular);
        FT_Done_FreeType(g_fonts.ft_library);
        return 0;
    }
    
    // Load Material Symbols font
    if (FT_New_Face(g_fonts.ft_library, FONT_MATERIAL_SYMBOLS, 0, &g_fonts.ft_material)) {
        LOG_ERROR("❌ Failed to load Material Symbols font: %s", FONT_MATERIAL_SYMBOLS);
        FT_Done_Face(g_fonts.ft_bold);
        FT_Done_Face(g_fonts.ft_regular);
        FT_Done_FreeType(g_fonts.ft_library);
        return 0;
    }
    
    // Create Cairo font faces
    g_fonts.regular = cairo_ft_font_face_create_for_ft_face(g_fonts.ft_regular, 0);
    g_fonts.bold = cairo_ft_font_face_create_for_ft_face(g_fonts.ft_bold, 0);
    g_fonts.material = cairo_ft_font_face_create_for_ft_face(g_fonts.ft_material, 0);
    
    if (!g_fonts.regular || !g_fonts.bold || !g_fonts.material) {
        LOG_ERROR("❌ Failed to create Cairo font faces");
        cleanup_dashboard_fonts();
        return 0;
    }
    
    g_fonts.initialized = 1;
    LOG_DEBUG("✅ Fonts loaded successfully");
    return 1;
}

/**
 * Cleanup font resources
 */
void cleanup_dashboard_fonts(void) {
    if (g_fonts.initialized) {
        if (g_fonts.regular) cairo_font_face_destroy(g_fonts.regular);
        if (g_fonts.bold) cairo_font_face_destroy(g_fonts.bold);
        if (g_fonts.material) cairo_font_face_destroy(g_fonts.material);
        
        if (g_fonts.ft_material) FT_Done_Face(g_fonts.ft_material);
        if (g_fonts.ft_bold) FT_Done_Face(g_fonts.ft_bold);
        if (g_fonts.ft_regular) FT_Done_Face(g_fonts.ft_regular);
        if (g_fonts.ft_library) FT_Done_FreeType(g_fonts.ft_library);
        
        memset(&g_fonts, 0, sizeof(g_fonts));
    }
}

// ====================== UTILITY FUNCTIONS ======================

/**
 * Set regular or bold font on Cairo context
 */
static void set_font(cairo_t *cr, FontWeight weight, int size) {
    if (!cr || !g_fonts.initialized) return;
    
    cairo_font_face_t *face = (weight == FONT_BOLD) ? g_fonts.bold : g_fonts.regular;
    cairo_set_font_face(cr, face);
    cairo_set_font_size(cr, size);
    cairo_set_source_rgb(cr, 0, 0, 0); // Ensure black text
}

/**
 * Set Material Icons font on Cairo context
 */
static void set_material_font(cairo_t *cr, int size) {
    if (!cr || !g_fonts.initialized) return;
    
    cairo_set_font_face(cr, g_fonts.material);
    cairo_set_font_size(cr, size);
    cairo_set_source_rgb(cr, 0, 0, 0); // Ensure black text
}

/**
 * Check if a UTF-8 byte sequence represents a Material Icon
 */
static int is_material_icon_byte(unsigned char byte) {
    // UTF-8 detection for Material Icons range (U+E000-U+F8FF)
    return (byte >= 0xEE && byte <= 0xEF);
}

/**
 * Extract UTF-8 character length from starting byte
 */
static int get_utf8_char_length(const char *p) {
    const char *start = p;
    while (*p && (*p & 0x80)) p++;
    if (*p && !(*p & 0x80)) p++; // Include the final byte
    return p - start;
}

/**
 * Draw text with Material Icons support and alignment
 */
static void draw_text_with_icons(cairo_t *cr, double x, double y, const char *text, 
                                FontWeight weight, int font_size, TextAlignment align) {
    if (!cr || !text || !*text) return;
    
    // Clear any previous font state to prevent bold text accumulation
    cairo_save(cr);
    
    cairo_text_extents_t extents;
    double current_x = x;
    
    // Handle text alignment
    if (align != ALIGN_LEFT) {
        set_font(cr, weight, font_size);
        cairo_text_extents(cr, text, &extents);
        
        if (align == ALIGN_CENTER) {
            current_x = x - extents.width / 2.0;
        } else if (align == ALIGN_RIGHT) {
            current_x = x - extents.width;
        }
    }
    
    // Process text character by character for Material Icons
    const char *p = text;
    while (*p) {
        unsigned char byte = (unsigned char)*p;
        
        if (is_material_icon_byte(byte)) {
            // Handle Material Icon
            const char *char_start = p;
            int char_len = get_utf8_char_length(p);
            p += char_len;
            
            if (char_len > 0 && char_len < MAX_ICON_BUFFER) {
                char icon_char[MAX_ICON_BUFFER];
                strncpy(icon_char, char_start, char_len);
                icon_char[char_len] = '\0';
                
                // Draw with Material font (with vertical offset for alignment)
                set_material_font(cr, font_size);
                cairo_move_to(cr, current_x, y - 2);  // Slight adjustment for icon alignment
                cairo_show_text(cr, icon_char);
                
                cairo_text_extents(cr, icon_char, &extents);
                current_x += extents.x_advance;
            }
        } else {
            // Handle regular text
            const char *text_start = p;
            while (*p && !is_material_icon_byte((unsigned char)*p)) {
                p++;
            }
            
            if (p > text_start) {
                int len = p - text_start;
                if (len < MAX_TEXT_BUFFER) {
                    char regular_text[MAX_TEXT_BUFFER];
                    strncpy(regular_text, text_start, len);
                    regular_text[len] = '\0';
                    
                    // Draw with regular font
                    set_font(cr, weight, font_size);
                    cairo_move_to(cr, current_x, y);
                    cairo_show_text(cr, regular_text);
                    
                    cairo_text_extents(cr, regular_text, &extents);
                    current_x += extents.x_advance;
                }
            }
        }
    }
    
    // Restore Cairo context to clear font state
    cairo_restore(cr);
}

/**
 * Wrap text to fit within specified width
 * Returns: number of lines created
 */
static int wrap_text(const char *text, int max_width, FontWeight weight, int font_size,
                    char lines[][MAX_TEXT_BUFFER], int max_lines, cairo_t *cr) {
    if (!text || !*text || !cr) return 0;
    
    set_font(cr, weight, font_size);
    
    char *text_copy = strdup(text);
    if (!text_copy) return 0;
    
    char *saveptr;
    char *word = strtok_r(text_copy, " ", &saveptr);
    int line_count = 0;
    
    // Initialize first line
    if (max_lines > 0) {
        strcpy(lines[0], "");
    }
    
    while (word && line_count < max_lines) {
        char test_line[MAX_LINE_BUFFER];
        
        // Create test line with new word
        if (strlen(lines[line_count]) > 0) {
            snprintf(test_line, sizeof(test_line), "%s %s", lines[line_count], word);
        } else {
            strncpy(test_line, word, sizeof(test_line) - 1);
            test_line[sizeof(test_line) - 1] = '\0';
        }
        
        // Check if test line fits
        cairo_text_extents_t extents;
        cairo_text_extents(cr, test_line, &extents);
        
        if (extents.width <= max_width) {
            // Fits on current line
            strncpy(lines[line_count], test_line, MAX_TEXT_BUFFER - 1);
            lines[line_count][MAX_TEXT_BUFFER - 1] = '\0';
        } else {
            // Doesn't fit, handle overflow
            if (strlen(lines[line_count]) == 0) {
                // Single word too long, truncate it
                strncpy(lines[line_count], word, MAX_TEXT_BUFFER - 1);
                lines[line_count][MAX_TEXT_BUFFER - 1] = '\0';
            }
            
            // Move to next line
            line_count++;
            if (line_count < max_lines) {
                strncpy(lines[line_count], word, MAX_TEXT_BUFFER - 1);
                lines[line_count][MAX_TEXT_BUFFER - 1] = '\0';
            }
        }
        
        word = strtok_r(NULL, " ", &saveptr);
    }
    
    free(text_copy);
    return line_count + 1;
}

/**
 * Draw section border with title and separator line
 */
static void draw_section_border(cairo_t *cr, const char *title, int x, int y, int width, int height) {
    if (!cr) return;
    
    // Draw main border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_stroke(cr);
    
    // Draw title if provided
    if (title) {
        // Parse icon and text (separated by space)
        const char *space_pos = strchr(title, ' ');
        if (space_pos) {
            // Extract icon (before space)
            int icon_len = space_pos - title;
            if (icon_len < MAX_ICON_BUFFER) {
                char icon[MAX_ICON_BUFFER];
                strncpy(icon, title, icon_len);
                icon[icon_len] = '\0';
                
                // Extract text (after space)
                const char *text_part = space_pos + 1;
                
                // Draw icon with vertical offset for better alignment
                set_material_font(cr, FONT_SIZE_HEADER);
                cairo_move_to(cr, x + SECTION_MARGIN, y + SECTION_TITLE_Y_OFFSET + ICON_VERTICAL_OFFSET);
                cairo_show_text(cr, icon);
                
                // Calculate icon width for text positioning
                cairo_text_extents_t icon_extents;
                cairo_text_extents(cr, icon, &icon_extents);
                
                // Draw text at normal position
                set_font(cr, FONT_BOLD, FONT_SIZE_HEADER);
                cairo_move_to(cr, x + SECTION_MARGIN + icon_extents.x_advance + 5, y + SECTION_TITLE_Y_OFFSET);
                cairo_show_text(cr, text_part);
            }
        } else {
            // No icon, draw as regular text
            set_font(cr, FONT_BOLD, FONT_SIZE_HEADER);
            cairo_move_to(cr, x + SECTION_MARGIN, y + SECTION_TITLE_Y_OFFSET);
            cairo_show_text(cr, title);
        }
        
        // Draw separator line under title
        cairo_move_to(cr, x + SECTION_MARGIN, y + SECTION_TITLE_SEPARATOR_Y);
        cairo_line_to(cr, x + width - SECTION_MARGIN, y + SECTION_TITLE_SEPARATOR_Y);
        cairo_stroke(cr);
    }
}

/**
 * Draw menu item (lunch or dinner) with icon and wrapped text
 */
static void draw_menu_item(cairo_t *cr, int x, int y, int col_width, 
                          const char *icon, const char *label, const char *content) {
    if (!cr) return;
    
    // Draw icon with vertical offset
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, x + COLUMN_PADDING, y + ICON_VERTICAL_OFFSET);
    cairo_text_extents_t icon_extents;
    cairo_text_extents(cr, icon, &icon_extents);
    cairo_show_text(cr, icon);
    
    // Draw label
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, x + COLUMN_PADDING + icon_extents.x_advance + 5, y);
    cairo_show_text(cr, label);
    
    // Draw content (wrapped if necessary)
    if (content && strlen(content) > 0) {
        char lines[3][MAX_TEXT_BUFFER];
        int line_count = wrap_text(content, col_width - 20, FONT_REGULAR, FONT_SIZE_SMALL, lines, 3, cr);
        
        for (int i = 0; i < line_count && i < 3; i++) {
            draw_text_with_icons(cr, x + COLUMN_PADDING, y + 18 + i * MENU_ITEM_LINE_HEIGHT, lines[i],
                                FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        }
    } else {
        draw_text_with_icons(cr, x + COLUMN_PADDING, y + 18, "-",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
    }
}

/**
 * Format calendar event line based on event type
 */
static void format_event_line(const CalendarEvent *event, char *buffer, size_t buffer_size) {
    if (!event || !buffer) return;
    
    struct tm *start_tm = localtime(&event->start);
    if (!start_tm) return;
    
    switch (event->event_type) {
        case EVENT_TYPE_ALL_DAY:
            snprintf(buffer, buffer_size, "Toute la journée: %s", event->title);
            break;
        case EVENT_TYPE_START:
            snprintf(buffer, buffer_size, "%02d:%02d: %s", 
                    start_tm->tm_hour, start_tm->tm_min, event->title);
            break;
        case EVENT_TYPE_END:
            {
                struct tm *end_tm = localtime(&event->end);
                if (end_tm) {
                    snprintf(buffer, buffer_size, "Jusqu'à %02d:%02d: %s", 
                            end_tm->tm_hour, end_tm->tm_min, event->title);
                } else {
                    snprintf(buffer, buffer_size, "%s", event->title);
                }
            }
            break;
        default:
            snprintf(buffer, buffer_size, "%02d:%02d: %s", 
                    start_tm->tm_hour, start_tm->tm_min, event->title);
            break;
    }
}

// ====================== MAIN RENDERING FUNCTION ======================

/**
 * Core rendering function - renders dashboard to any Cairo surface
 */
void render_dashboard_to_surface(cairo_surface_t *surface, time_t display_date,
                                const WeatherData *weather_data,
                                const MenuData *menu_data,
                                const CalendarData *calendar_data) {
    if (!surface) return;
    
    cairo_t *cr = cairo_create(surface);
    if (!cr) return;
    
    // Clear background to white
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    
    // Set default drawing color to black
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);
    
    // Draw all dashboard sections
    draw_header_section(cr, display_date);
    draw_weather_section(cr, weather_data);
    draw_menu_section(cr, menu_data, display_date);
    draw_calendar_section(cr, calendar_data);
    
    cairo_destroy(cr);
}

// ====================== SECTION DRAWING FUNCTIONS ======================

/**
 * Draw header section with date and time
 */
void draw_header_section(cairo_t *cr, time_t display_date) {
    if (!cr) return;
    
    struct tm *tm_info = localtime(&display_date);
    if (!tm_info) return;
    
    // Draw border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, HEADER_X, HEADER_Y, HEADER_WIDTH, HEADER_HEIGHT);
    cairo_stroke(cr);
    
    // Format and draw date string
    char date_str[MAX_DATE_BUFFER];
    snprintf(date_str, sizeof(date_str), "%s %d %s %d",
             french_days[tm_info->tm_wday == 0 ? 6 : tm_info->tm_wday - 1],
             tm_info->tm_mday,
             french_months[tm_info->tm_mon],
             tm_info->tm_year + 1900);
    
    draw_text_with_icons(cr, HEADER_X + HEADER_WIDTH/2, HEADER_Y + 30, date_str, 
                        FONT_BOLD, FONT_SIZE_HEADER, ALIGN_CENTER);
    
    // Format and draw time string
    char time_str[MAX_TIME_BUFFER];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    
    draw_text_with_icons(cr, HEADER_X + HEADER_WIDTH/2, HEADER_Y + 65, time_str,
                        FONT_BOLD, FONT_SIZE_TIME, ALIGN_CENTER);
}

/**
 * Draw weather section with current conditions and forecasts
 */
void draw_weather_section(cairo_t *cr, const WeatherData *weather_data) {
    if (!cr) return;
    
    LOG_DEBUG("🌤️ Drawing weather section...");
    
    // Draw section border and title
    char title[MAX_TITLE_BUFFER];
    snprintf(title, sizeof(title), "%s Météo", ICON_WEATHER);
    draw_section_border(cr, title, WEATHER_X, WEATHER_Y, WEATHER_WIDTH, WEATHER_HEIGHT);
    
    // Add location info (right-aligned)
    const char *location_text = "Clamart, France";
    cairo_text_extents_t text_extents, icon_extents;
    
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_text_extents(cr, location_text, &text_extents);
    
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_text_extents(cr, ICON_LOCATION, &icon_extents);
    
    double text_x = WEATHER_X + WEATHER_WIDTH - 20 - text_extents.width;
    double icon_x = text_x - 5 - icon_extents.x_advance;
    
    // Draw location icon and text
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, icon_x, WEATHER_Y + 20 + ICON_VERTICAL_OFFSET);
    cairo_show_text(cr, ICON_LOCATION);
    
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, text_x, WEATHER_Y + 20);
    cairo_show_text(cr, location_text);
    
    if (!weather_data) {
        draw_text_with_icons(cr, WEATHER_X + 20, WEATHER_Y + 60, "Données météo non disponibles",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        return;
    }
    
    // Current weather section (left side)
    int content_y = WEATHER_Y + 75;
    
    // Format temperature
    char temp_str[32];
    snprintf(temp_str, sizeof(temp_str), "%.0f°C", weather_data->current.temperature);
    
    // Calculate positioning for centered weather icon and temperature
    cairo_text_extents_t temp_extents, weather_icon_extents;
    set_font(cr, FONT_BOLD, FONT_SIZE_LARGE_TEMP);
    cairo_text_extents(cr, temp_str, &temp_extents);
    
    set_material_font(cr, FONT_SIZE_WEATHER_ICON);
    cairo_text_extents(cr, weather_data->current.icon_unicode, &weather_icon_extents);
    
    double weather_total_width = weather_icon_extents.width + WEATHER_ICON_TEMP_SPACING + temp_extents.width;
    double start_x = WEATHER_X + SECTION_MARGIN + (WEATHER_LEFT_SECTION_WIDTH - weather_total_width) / 2;
    
    // Draw weather icon and temperature
    cairo_move_to(cr, start_x, content_y + 55);
    cairo_show_text(cr, weather_data->current.icon_unicode);
    
    set_font(cr, FONT_BOLD, FONT_SIZE_LARGE_TEMP);
    cairo_move_to(cr, start_x + weather_icon_extents.width + WEATHER_ICON_TEMP_SPACING, content_y + 45);
    cairo_show_text(cr, temp_str);
    
    // Draw weather description centered below
    draw_text_with_icons(cr, WEATHER_X + SECTION_MARGIN + WEATHER_LEFT_SECTION_WIDTH/2, content_y + 85,
                        weather_data->current.description, FONT_REGULAR, FONT_SIZE_MEDIUM, ALIGN_CENTER);
    
    // Forecasts section (right side)
    int forecast_x = WEATHER_X + 230;
    int forecast_y = WEATHER_Y + 50;
    
    // Forecast title and separators
    draw_text_with_icons(cr, forecast_x + (WEATHER_WIDTH - 230)/2, forecast_y + 10,
                        "Prévisions 12h:", FONT_REGULAR, FONT_SIZE_MEDIUM, ALIGN_CENTER);
    
    // Horizontal separator
    cairo_move_to(cr, forecast_x, forecast_y + 25);
    cairo_line_to(cr, WEATHER_X + WEATHER_WIDTH - SECTION_MARGIN, forecast_y + 25);
    cairo_stroke(cr);
    
    // Vertical separator for columns
    int col_separator_x = forecast_x + (WEATHER_WIDTH - 230) / 2;
    cairo_move_to(cr, col_separator_x, forecast_y + 30);
    cairo_line_to(cr, col_separator_x, forecast_y + 150);
    cairo_stroke(cr);
    
    // Draw forecast items in two columns
    int col1_x = forecast_x + 20;
    int col2_x = col_separator_x + 20;
    int item_y = forecast_y + 50;
    
    for (int i = 0; i < weather_data->forecast_count && i < 12; i++) {
        struct tm *tm_info = localtime(&weather_data->forecasts[i].datetime);
        if (!tm_info) continue;
        
        int x = (i < WEATHER_FORECAST_ITEMS_PER_COL) ? col1_x : col2_x;
        int y = item_y + (i % WEATHER_FORECAST_ITEMS_PER_COL) * WEATHER_FORECAST_LINE_HEIGHT;
        
        // Draw time part
        char time_part[8];
        snprintf(time_part, sizeof(time_part), "%02d:%02d ", tm_info->tm_hour, tm_info->tm_min);
        
        set_font(cr, FONT_REGULAR, FONT_SIZE_TINY);
        cairo_move_to(cr, x, y);
        cairo_text_extents_t time_extents;
        cairo_text_extents(cr, time_part, &time_extents);
        cairo_show_text(cr, time_part);
        
        // Draw weather icon
        set_material_font(cr, FONT_SIZE_TINY);
        cairo_move_to(cr, x + time_extents.x_advance, y + ICON_VERTICAL_OFFSET);
        cairo_text_extents_t icon_extents;
        cairo_text_extents(cr, weather_data->forecasts[i].icon_unicode, &icon_extents);
        cairo_show_text(cr, weather_data->forecasts[i].icon_unicode);
        
        // Draw temperature
        char temp_part[16];
        snprintf(temp_part, sizeof(temp_part), " %.0f°C", weather_data->forecasts[i].temperature);
        set_font(cr, FONT_REGULAR, FONT_SIZE_TINY);
        cairo_move_to(cr, x + time_extents.x_advance + icon_extents.x_advance, y);
        cairo_show_text(cr, temp_part);
    }
}

/**
 * Draw menu section with today/tomorrow columns
 */
void draw_menu_section(cairo_t *cr, const MenuData *menu_data, time_t display_date) {
    if (!cr) return;
    
    (void)display_date; // Not used in current implementation
    
    LOG_DEBUG("🍽️ Drawing menus section...");
    
    // Draw section border and title
    char title[MAX_TITLE_BUFFER];
    snprintf(title, sizeof(title), "%s Menus", ICON_MENU);
    draw_section_border(cr, title, MENU_X, MENU_Y, MENU_WIDTH, MENU_HEIGHT);
    
    if (!menu_data) {
        draw_text_with_icons(cr, MENU_X + 20, MENU_Y + 60, "Données menu non disponibles",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        return;
    }
    
    // Calculate column layout
    int available_width = MENU_WIDTH - 40;
    int col_width = (available_width - COLUMN_GAP) / 2;
    int today_x = MENU_X + 20;
    int tomorrow_x = today_x + col_width + COLUMN_GAP;
    int col_y = MENU_Y + 40;
    int col_height = MENU_HEIGHT - 45;
    
    // Today column
    cairo_rectangle(cr, today_x - COLUMN_PADDING, col_y - COLUMN_PADDING, 
                   col_width + 2*COLUMN_PADDING, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, today_x + col_width/2, col_y + 12, "Aujourd'hui",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, today_x, col_y + 20);
    cairo_line_to(cr, today_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Today's meals
    draw_menu_item(cr, today_x, col_y + 40, col_width, ICON_LUNCH, "Midi:", menu_data->today.midi);
    draw_menu_item(cr, today_x, col_y + 110, col_width, ICON_DINNER, "Soir:", menu_data->today.soir);
    
    // Tomorrow column
    cairo_rectangle(cr, tomorrow_x - COLUMN_PADDING, col_y - COLUMN_PADDING, 
                   col_width + 2*COLUMN_PADDING, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, tomorrow_x + col_width/2, col_y + 12, "Demain",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, tomorrow_x, col_y + 20);
    cairo_line_to(cr, tomorrow_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Tomorrow's meals
    draw_menu_item(cr, tomorrow_x, col_y + 40, col_width, ICON_LUNCH, "Midi:", menu_data->tomorrow.midi);
    draw_menu_item(cr, tomorrow_x, col_y + 110, col_width, ICON_DINNER, "Soir:", menu_data->tomorrow.soir);
}

/**
 * Draw calendar section with today/tomorrow events
 */
void draw_calendar_section(cairo_t *cr, const CalendarData *calendar_data) {
    if (!cr) return;
    
    LOG_DEBUG("📅 Drawing appointments section...");
    
    // Draw section border and title
    char title[MAX_TITLE_BUFFER];
    snprintf(title, sizeof(title), "%s Rendez-vous", ICON_CALENDAR);
    draw_section_border(cr, title, CALENDAR_X, CALENDAR_Y, CALENDAR_WIDTH, CALENDAR_HEIGHT);
    
    if (!calendar_data || (calendar_data->today.count == 0 && calendar_data->tomorrow.count == 0)) {
        draw_text_with_icons(cr, CALENDAR_X + 20, CALENDAR_Y + 60, "Données rendez-vous non disponibles",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        return;
    }
    
    // Calculate column layout
    int available_width = CALENDAR_WIDTH - 40;
    int col_width = (available_width - COLUMN_GAP) / 2;
    int today_x = CALENDAR_X + 20;
    int tomorrow_x = today_x + col_width + COLUMN_GAP;
    int col_y = CALENDAR_Y + 40;
    int col_height = 210;
    
    // Today column
    cairo_rectangle(cr, today_x - COLUMN_PADDING, col_y - COLUMN_PADDING, 
                   col_width + 2*COLUMN_PADDING, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, today_x + col_width/2, col_y + 12, "Aujourd'hui",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, today_x, col_y + 20);
    cairo_line_to(cr, today_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Today's events
    int event_y = col_y + 40;
    for (int i = 0; i < calendar_data->today.count && i < 8; i++) {
        const CalendarEvent *event = &calendar_data->today.events[i];
        
        char event_line[MAX_TEXT_BUFFER];
        format_event_line(event, event_line, sizeof(event_line));
        
        char lines[2][MAX_TEXT_BUFFER];
        int line_count = wrap_text(event_line, col_width - 10, FONT_REGULAR, FONT_SIZE_TINY, lines, 2, cr);
        
        for (int j = 0; j < line_count && j < 2; j++) {
            if (event_y > col_y + 200) break;
            
            draw_text_with_icons(cr, today_x + COLUMN_PADDING + (j > 0 ? 10 : 0), event_y, lines[j],
                                FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
            event_y += CALENDAR_ITEM_LINE_HEIGHT;
        }
        
        event_y += CALENDAR_EVENT_GAP;
    }
    
    if (calendar_data->today.count == 0) {
        draw_text_with_icons(cr, today_x + COLUMN_PADDING, col_y + 35, "Aucun événement",
                            FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
    }
    
    // Tomorrow column
    cairo_rectangle(cr, tomorrow_x - COLUMN_PADDING, col_y - COLUMN_PADDING, 
                   col_width + 2*COLUMN_PADDING, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, tomorrow_x + col_width/2, col_y + 12, "Demain",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, tomorrow_x, col_y + 20);
    cairo_line_to(cr, tomorrow_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Tomorrow's events
    event_y = col_y + 35;
    for (int i = 0; i < calendar_data->tomorrow.count && i < 8; i++) {
        const CalendarEvent *event = &calendar_data->tomorrow.events[i];
        
        char event_line[MAX_TEXT_BUFFER];
        format_event_line(event, event_line, sizeof(event_line));
        
        char lines[2][MAX_TEXT_BUFFER];
        int line_count = wrap_text(event_line, col_width - 10, FONT_REGULAR, FONT_SIZE_TINY, lines, 2, cr);
        
        for (int j = 0; j < line_count && j < 2; j++) {
            if (event_y > col_y + 200) break;
            
            draw_text_with_icons(cr, tomorrow_x + COLUMN_PADDING + (j > 0 ? 10 : 0), event_y, lines[j],
                                FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
            event_y += CALENDAR_ITEM_LINE_HEIGHT;
        }
        
        event_y += CALENDAR_EVENT_GAP;
    }
    
    if (calendar_data->tomorrow.count == 0) {
        draw_text_with_icons(cr, tomorrow_x + COLUMN_PADDING, col_y + 35, "Aucun événement",
                            FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
    }
}

/**
 * Render clock time to Cairo surface for partial updates
 */
int render_clock_to_surface(cairo_t *cr, time_t current_time, int width, int height) {
    if (!cr) return -1;
    
    // Initialize fonts
    if (!init_dashboard_fonts()) {
        LOG_ERROR("❌ Failed to initialize dashboard fonts for clock");
        return -1;
    }
    
    // Set white background
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    
    // Format time string
    struct tm *tm_info = localtime(&current_time);
    if (!tm_info) {
        cleanup_dashboard_fonts();
        return -1;
    }
    
    char time_str[MAX_TIME_BUFFER];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    
    // Get text extents using the same font setup as draw_text_with_icons
    set_font(cr, FONT_BOLD, FONT_SIZE_TIME);  // Use the same font setup function
    
    cairo_text_extents_t text_extents;
    cairo_text_extents(cr, time_str, &text_extents);
    
    // Center the text properly using text extents
    int center_x = (width + text_extents.x_bearing) / 2;
    int center_y = (height - text_extents.y_bearing) / 2;  // Adjust for baseline offset
    
    // Draw time text directly with consistent font (bypass draw_text_with_icons to avoid character processing issues)
    set_font(cr, FONT_BOLD, FONT_SIZE_TIME);
    cairo_move_to(cr, center_x, center_y);
    cairo_show_text(cr, time_str);
    
    // Cleanup fonts
    cleanup_dashboard_fonts();
    
    return 0;
}