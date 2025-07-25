#define _GNU_SOURCE
#include "display_eink.h"
#include "logging.h"
#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// French localization arrays implementation
const char* const french_days[7] = {
    "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"
};

const char* const french_months[12] = {
    "janvier", "février", "mars", "avril", "mai", "juin",
    "juillet", "août", "septembre", "octobre", "novembre", "décembre"
};

// Font structure for caching loaded fonts
typedef struct {
    cairo_font_face_t *regular;
    cairo_font_face_t *bold;
    cairo_font_face_t *material;
    FT_Library ft_library;
    FT_Face ft_regular;
    FT_Face ft_bold;
    FT_Face ft_material;
} FontManager;

static FontManager g_fonts = {0};

// Initialize font manager and load fonts
static int init_fonts(void) {
    if (g_fonts.regular) {
        return 1; // Already initialized
    }
    
    LOG_DEBUG("🔤 Loading fonts...");
    
    // Initialize FreeType
    if (FT_Init_FreeType(&g_fonts.ft_library)) {
        LOG_ERROR("❌ Failed to initialize FreeType");
        return 0;
    }
    
    // Load Liberation Sans Regular
    if (FT_New_Face(g_fonts.ft_library, FONT_LIBERATION_REGULAR, 0, &g_fonts.ft_regular)) {
        LOG_ERROR("❌ Failed to load Liberation Sans Regular");
        FT_Done_FreeType(g_fonts.ft_library);
        return 0;
    }
    
    // Load Liberation Sans Bold
    if (FT_New_Face(g_fonts.ft_library, FONT_LIBERATION_BOLD, 0, &g_fonts.ft_bold)) {
        LOG_ERROR("❌ Failed to load Liberation Sans Bold");
        FT_Done_Face(g_fonts.ft_regular);
        FT_Done_FreeType(g_fonts.ft_library);
        return 0;
    }
    
    // Load Material Symbols
    if (FT_New_Face(g_fonts.ft_library, FONT_MATERIAL_SYMBOLS, 0, &g_fonts.ft_material)) {
        LOG_ERROR("❌ Failed to load Material Symbols font");
        FT_Done_Face(g_fonts.ft_bold);
        FT_Done_Face(g_fonts.ft_regular);
        FT_Done_FreeType(g_fonts.ft_library);
        return 0;
    }
    
    // Create Cairo font faces
    g_fonts.regular = cairo_ft_font_face_create_for_ft_face(g_fonts.ft_regular, 0);
    g_fonts.bold = cairo_ft_font_face_create_for_ft_face(g_fonts.ft_bold, 0);
    g_fonts.material = cairo_ft_font_face_create_for_ft_face(g_fonts.ft_material, 0);
    
    LOG_DEBUG("✅ Fonts loaded successfully");
    
    return 1;
}

// Cleanup fonts
static void cleanup_fonts(void) {
    if (g_fonts.regular) {
        cairo_font_face_destroy(g_fonts.regular);
        cairo_font_face_destroy(g_fonts.bold);
        cairo_font_face_destroy(g_fonts.material);
        FT_Done_Face(g_fonts.ft_material);
        FT_Done_Face(g_fonts.ft_bold);
        FT_Done_Face(g_fonts.ft_regular);
        FT_Done_FreeType(g_fonts.ft_library);
        memset(&g_fonts, 0, sizeof(g_fonts));
    }
}

// Set font on Cairo context
static void set_font(cairo_t *cr, FontWeight weight, int size) {
    cairo_font_face_t *face = (weight == FONT_BOLD) ? g_fonts.bold : g_fonts.regular;
    cairo_set_font_face(cr, face);
    cairo_set_font_size(cr, size);
}

// Set material font on Cairo context
static void set_material_font(cairo_t *cr, int size) {
    cairo_set_font_face(cr, g_fonts.material);
    cairo_set_font_size(cr, size);
}

