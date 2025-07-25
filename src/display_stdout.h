#ifndef DISPLAY_STDOUT_H
#define DISPLAY_STDOUT_H

#include <time.h>
#include "weather.h"
#include "menu.h"
#include "calendar.h"

// Constants
#define MAX_FORECAST_DISPLAY 12
#define DAYS_IN_WEEK 7
#define MONTHS_IN_YEAR 12
#define MAX_MENU_EMPTY_TEXT 2

// Public display functions
void print_dashboard_header(time_t display_date);
void print_dashboard_weather(const WeatherData *weather_data);
void print_dashboard_menu(const MenuData *menu_data);
void print_dashboard_calendar(const CalendarData *calendar_data);

#endif // DISPLAY_STDOUT_H