#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "common.h"
#include "http.h"
#include "weather.h"
#include "logging.h"

// Weather client structure (implementation)
struct WeatherClient {
    char api_base_url[MAX_API_URL_LENGTH];
    double latitude;
    double longitude;
    int debug;
};

// Weather code to description mapping
static const char* get_weather_description(int code) {
    switch (code) {
        case 0: return "Ciel dégagé";
        case 1: return "Principalement dégagé";
        case 2: return "Partiellement nuageux";
        case 3: return "Couvert";
        case 45: return "Brouillard";
        case 48: return "Brouillard givrant";
        case 51: return "Bruine légère";
        case 53: return "Bruine modérée";
        case 55: return "Bruine forte";
        case 56: return "Bruine verglaçante légère";
        case 57: return "Bruine verglaçante forte";
        case 61: return "Pluie légère";
        case 63: return "Pluie modérée";
        case 65: return "Pluie forte";
        case 66: return "Pluie verglaçante légère";
        case 67: return "Pluie verglaçante forte";
        case 71: return "Neige légère";
        case 73: return "Neige modérée";
        case 75: return "Neige forte";
        case 77: return "Grains de neige";
        case 80: return "Averses légères";
        case 81: return "Averses modérées";
        case 82: return "Averses fortes";
        case 85: return "Averses de neige légères";
        case 86: return "Averses de neige fortes";
        case 95: return "Orages";
        case 96: return "Orages avec grêle légère";
        case 99: return "Orages avec grêle forte";
        default: return "Conditions inconnues";
    }
}

// Weather code to icon mapping
static const char* get_weather_icon(int code, int is_day) {
    switch (code) {
        case 0: return is_day ? "☀️" : "🌙";
        case 1: case 2: return is_day ? "🌤️" : "🌙";
        case 3: return "☁️";
        case 45: case 48: return "🌫️";
        case 51: case 53: case 55: case 56: case 57: return "🌦️";
        case 61: case 63: case 65: case 66: case 67: return "🌧️";
        case 71: case 73: case 75: case 77: return "🌨️";
        case 80: case 81: case 82: return "🌦️";
        case 85: case 86: return "🌨️";
        case 95: case 96: case 99: return "⛈️";
        default: return "🌤️";
    }
}

// Weather code to icon unicode mapping
static const char* get_weather_icon_unicode(int code, int is_day) {
    switch (code) {
        case 0: return is_day ? "\ue81a" : "\uef44";
        case 1: case 2: return is_day ? "\uf172" : "\uf174";
        case 3: return "\ue2bd";
        case 45: case 48: return "\ue818";
        case 51: case 53: case 55: case 56: case 57: return "\uf61a";
        case 61: case 63: case 65: case 66: case 67: return "\uf176";
        case 71: case 73: case 75: case 77: return "\ue819";
        case 80: case 81: case 82: return "\uf61f";
        case 85: case 86: return "\ue2cd";
        case 95: case 96: case 99: return "\uebdb";
        default: return is_day ? "\uf172" : "\uf174";
    }
}

// Check if it's daytime using sunrise/sunset times
static int is_day_time(time_t timestamp, time_t sunrise, time_t sunset) {
    if (timestamp == 0) {
        return 1;  // Default to day if timestamp is invalid
    }
    
    // If sunrise/sunset data is invalid, fall back to simple hour check
    if (sunrise == 0 || sunset == 0) {
        struct tm *timeinfo = localtime(&timestamp);
        if (!timeinfo) {
            return 1;
        }
        int hour = timeinfo->tm_hour;
        return (hour >= DAY_START_HOUR && hour < DAY_END_HOUR);  // Fallback to constants
    }
    
    return (timestamp >= sunrise && timestamp < sunset);
}

// Parse ISO 8601 datetime string with validation
static time_t parse_iso_datetime(const char* datetime_str) {
    if (!datetime_str || strlen(datetime_str) == 0) {
        return 0;
    }
    
    struct tm tm = {0};
    if (strptime(datetime_str, "%Y-%m-%dT%H:%M", &tm) != NULL) {
        tm.tm_isdst = -1;  // Let mktime determine DST
        return mktime(&tm);
    }
    return 0;
}