// Draw text with optional Material Icons support
static void draw_text_with_icons(cairo_t *cr, double x, double y, const char *text, 
                                FontWeight weight, int font_size, TextAlignment align) {
    if (!text || !*text) return;
    
    cairo_text_extents_t extents;
    double current_x = x;
    
    // Handle alignment
    if (align != ALIGN_LEFT) {
        // Calculate total text width for alignment
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
        // Check if character is a Material Icon (Unicode private use area)
        unsigned char byte = (unsigned char)*p;
        int is_material_icon = 0;
        
        // UTF-8 detection for Material Icons range (U+E000-U+F8FF)
        if (byte >= 0xEE && byte <= 0xEF) {
            is_material_icon = 1;
        }
        
        if (is_material_icon) {
            // Find end of UTF-8 character
            const char *char_start = p;
            while (*p && (*p & 0x80)) p++;
            if (*p && !(*p & 0x80)) p++; // Include the final byte
            
            // Draw with Material font (adjusted for better baseline alignment)
            set_material_font(cr, font_size);
            cairo_move_to(cr, current_x, y - 2);  // Slight adjustment for icon alignment
            
            char icon_char[16];
            int len = p - char_start;
            if (len < (int)sizeof(icon_char)) {
                strncpy(icon_char, char_start, len);
                icon_char[len] = '\0';
                cairo_show_text(cr, icon_char);
                
                cairo_text_extents(cr, icon_char, &extents);
                current_x += extents.x_advance;
            }
        } else {
            // Find end of regular text sequence
            const char *text_start = p;
            while (*p && !((unsigned char)*p >= 0xEE && (unsigned char)*p <= 0xEF)) {
                p++;
            }
            
            if (p > text_start) {
                // Draw with regular font
                set_font(cr, weight, font_size);
                cairo_move_to(cr, current_x, y);
                
                char regular_text[1024];
                int len = p - text_start;
                if (len < (int)sizeof(regular_text)) {
                    strncpy(regular_text, text_start, len);
                    regular_text[len] = '\0';
                    cairo_show_text(cr, regular_text);
                    
                    cairo_text_extents(cr, regular_text, &extents);
                    current_x += extents.x_advance;
                }
            }
        }
    }
}

// Wrap text to fit within given width
static int wrap_text(const char *text, int max_width, FontWeight weight, int font_size,
                    char lines[][1024], int max_lines, cairo_t *cr) {
    if (!text || !*text) return 0;
    
    set_font(cr, weight, font_size);
    
    char *text_copy = strdup(text);
    char *saveptr;
    char *word = strtok_r(text_copy, " ", &saveptr);
    int line_count = 0;
    
    strcpy(lines[0], "");
    
    while (word && line_count < max_lines) {
        char test_line[512];
        if (strlen(lines[line_count]) > 0) {
            snprintf(test_line, sizeof(test_line), "%s %s", lines[line_count], word);
        } else {
            strcpy(test_line, word);
        }
        
        cairo_text_extents_t extents;
        cairo_text_extents(cr, test_line, &extents);
        
        if (extents.width <= max_width) {
            strcpy(lines[line_count], test_line);
        } else {
            if (strlen(lines[line_count]) == 0) {
                // Single word too long, truncate it
                strncpy(lines[line_count], word, 255);
                lines[line_count][255] = '\0';
            }
            // Move to next line
            line_count++;
            if (line_count < max_lines) {
                strcpy(lines[line_count], word);
            }
        }
        
        word = strtok_r(NULL, " ", &saveptr);
    }
    
    free(text_copy);
    return line_count + 1;
}

