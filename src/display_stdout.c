#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common.h"
#include "display_stdout.h"
#include "logging.h"

// Static arrays for French localization
static const char* const french_days[DAYS_IN_WEEK] = {
    "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"
};

static const char* const french_months[MONTHS_IN_YEAR] = {
    "janvier", "février", "mars", "avril", "mai", "juin",
    "juillet", "août", "septembre", "octobre", "novembre", "décembre"
};

// ====================== DISPLAY FUNCTIONS ======================

void print_dashboard_header(time_t display_date) {
    if (display_date == 0) {
        display_date = time(NULL);
    }
    
    time_t now = time(NULL);
    struct tm *date_info = localtime(&display_date);
    struct tm *time_info = localtime(&now);
    
    if (!date_info || !time_info) {
        LOG_ERROR("\n⚠️  Date display error");
        return;
    }
    
    // Validate array bounds
    if (date_info->tm_wday < 0 || date_info->tm_wday >= DAYS_IN_WEEK ||
        date_info->tm_mon < 0 || date_info->tm_mon >= MONTHS_IN_YEAR) {
        LOG_ERROR("\n⚠️  Date format error");
        return;
    }
    
    printf("\n📅 %s %d %s %d, %02d:%02d\n",
           french_days[date_info->tm_wday],
           date_info->tm_mday,
           french_months[date_info->tm_mon],
           date_info->tm_year + 1900,
           time_info->tm_hour,
           time_info->tm_min);
    printf("📍 %s, %s\n", WEATHER_CITY, WEATHER_COUNTRY);
    printf("==================================================\n");
}

void print_dashboard_weather(const WeatherData *weather_data) {
    if (!weather_data) {
        LOG_ERROR("\n⚠️  Weather data not available");
        return;
    }
    
    printf("\n🌤️  MÉTÉO - %s\n", WEATHER_CITY);
    printf("════════════════════════════════\n");
    printf("🌡️  Température: %.0f°C\n", weather_data->current.temperature);
    printf("☀️  Conditions: %s %s\n", weather_data->current.icon, weather_data->current.description);
    
    if (weather_data->forecast_count > 0) {
        printf("\n📊 Prévisions 12h:\n");
        int max_forecasts = (weather_data->forecast_count < MAX_FORECAST_DISPLAY) ? 
                           weather_data->forecast_count : MAX_FORECAST_DISPLAY;
        
        for (int i = 0; i < max_forecasts; i++) {
            struct tm *tm_forecast = localtime(&weather_data->forecasts[i].datetime);
            if (tm_forecast) {
                printf("  %02d:%02d %s %.0f°C\n",
                       tm_forecast->tm_hour,
                       tm_forecast->tm_min,
                       weather_data->forecasts[i].icon,
                       weather_data->forecasts[i].temperature);
            }
        }
    }
    printf("\n");
}

// Helper function to display menu item or "-" if empty
static const char* get_menu_display(const char* menu_item) {
    if (!menu_item || strlen(menu_item) == 0) {
        return "-";
    }
    return menu_item;
}

void print_dashboard_menu(const MenuData *menu_data) {
    if (!menu_data) {
        printf("🍽️  MENUS\n");
        printf("════════════════════════════════\n");
        LOG_ERROR("⚠️  Menu data not available\n");
        return;
    }
    
    printf("🍽️  MENUS\n");
    printf("════════════════════════════════\n");
    printf("Aujourd'hui\n");
    printf("  🥗 Midi : %s\n", get_menu_display(menu_data->today.midi));
    printf("  🌙 Soir : %s\n", get_menu_display(menu_data->today.soir));
    printf("\nDemain\n");
    printf("  🥗 Midi : %s\n", get_menu_display(menu_data->tomorrow.midi));
    printf("  🌙 Soir : %s\n", get_menu_display(menu_data->tomorrow.soir));
    printf("\n");
}

// Helper function to print events for a specific day
static void print_day_events(const char* day_name, const DayEvents *day_events) {
    if (!day_name || !day_events) {
        LOG_ERROR("⚠️  Event display error");
        return;
    }
    
    printf("%s\n", day_name);
    
    if (day_events->count == 0) {
        printf("Aucun événement\n");
        return;
    }
    
    for (int i = 0; i < day_events->count; i++) {
        const CalendarEvent *event = &day_events->events[i];
        
        if (!event || event->start == 0) {
            continue;  // Skip invalid events
        }
        
        struct tm *start_tm = localtime(&event->start);
        struct tm *end_tm = localtime(&event->end);
        
        if (!start_tm || !end_tm) {
            continue;  // Skip if time conversion fails
        }
            
        switch (event->event_type) {
            case EVENT_TYPE_START:
                printf("%02d:%02d : %s\n", start_tm->tm_hour, start_tm->tm_min, event->title);
                break;
            case EVENT_TYPE_END:
                printf("Jusqu'à %02d:%02d : %s\n", end_tm->tm_hour, end_tm->tm_min, event->title);
                break;
            case EVENT_TYPE_ALL_DAY:
                printf("Toute la journée : %s\n", event->title);
                break;
            default:
                printf("%02d:%02d - %02d:%02d : %s\n", 
                        start_tm->tm_hour, start_tm->tm_min,
                        end_tm->tm_hour, end_tm->tm_min,
                        event->title);
                break;
        }
    }
}

void print_dashboard_calendar(const CalendarData *calendar_data) {
    printf("📅 CALENDRIER\n");
    printf("════════════════════════════════\n");
    
    if (!calendar_data) {
        LOG_ERROR("⚠️  Calendar data not available\n");
        return;
    }
    
    print_day_events("Aujourd'hui", &calendar_data->today);
    printf("\n");
    print_day_events("Demain", &calendar_data->tomorrow);
    printf("\n");
}