// Safe string copy with bounds checking
static void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Fetch weather JSON data from API with validation
static cJSON* fetch_weather_json(const WeatherClient *client) {
    if (!client) {
        return NULL;
    }
    
    char url[MAX_REQUEST_URL_LENGTH];
    int url_len = snprintf(url, sizeof(url), 
        "%s/v1/forecast?"
        "latitude=%.2f&longitude=%.2f&"
        "current=temperature_2m,weather_code,is_day&"
        "hourly=temperature_2m,weather_code&"
        "daily=sunrise,sunset&"
        "timezone=auto&forecast_hours=%d", 
        client->api_base_url, client->latitude, client->longitude, MAX_FORECAST_HOURS + 1);
    
    if (url_len >= (int)sizeof(url)) {
        return NULL;  // URL too long
    }
    
    char *response = http_get(url);
    if (!response) {
        return NULL;
    }
    
    cJSON *json = cJSON_Parse(response);
    free(response);
    return json;
}

// Process current weather data from JSON with validation
static int process_current_weather(const cJSON *json, WeatherCurrent *current) {
    if (!json || !current) {
        return -1;
    }
    
    const cJSON *current_json = cJSON_GetObjectItem(json, "current");
    if (!current_json) {
        return -1;
    }
    
    const cJSON *temp = cJSON_GetObjectItem(current_json, "temperature_2m");
    const cJSON *weather_code = cJSON_GetObjectItem(current_json, "weather_code");
    const cJSON *is_day = cJSON_GetObjectItem(current_json, "is_day");
    
    if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(weather_code) || !cJSON_IsNumber(is_day)) {
        return -1;
    }
    
    current->temperature = temp->valuedouble;
    int code = weather_code->valueint;
    int day = is_day->valueint;
    
    safe_strncpy(current->description, get_weather_description(code), sizeof(current->description));
    safe_strncpy(current->icon, get_weather_icon(code, day), sizeof(current->icon));
    safe_strncpy(current->icon_unicode, get_weather_icon_unicode(code, day), sizeof(current->icon_unicode));
    
    return 0;
}

// Process daily data to extract sunrise/sunset times
static int process_daily_data(const cJSON *json, time_t *sunrise, time_t *sunset) {
    if (!json || !sunrise || !sunset) {
        return -1;
    }
    
    *sunrise = 0;
    *sunset = 0;
    
    const cJSON *daily = cJSON_GetObjectItem(json, "daily");
    if (!daily) {
        return -1;
    }
    
    const cJSON *sunrise_array = cJSON_GetObjectItem(daily, "sunrise");
    const cJSON *sunset_array = cJSON_GetObjectItem(daily, "sunset");
    
    if (!cJSON_IsArray(sunrise_array) || !cJSON_IsArray(sunset_array)) {
        return -1;
    }
    
    // Get today's sunrise/sunset (first item in array)
    const cJSON *sunrise_item = cJSON_GetArrayItem(sunrise_array, 0);
    const cJSON *sunset_item = cJSON_GetArrayItem(sunset_array, 0);
    
    if (cJSON_IsString(sunrise_item) && cJSON_IsString(sunset_item)) {
        *sunrise = parse_iso_datetime(sunrise_item->valuestring);
        *sunset = parse_iso_datetime(sunset_item->valuestring);
        return 0;
    }
    
    return -1;
}

// Process hourly forecast data from JSON with validation
static int process_hourly_forecast(const cJSON *json, WeatherForecast *forecasts, int *forecast_count, time_t sunrise, time_t sunset) {
    if (!json || !forecasts || !forecast_count) {
        return -1;
    }
    
    *forecast_count = 0;
    
    const cJSON *hourly = cJSON_GetObjectItem(json, "hourly");
    if (!hourly) {
        return -1;
    }
    
    const cJSON *time_array = cJSON_GetObjectItem(hourly, "time");
    const cJSON *temp_array = cJSON_GetObjectItem(hourly, "temperature_2m");
    const cJSON *code_array = cJSON_GetObjectItem(hourly, "weather_code");
    
    if (!cJSON_IsArray(time_array) || !cJSON_IsArray(temp_array) || !cJSON_IsArray(code_array)) {
        return -1;
    }
    
    int count = cJSON_GetArraySize(time_array);
    // Skip first hour (same as current weather) and limit to MAX_FORECAST_HOURS
    if (count > MAX_FORECAST_HOURS + 1) {
        count = MAX_FORECAST_HOURS + 1;
    }
    
    for (int i = 1; i < count; i++) {  // Start from index 1 to skip first hour
        const cJSON *time_item = cJSON_GetArrayItem(time_array, i);
        const cJSON *temp_item = cJSON_GetArrayItem(temp_array, i);
        const cJSON *code_item = cJSON_GetArrayItem(code_array, i);
        
        if (cJSON_IsString(time_item) && cJSON_IsNumber(temp_item) && cJSON_IsNumber(code_item)) {
            int forecast_index = i - 1;  // Adjust index for forecast array
            forecasts[forecast_index].datetime = parse_iso_datetime(time_item->valuestring);
            forecasts[forecast_index].temperature = temp_item->valuedouble;
            
            int code = code_item->valueint;
            int day = is_day_time(forecasts[forecast_index].datetime, sunrise, sunset);
            
            safe_strncpy(forecasts[forecast_index].description, get_weather_description(code), sizeof(forecasts[forecast_index].description));
            safe_strncpy(forecasts[forecast_index].icon, get_weather_icon(code, day), sizeof(forecasts[forecast_index].icon));
            safe_strncpy(forecasts[forecast_index].icon_unicode, get_weather_icon_unicode(code, day), sizeof(forecasts[forecast_index].icon_unicode));
            
            (*forecast_count)++;
        }
    }
    
    return 0;
}