// Draw section border and title
static void draw_section_border(cairo_t *cr, const char *title, int x, int y, int width, int height) {
    // Draw border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_stroke(cr);
    
    // Draw title (icon and text separately for better alignment)
    if (title) {
        // Find the space to separate icon from text
        const char *space_pos = strchr(title, ' ');
        if (space_pos) {
            // Extract icon (before space)
            int icon_len = space_pos - title;
            char icon[16];
            if (icon_len < (int)sizeof(icon)) {
                strncpy(icon, title, icon_len);
                icon[icon_len] = '\0';
                
                // Extract text (after space)
                const char *text_part = space_pos + 1;
                
                // Draw icon lower for better alignment
                set_material_font(cr, FONT_SIZE_HEADER);
                cairo_move_to(cr, x + 10, y + 22 + 3);  // +3 offset for icon
                cairo_show_text(cr, icon);
                
                // Calculate icon width for text positioning
                cairo_text_extents_t icon_extents;
                cairo_text_extents(cr, icon, &icon_extents);
                
                // Draw text at normal position
                set_font(cr, FONT_BOLD, FONT_SIZE_HEADER);
                cairo_move_to(cr, x + 10 + icon_extents.x_advance + 5, y + 22);
                cairo_show_text(cr, text_part);
            }
        } else {
            // No space found, draw as regular text
            set_font(cr, FONT_BOLD, FONT_SIZE_HEADER);
            cairo_move_to(cr, x + 10, y + 22);
            cairo_show_text(cr, title);
        }
        
        // Draw separator line under title
        cairo_move_to(cr, x + 10, y + 30);
        cairo_line_to(cr, x + width - 10, y + 30);
        cairo_stroke(cr);
    }
}

// Draw header section
static void draw_header_section(cairo_t *cr, time_t display_date) {
    struct tm *tm_info = localtime(&display_date);
    
    // Draw border
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, HEADER_X, HEADER_Y, HEADER_WIDTH, HEADER_HEIGHT);
    cairo_stroke(cr);
    
    // Format date string
    char date_str[128];
    snprintf(date_str, sizeof(date_str), "%s %d %s %d",
             french_days[tm_info->tm_wday == 0 ? 6 : tm_info->tm_wday - 1],
             tm_info->tm_mday,
             french_months[tm_info->tm_mon],
             tm_info->tm_year + 1900);
    
    // Draw date (centered)
    draw_text_with_icons(cr, HEADER_X + HEADER_WIDTH/2, HEADER_Y + 30, date_str, 
                        FONT_BOLD, FONT_SIZE_HEADER, ALIGN_CENTER);
    
    // Format time string
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    
    // Draw time (centered)
    draw_text_with_icons(cr, HEADER_X + HEADER_WIDTH/2, HEADER_Y + 65, time_str,
                        FONT_BOLD, FONT_SIZE_TIME, ALIGN_CENTER);
}

