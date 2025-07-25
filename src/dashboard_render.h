#ifndef DASHBOARD_RENDER_H
#define DASHBOARD_RENDER_H

#include <cairo.h>
#include <time.h>
#include "weather.h"
#include "menu.h"
#include "calendar.h"

// Display dimensions (7.5" Waveshare e-paper in portrait)
#define EINK_WIDTH 480
#define EINK_HEIGHT 800

// Section positions and sizes
#define HEADER_X 5
#define HEADER_Y 5
#define HEADER_WIDTH 470
#define HEADER_HEIGHT 80

#define WEATHER_X 5
#define WEATHER_Y 90
#define WEATHER_WIDTH 470
#define WEATHER_HEIGHT 220

#define MENU_X 5
#define MENU_Y 315
#define MENU_WIDTH 470
#define MENU_HEIGHT 220

#define CALENDAR_X 5
#define CALENDAR_Y 540
#define CALENDAR_WIDTH 470
#define CALENDAR_HEIGHT 255

// Font sizes
#define FONT_SIZE_TINY 12
#define FONT_SIZE_SMALL 14
#define FONT_SIZE_MEDIUM 18
#define FONT_SIZE_HEADER 20
#define FONT_SIZE_LARGE 24
#define FONT_SIZE_TIME 28
#define FONT_SIZE_LARGE_TEMP 48
#define FONT_SIZE_WEATHER_ICON 60

// Font paths
#define FONT_LIBERATION_REGULAR "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
#define FONT_LIBERATION_BOLD "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
#define FONT_MATERIAL_SYMBOLS PROJECT_ROOT "/config/fonts/MaterialSymbolsOutlined.ttf"

// Material Icons Unicode characters
#define ICON_WEATHER "\uf172"      // Weather section icon
#define ICON_MENU "\uf357"         // Menu section icon
#define ICON_CALENDAR "\uebcc"     // Calendar section icon
#define ICON_LOCATION "\ue0c8"     // Location icon
#define ICON_LUNCH "\ue56c"        // Lunch icon
#define ICON_DINNER "\uea57"       // Dinner icon

// Text alignment
typedef enum {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
} TextAlignment;

// Font weight
typedef enum {
    FONT_REGULAR,
    FONT_BOLD
} FontWeight;

// French localization arrays
extern const char* const french_days[7];
extern const char* const french_months[12];

// Font management
int init_dashboard_fonts(void);
void cleanup_dashboard_fonts(void);

// Core rendering functions
void render_dashboard_to_surface(cairo_surface_t *surface, time_t display_date,
                                const WeatherData *weather_data,
                                const MenuData *menu_data,
                                const CalendarData *calendar_data);

// Individual section renderers
void draw_header_section(cairo_t *cr, time_t display_date);
void draw_weather_section(cairo_t *cr, const WeatherData *weather_data);
void draw_menu_section(cairo_t *cr, const MenuData *menu_data, time_t display_date);
void draw_calendar_section(cairo_t *cr, const CalendarData *calendar_data);

#endif // DASHBOARD_RENDER_H