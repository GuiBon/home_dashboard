#ifndef COMMON_H
#define COMMON_H

// ====================== CONFIGURATION ======================

// Project root path - dynamically set by Makefile using $(shell pwd)
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

#define WEATHER_LATITUDE 48.8566
#define WEATHER_LONGITUDE 2.3522
#define WEATHER_CITY "Clamart"
#define WEATHER_COUNTRY "France"

#endif // COMMON_H