// Get weather data using client with comprehensive validation
int get_weather_data(WeatherClient *client, WeatherData *data) {
    if (!client || !data) {
        return -1;
    }
    
    // Initialize structure
    memset(data, 0, sizeof(WeatherData));
    
    LOG_DEBUG("🌤️  Fetching weather for %s (%.2f, %.2f)", 
               WEATHER_CITY, client->latitude, client->longitude);
    
    // Fetch JSON data from API
    cJSON *json = fetch_weather_json(client);
    if (!json) {
        LOG_ERROR("❌ Failed to fetch weather data");
        return -1;
    }
    
    // Process current weather
    if (process_current_weather(json, &data->current) != 0) {
        LOG_ERROR("❌ Failed to process current weather");
        cJSON_Delete(json);
        return -1;
    }
    
    // Process daily data for sunrise/sunset
    if (process_daily_data(json, &data->sunrise, &data->sunset) != 0) {
        LOG_ERROR("❌ Failed to process daily data");
        cJSON_Delete(json);
        return -1;
    }
    
    // Process hourly forecast
    if (process_hourly_forecast(json, data->forecasts, &data->forecast_count, data->sunrise, data->sunset) != 0) {
        LOG_ERROR("❌ Failed to process forecast data");
        cJSON_Delete(json);
        return -1;
    }
    
    cJSON_Delete(json);
    
    LOG_DEBUG("✅ Weather data retrieved successfully (current + %d forecasts)", data->forecast_count);
    
    return 0;
}

// Weather client initialization with validation
WeatherClient* weather_client_init(const char* api_base_url, double latitude, double longitude, int debug) {
    if (!api_base_url || strlen(api_base_url) == 0) {
        return NULL;
    }
    
    // Validate coordinates (basic range check)
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
        return NULL;
    }
    
    WeatherClient *client = malloc(sizeof(WeatherClient));
    if (!client) {
        return NULL;
    }
    
    safe_strncpy(client->api_base_url, api_base_url, sizeof(client->api_base_url));
    client->latitude = latitude;
    client->longitude = longitude;
    client->debug = debug;
    
    return client;
}

// Weather client cleanup
void weather_client_free(WeatherClient *client) {
    if (client) {
        free(client);
    }
}

// Weather data comparison function
int weather_data_changed(const WeatherData *current, const WeatherData *previous) {
    if (!current || !previous) {
        return 1; // Consider it changed if either is null
    }
    
    // Compare current weather
    if (fabs(current->current.temperature - previous->current.temperature) > 0.5) {
        return 1;
    }
    if (strcmp(current->current.description, previous->current.description) != 0) {
        return 1;
    }
    if (strcmp(current->current.icon, previous->current.icon) != 0) {
        return 1;
    }
    
    // Compare forecast count
    if (current->forecast_count != previous->forecast_count) {
        return 1;
    }
    
    // Compare forecasts
    for (int i = 0; i < current->forecast_count; i++) {
        if (fabs(current->forecasts[i].temperature - previous->forecasts[i].temperature) > 0.5) {
            return 1;
        }
        if (strcmp(current->forecasts[i].description, previous->forecasts[i].description) != 0) {
            return 1;
        }
        if (strcmp(current->forecasts[i].icon, previous->forecasts[i].icon) != 0) {
            return 1;
        }
        // Note: datetime differences are expected for forecasts, so we don't compare them
    }
    
    // Compare sunrise/sunset times (allow 10 minute tolerance for minor variations)
    if (abs((int)(current->sunrise - previous->sunrise)) > 600) {
        return 1;
    }
    if (abs((int)(current->sunset - previous->sunset)) > 600) {
        return 1;
    }
    
    return 0; // No significant changes detected
}
