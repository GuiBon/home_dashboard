#ifndef WEATHER_H
#define WEATHER_H

#include <time.h>

// Constants
#define MAX_API_URL_LENGTH 512
#define MAX_REQUEST_URL_LENGTH 1024
#define MAX_FORECAST_HOURS 12
#define DAY_START_HOUR 6
#define DAY_END_HOUR 20

// Weather data structures
typedef struct {
    double temperature;
    char description[256];
    char icon[64];
    char icon_unicode[64];
} WeatherCurrent;

typedef struct {
    time_t datetime;
    double temperature;
    char description[256];
    char icon[64];
    char icon_unicode[64];
} WeatherForecast;

typedef struct {
    WeatherCurrent current;
    WeatherForecast forecasts[MAX_FORECAST_HOURS];
    int forecast_count;
    time_t sunrise;
    time_t sunset;
} WeatherData;

// Weather client structure (opaque)
typedef struct WeatherClient WeatherClient;

// Public functions
WeatherClient* weather_client_init(const char* api_base_url, double latitude, double longitude, int debug);
void weather_client_free(WeatherClient *client);
int get_weather_data(WeatherClient *client, WeatherData *data);

// Weather data comparison
int weather_data_changed(const WeatherData *current, const WeatherData *previous);

#endif // WEATHER_H