// Draw weather section
static void draw_weather_section(cairo_t *cr, const WeatherData *weather_data) {
    LOG_DEBUG("🌤️ Drawing weather section...");
    
    // Draw section border and title
    char title[64];
    snprintf(title, sizeof(title), "%s Météo", ICON_WEATHER);
    draw_section_border(cr, title, WEATHER_X, WEATHER_Y, WEATHER_WIDTH, WEATHER_HEIGHT);
    
    // Add location on the right side of title (icon and text separated)
    const char *location_text = "Clamart, France";
    
    // Calculate text width first for right alignment
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_text_extents_t text_extents;
    cairo_text_extents(cr, location_text, &text_extents);
    
    // Calculate icon width
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_text_extents_t icon_extents;
    cairo_text_extents(cr, ICON_LOCATION, &icon_extents);
    
    // Position from right edge
    double text_x = WEATHER_X + WEATHER_WIDTH - 20 - text_extents.width;
    double icon_x = text_x - 5 - icon_extents.x_advance;
    
    // Draw icon (offset +3)
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, icon_x, WEATHER_Y + 20 + 3);
    cairo_show_text(cr, ICON_LOCATION);
    
    // Draw text
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, text_x, WEATHER_Y + 20);
    cairo_show_text(cr, location_text);
    
    if (!weather_data) {
        LOG_ERROR("⚠️  No weather data available");
        draw_text_with_icons(cr, WEATHER_X + 20, WEATHER_Y + 60, "Données météo non disponibles",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        return;
    }
    
    // Current weather (left side, centered)
    int left_section_width = 220;
    int content_y = WEATHER_Y + 75;
    
    // Weather icon and temperature
    char temp_str[32];
    snprintf(temp_str, sizeof(temp_str), "%.0f°C", weather_data->current.temperature);
    
    // Center the icon and temperature
    cairo_text_extents_t temp_extents;
    set_font(cr, FONT_BOLD, FONT_SIZE_LARGE_TEMP);
    cairo_text_extents(cr, temp_str, &temp_extents);
    
    // Draw large weather icon
    set_material_font(cr, FONT_SIZE_WEATHER_ICON);
    cairo_text_extents_t weather_icon_extents;
    cairo_text_extents(cr, weather_data->current.icon_unicode, &weather_icon_extents);
    
    double weather_total_width = weather_icon_extents.width + 15 + temp_extents.width;
    double start_x = WEATHER_X + 10 + (left_section_width - weather_total_width) / 2;
    
    // Draw weather icon (better vertical alignment with temperature, +3 offset)
    cairo_move_to(cr, start_x, content_y + 55);
    cairo_show_text(cr, weather_data->current.icon_unicode);
    
    // Draw temperature (adjusted Y position for better alignment)
    set_font(cr, FONT_BOLD, FONT_SIZE_LARGE_TEMP);
    cairo_move_to(cr, start_x + weather_icon_extents.width + 15, content_y + 40 + 5);
    cairo_show_text(cr, temp_str);
    
    // Draw weather description centered below
    draw_text_with_icons(cr, WEATHER_X + 10 + left_section_width/2, content_y + 85,
                        weather_data->current.description, FONT_REGULAR, FONT_SIZE_MEDIUM, ALIGN_CENTER);
    
    // Forecasts section (right side)
    int forecast_x = WEATHER_X + 230;
    int forecast_y = WEATHER_Y + 50;
    
    // Title for forecasts (closer to separator)
    draw_text_with_icons(cr, forecast_x + (WEATHER_WIDTH - 230)/2, forecast_y + 10,
                        "Prévisions 12h:", FONT_REGULAR, FONT_SIZE_MEDIUM, ALIGN_CENTER);
    
    // Separator line (match Python positioning)
    cairo_move_to(cr, forecast_x, forecast_y + 25);
    cairo_line_to(cr, WEATHER_X + WEATHER_WIDTH - 10, forecast_y + 25);
    cairo_stroke(cr);
    
    // Vertical separator for columns
    int col_separator_x = forecast_x + (WEATHER_WIDTH - 230) / 2;
    cairo_move_to(cr, col_separator_x, forecast_y + 30);
    cairo_line_to(cr, col_separator_x, forecast_y + 150);
    cairo_stroke(cr);
    
    // Draw forecasts in two columns
    int col1_x = forecast_x + 20;
    int col2_x = col_separator_x + 20;
    int item_y = forecast_y + 50; // Lowered further for better spacing
    
    for (int i = 0; i < weather_data->forecast_count && i < 12; i++) {
        struct tm *tm_info = localtime(&weather_data->forecasts[i].datetime);
        
        // Create time and temperature text (without icon)
        char time_temp[64];
        snprintf(time_temp, sizeof(time_temp), "%02d:%02d %.0f°C",
                tm_info->tm_hour, tm_info->tm_min,
                weather_data->forecasts[i].temperature);
        
        int x = (i < 6) ? col1_x : col2_x;
        int y = item_y + (i % 6) * 18;
        
        // Draw time part
        set_font(cr, FONT_REGULAR, FONT_SIZE_TINY);
        cairo_move_to(cr, x, y);
        
        // Calculate position after time ("XX:XX ")
        char time_part[8];
        snprintf(time_part, sizeof(time_part), "%02d:%02d ", tm_info->tm_hour, tm_info->tm_min);
        cairo_text_extents_t time_extents;
        cairo_text_extents(cr, time_part, &time_extents);
        cairo_show_text(cr, time_part);
        
        // Draw icon (with +3 offset)
        set_material_font(cr, FONT_SIZE_TINY);
        cairo_move_to(cr, x + time_extents.x_advance, y + 3);
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

// Draw menus section
static void draw_menus_section(cairo_t *cr, const MenuData *menu_data) {
    LOG_DEBUG("🍽️ Drawing menus section...");
    
    // Draw section border and title
    char title[64];
    snprintf(title, sizeof(title), "%s Menus", ICON_MENU);
    draw_section_border(cr, title, MENU_X, MENU_Y, MENU_WIDTH, MENU_HEIGHT);
    
    if (!menu_data) {
        LOG_ERROR("⚠️  No menu data available");
        draw_text_with_icons(cr, MENU_X + 20, MENU_Y + 60, "Données menu non disponibles",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        return;
    }
    
    // Two columns layout
    int col_gap = 20;
    int available_width = MENU_WIDTH - 40;
    int col_width = (available_width - col_gap) / 2;
    
    int today_x = MENU_X + 20;
    int tomorrow_x = today_x + col_width + col_gap;
    int col_y = MENU_Y + 40;
    int col_height = MENU_HEIGHT - 45;
    
    // Today column
    cairo_rectangle(cr, today_x - 5, col_y - 5, col_width + 10, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, today_x + col_width/2, col_y + 12, "Aujourd'hui",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, today_x, col_y + 20);
    cairo_line_to(cr, today_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Today lunch (separated icon and text)
    // Draw lunch icon with +3 offset
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, today_x + 5, col_y + 40 + 3);
    cairo_text_extents_t lunch_icon_extents;
    cairo_text_extents(cr, ICON_LUNCH, &lunch_icon_extents);
    cairo_show_text(cr, ICON_LUNCH);
    
    // Draw "Midi:" text
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, today_x + 5 + lunch_icon_extents.x_advance + 5, col_y + 40);
    cairo_show_text(cr, "Midi:");
    
    if (strlen(menu_data->today.midi) > 0) {
        char lines[3][1024];
        int line_count = wrap_text(menu_data->today.midi, col_width - 20, FONT_REGULAR, FONT_SIZE_SMALL, lines, 3, cr);
        
        for (int i = 0; i < line_count && i < 3; i++) {
            draw_text_with_icons(cr, today_x + 5, col_y + 58 + i * 16, lines[i],
                                FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        }
    } else {
        draw_text_with_icons(cr, today_x + 5, col_y + 58, "-",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
    }
    
    // Today dinner (separated icon and text)
    // Draw dinner icon with +3 offset
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, today_x + 5, col_y + 110 + 3);
    cairo_text_extents_t dinner_icon_extents;
    cairo_text_extents(cr, ICON_DINNER, &dinner_icon_extents);
    cairo_show_text(cr, ICON_DINNER);
    
    // Draw "Soir:" text
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, today_x + 5 + dinner_icon_extents.x_advance + 5, col_y + 110);
    cairo_show_text(cr, "Soir:");
    
    if (strlen(menu_data->today.soir) > 0) {
        char lines[3][1024];
        int line_count = wrap_text(menu_data->today.soir, col_width - 20, FONT_REGULAR, FONT_SIZE_SMALL, lines, 3, cr);
        
        for (int i = 0; i < line_count && i < 3; i++) {
            draw_text_with_icons(cr, today_x + 5, col_y + 128 + i * 16, lines[i],
                                FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        }
    } else {
        draw_text_with_icons(cr, today_x + 5, col_y + 128, "-",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
    }
    
    // Tomorrow column
    cairo_rectangle(cr, tomorrow_x - 5, col_y - 5, col_width + 10, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, tomorrow_x + col_width/2, col_y + 12, "Demain",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, tomorrow_x, col_y + 20);
    cairo_line_to(cr, tomorrow_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Tomorrow lunch (separated icon and text)
    // Draw lunch icon with +3 offset
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, tomorrow_x + 5, col_y + 40 + 3);
    cairo_text_extents_t tomorrow_lunch_icon_extents;
    cairo_text_extents(cr, ICON_LUNCH, &tomorrow_lunch_icon_extents);
    cairo_show_text(cr, ICON_LUNCH);
    
    // Draw "Midi:" text
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, tomorrow_x + 5 + tomorrow_lunch_icon_extents.x_advance + 5, col_y + 40);
    cairo_show_text(cr, "Midi:");
    
    if (strlen(menu_data->tomorrow.midi) > 0) {
        char lines[3][1024];
        int line_count = wrap_text(menu_data->tomorrow.midi, col_width - 20, FONT_REGULAR, FONT_SIZE_SMALL, lines, 3, cr);
        
        for (int i = 0; i < line_count && i < 3; i++) {
            draw_text_with_icons(cr, tomorrow_x + 5, col_y + 58 + i * 16, lines[i],
                                FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        }
    } else {
        draw_text_with_icons(cr, tomorrow_x + 5, col_y + 58, "-",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
    }
    
    // Tomorrow dinner (separated icon and text)
    // Draw dinner icon with +3 offset
    set_material_font(cr, FONT_SIZE_SMALL);
    cairo_move_to(cr, tomorrow_x + 5, col_y + 110 + 3);
    cairo_text_extents_t tomorrow_dinner_icon_extents;
    cairo_text_extents(cr, ICON_DINNER, &tomorrow_dinner_icon_extents);
    cairo_show_text(cr, ICON_DINNER);
    
    // Draw "Soir:" text
    set_font(cr, FONT_REGULAR, FONT_SIZE_SMALL);
    cairo_move_to(cr, tomorrow_x + 5 + tomorrow_dinner_icon_extents.x_advance + 5, col_y + 110);
    cairo_show_text(cr, "Soir:");
    
    if (strlen(menu_data->tomorrow.soir) > 0) {
        char lines[3][1024];
        int line_count = wrap_text(menu_data->tomorrow.soir, col_width - 20, FONT_REGULAR, FONT_SIZE_SMALL, lines, 3, cr);
        
        for (int i = 0; i < line_count && i < 3; i++) {
            draw_text_with_icons(cr, tomorrow_x + 5, col_y + 128 + i * 16, lines[i],
                                FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        }
    } else {
        draw_text_with_icons(cr, tomorrow_x + 5, col_y + 128, "-",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
    }
}

// Format calendar event line
static void format_event_line(const CalendarEvent *event, char *buffer, size_t buffer_size) {
    struct tm *start_tm = localtime(&event->start);
    
    if (event->event_type == EVENT_TYPE_ALL_DAY) {
        snprintf(buffer, buffer_size, "Toute la journée: %s", event->title);
    } else if (event->event_type == EVENT_TYPE_START) {
        snprintf(buffer, buffer_size, "%02d:%02d: %s", 
                start_tm->tm_hour, start_tm->tm_min, event->title);
    } else if (event->event_type == EVENT_TYPE_END) {
        struct tm *end_tm = localtime(&event->end);
        snprintf(buffer, buffer_size, "Jusqu'à %02d:%02d: %s", 
                end_tm->tm_hour, end_tm->tm_min, event->title);
    } else {
        snprintf(buffer, buffer_size, "%02d:%02d: %s", 
                start_tm->tm_hour, start_tm->tm_min, event->title);
    }
}

// Draw appointments section  
static void draw_appointments_section(cairo_t *cr, const CalendarData *calendar_data) {
    LOG_DEBUG("📅 Drawing appointments section...");
    
    // Draw section border and title
    char title[64];
    snprintf(title, sizeof(title), "%s Rendez-vous", ICON_CALENDAR);
    draw_section_border(cr, title, CALENDAR_X, CALENDAR_Y, CALENDAR_WIDTH, CALENDAR_HEIGHT);
    
    if (!calendar_data || (calendar_data->today.count == 0 && calendar_data->tomorrow.count == 0)) {
        LOG_ERROR("⚠️  No calendar data available");
        draw_text_with_icons(cr, CALENDAR_X + 20, CALENDAR_Y + 60, "Données rendez-vous non disponibles",
                            FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_LEFT);
        return;
    }
    
    // Calendar data already filtered by today/tomorrow, no need for date calculations
    
    // Two columns layout
    int col_gap = 20;
    int available_width = CALENDAR_WIDTH - 40;
    int col_width = (available_width - col_gap) / 2;
    
    int today_x = CALENDAR_X + 20;
    int tomorrow_x = today_x + col_width + col_gap;
    int col_y = CALENDAR_Y + 40;
    int col_height = 210;
    
    // Today column
    cairo_rectangle(cr, today_x - 5, col_y - 5, col_width + 10, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, today_x + col_width/2, col_y + 12, "Aujourd'hui",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, today_x, col_y + 20);
    cairo_line_to(cr, today_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Display today's events (lowered further)
    int event_y = col_y + 40;
    
    for (int i = 0; i < calendar_data->today.count && i < 8; i++) {
        const CalendarEvent *event = &calendar_data->today.events[i];
        
        char event_line[1024];
        format_event_line(event, event_line, sizeof(event_line));
        
        char lines[2][1024];
        int line_count = wrap_text(event_line, col_width - 10, FONT_REGULAR, FONT_SIZE_TINY, lines, 2, cr);
        
        for (int j = 0; j < line_count && j < 2; j++) {
            if (event_y > col_y + 200) break;
            
            draw_text_with_icons(cr, today_x + 5 + (j > 0 ? 10 : 0), event_y, lines[j],
                                FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
            event_y += 18;
        }
        
        event_y += 3; // Small gap between events
    }
    
    if (calendar_data->today.count == 0) {
        draw_text_with_icons(cr, today_x + 5, col_y + 35, "Aucun événement",
                            FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
    }
    
    // Tomorrow column
    cairo_rectangle(cr, tomorrow_x - 5, col_y - 5, col_width + 10, col_height);
    cairo_stroke(cr);
    
    draw_text_with_icons(cr, tomorrow_x + col_width/2, col_y + 12, "Demain",
                        FONT_REGULAR, FONT_SIZE_SMALL, ALIGN_CENTER);
    
    cairo_move_to(cr, tomorrow_x, col_y + 20);
    cairo_line_to(cr, tomorrow_x + col_width, col_y + 20);
    cairo_stroke(cr);
    
    // Display tomorrow's events (lowered further)
    event_y = col_y + 35;
    
    for (int i = 0; i < calendar_data->tomorrow.count && i < 8; i++) {
        const CalendarEvent *event = &calendar_data->tomorrow.events[i];
        
        char event_line[1024];
        format_event_line(event, event_line, sizeof(event_line));
        
        char lines[2][1024];
        int line_count = wrap_text(event_line, col_width - 10, FONT_REGULAR, FONT_SIZE_TINY, lines, 2, cr);
        
        for (int j = 0; j < line_count && j < 2; j++) {
            if (event_y > col_y + 200) break;
            
            draw_text_with_icons(cr, tomorrow_x + 5 + (j > 0 ? 10 : 0), event_y, lines[j],
                                FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
            event_y += 18;
        }
        
        event_y += 3; // Small gap between events
    }
    
    if (calendar_data->tomorrow.count == 0) {
        draw_text_with_icons(cr, tomorrow_x + 5, col_y + 35, "Aucun événement",
                            FONT_REGULAR, FONT_SIZE_TINY, ALIGN_LEFT);
    }
}

// Main function to generate dashboard PNG
int generate_dashboard_png(const char *filename, time_t display_date, 
                          const WeatherData *weather_data, 
                          const MenuData *menu_data, 
                          const CalendarData *calendar_data) {
    
    LOG_DEBUG("🎨 Generating dashboard PNG: %s", filename);
    
    // Initialize fonts
    if (!init_fonts()) {
        LOG_ERROR("❌ Failed to initialize fonts");
        return 0;
    }
    
    // Create Cairo surface and context
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, EINK_WIDTH, EINK_HEIGHT);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        printf("❌ Failed to create Cairo surface\n");
        cleanup_fonts();
        return 0;
    }
    
    cairo_t *cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        printf("❌ Failed to create Cairo context\n");
        cairo_surface_destroy(surface);
        cleanup_fonts();
        return 0;
    }
    
    // Fill background with white
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    
    // Set default color to black
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);
    
    // Draw all sections
    draw_header_section(cr, display_date);
    draw_weather_section(cr, weather_data);
    draw_menus_section(cr, menu_data);
    draw_appointments_section(cr, calendar_data);
    
    // Save to PNG file
    cairo_status_t status = cairo_surface_write_to_png(surface, filename);
    
    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    cleanup_fonts();
    
    if (status != CAIRO_STATUS_SUCCESS) {
        printf("❌ Failed to save PNG file: %s\n", cairo_status_to_string(status));
        return 0;
    }
    
    LOG_INFO("✅ Dashboard PNG generated successfully: %s", filename);
    
    return 1;
}