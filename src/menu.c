#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include "common.h"
#include "menu.h"
#include "logging.h"

// Menu client structure (implementation)
struct MenuClient {
    char credentials_file[512];
    char spreadsheet_id[256];
    char access_token[1024];
    int debug;
};

// Constants for menu fetching
#define MENU_SCRIPT_PATH PROJECT_ROOT "/scripts/menu_fetcher.py"
#define MENU_INITIAL_BUFFER_SIZE 1024
#define MENU_TIMEOUT_SECONDS 30

// Call dedicated Python script to get menu data
static char* call_python_menu(MenuClient *client, const char* test_date_str) {
    char command[1024];
    
    // Build command using dedicated script
    if (test_date_str) {
        snprintf(command, sizeof(command), 
                "%s --spreadsheet-id \"%s\" --credentials \"" PROJECT_ROOT "/config/credentials.json\" --date \"%s\"",
                MENU_SCRIPT_PATH, client->spreadsheet_id, test_date_str);
    } else {
        snprintf(command, sizeof(command), 
                "%s --spreadsheet-id \"%s\" --credentials \"" PROJECT_ROOT "/config/credentials.json\"",
                MENU_SCRIPT_PATH, client->spreadsheet_id);
    }
    
    // Add debug flag if enabled
    LOG_DEBUG("🐍 Calling Python menu script...");
    LOG_DEBUG("📋 Command: %s", command);
    
    // Set timeout alarm
    alarm(MENU_TIMEOUT_SECONDS);
    
    // Execute the command and capture output
    FILE *pipe = popen(command, "r");
    if (!pipe) {
        alarm(0);  // Cancel alarm
        LOG_ERROR("❌ Failed to execute Python script");
        return NULL;
    }
    
    // Dynamic buffer allocation starting with smaller size
    size_t capacity = MENU_INITIAL_BUFFER_SIZE;
    char *result = malloc(capacity);
    if (!result) {
        alarm(0);
        pclose(pipe);
        return NULL;
    }
    
    // Read output with dynamic buffer growth
    size_t total_read = 0;
    size_t bytes_read;
    while ((bytes_read = fread(result + total_read, 1, capacity - total_read - 1, pipe)) > 0) {
        total_read += bytes_read;
        
        // If buffer is getting full, expand it
        if (total_read >= capacity - 256) {
            capacity *= 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                alarm(0);
                pclose(pipe);
                return NULL;
            }
            result = new_result;
        }
    }
    result[total_read] = '\0';
    
    int exit_code = pclose(pipe);
    alarm(0);  // Cancel alarm
    
    if (exit_code != 0) {
        LOG_ERROR("❌ Python script failed with exit code: %d", exit_code);
        LOG_ERROR("   Output: %s", result);
        free(result);
        return NULL;
    }
    
    LOG_DEBUG("✅ Python menu data retrieved successfully (%zu bytes)", total_read);
    
    return result;
}

// Parse Python JSON response to extract menu data
static int parse_python_response(const char* json_response, MenuData *result) {
    cJSON *json = cJSON_Parse(json_response);
    if (!json) {
        return -1;
    }
    
    // Parse today's menu
    cJSON *today = cJSON_GetObjectItem(json, "today");
    if (today) {
        cJSON *date = cJSON_GetObjectItem(today, "date");
        cJSON *midi = cJSON_GetObjectItem(today, "midi");
        cJSON *soir = cJSON_GetObjectItem(today, "soir");
        
        if (date && cJSON_IsString(date)) {
            strncpy(result->today.date, date->valuestring, sizeof(result->today.date) - 1);
        }
        if (midi && cJSON_IsString(midi)) {
            strncpy(result->today.midi, midi->valuestring, sizeof(result->today.midi) - 1);
        }
        if (soir && cJSON_IsString(soir)) {
            strncpy(result->today.soir, soir->valuestring, sizeof(result->today.soir) - 1);
        }
    }
    
    // Parse tomorrow's menu
    cJSON *tomorrow = cJSON_GetObjectItem(json, "tomorrow");
    if (tomorrow) {
        cJSON *date = cJSON_GetObjectItem(tomorrow, "date");
        cJSON *midi = cJSON_GetObjectItem(tomorrow, "midi");
        cJSON *soir = cJSON_GetObjectItem(tomorrow, "soir");
        
        if (date && cJSON_IsString(date)) {
            strncpy(result->tomorrow.date, date->valuestring, sizeof(result->tomorrow.date) - 1);
        }
        if (midi && cJSON_IsString(midi)) {
            strncpy(result->tomorrow.midi, midi->valuestring, sizeof(result->tomorrow.midi) - 1);
        }
        if (soir && cJSON_IsString(soir)) {
            strncpy(result->tomorrow.soir, soir->valuestring, sizeof(result->tomorrow.soir) - 1);
        }
    }
    
    cJSON_Delete(json);
    return 0;
}

// Initialize menu client
MenuClient* menu_client_init(const char* credentials_file, const char* spreadsheet_id, int debug) {
    MenuClient *client = malloc(sizeof(MenuClient));
    if (!client) return NULL;
    
    strncpy(client->credentials_file, credentials_file, sizeof(client->credentials_file) - 1);
    strncpy(client->spreadsheet_id, spreadsheet_id, sizeof(client->spreadsheet_id) - 1);
    client->debug = debug;
    client->access_token[0] = '\0';
    
    return client;
}

// Free menu client
void menu_client_free(MenuClient *client) {
    if (client) {
        free(client);
    }
}

// Get menus for specific date
int get_menus_data(MenuClient *client, MenuData *data, time_t date) {
    // Initialize data with empty values
    strcpy(data->today.date, "");
    strcpy(data->today.midi, "-");
    strcpy(data->today.soir, "-");
    strcpy(data->tomorrow.date, "");
    strcpy(data->tomorrow.midi, "-");
    strcpy(data->tomorrow.soir, "-");
    
    // Format date as DD/MM/YYYY
    struct tm *tm_info = localtime(&date);
    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%02d/%02d/%04d", 
             tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900);
    
    // Call Python to get menu data
    char *python_response = call_python_menu(client, date_str);
    if (!python_response) {
        return -1;
    }
    
    // Parse the JSON response
    if (parse_python_response(python_response, data) != 0) {
        free(python_response);
        return -1;
    }
    
    free(python_response);
    return 0;
}
