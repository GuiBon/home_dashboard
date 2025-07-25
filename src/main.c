#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "common.h"
#include "weather.h"
#include "menu.h"
#include "calendar.h"
#include "display_stdout.h"
#include "display_eink.h"
#include "display_partial.h"
#include "logging.h"

// Constants
#define WEATHER_UPDATE_INTERVAL_MIN 10
#define MENU_UPDATE_HOUR 0
#define MENU_UPDATE_MIN 0
#define MENU_UPDATE_SEC 0
#define CALENDAR_UPDATE_MIN 0
#define CALENDAR_UPDATE_SEC 0
#define MAIN_LOOP_SLEEP_MS 100000  // 100ms
#define THREAD_SLEEP_SEC 1
#define DEFAULT_NOON_HOUR 12
#define MIN_YEAR 1900
#define MAX_YEAR 2100
#define MONTHS_PER_YEAR 12
#define MAX_DAYS_PER_MONTH 31
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR 60

// ====================== DASHBOARD ORCHESTRATOR ======================

// Component status tracking
typedef struct {
    int weather_available;
    int menu_available;
    int calendar_available;
    char weather_error[256];
    char menu_error[256];
    char calendar_error[256];
    // Track what data changed for refresh optimization
    int weather_changed;
    int menu_changed;
    int calendar_changed;
    time_t last_change_time;  // Track when last change occurred
} ComponentStatus;

// Main orchestrator structure
typedef struct {
    WeatherClient *weather_client;
    MenuClient *menu_client;
    CalendarClient *calendar_client;
    
    WeatherData weather_data;
    MenuData menu_data;
    CalendarData calendar_data;
    ComponentStatus status;
    
    pthread_t clock_thread;
    pthread_t weather_thread;
    pthread_t menu_thread;
    pthread_t calendar_thread;
    pthread_mutex_t data_mutex;
    
    volatile int running;
    int debug;
    time_t date;
} DataOrchestrator;

static DataOrchestrator *g_orchestrator = NULL;

// Constants for batching
#define BATCH_DELAY_SECONDS 30  // Wait 30 seconds to batch updates (menu can be slow)

// Forward declaration
static void update_eink_display_batched(DataOrchestrator *orch, time_t date);

// Centralized function to update e-ink display based on batched changes
static void update_eink_display_batched(DataOrchestrator *orch, time_t date) {
    if (orch->debug) return;  // Only in production mode
    
    // Check if any data has changed
    int has_changes = orch->status.weather_changed || orch->status.menu_changed || orch->status.calendar_changed;
    if (!has_changes) {
        return;  // No changes, no need to refresh
    }
    
    // Determine refresh type: fast if only weather changed, full for menu/calendar changes
    int use_fast_refresh = orch->status.weather_changed && !orch->status.menu_changed && !orch->status.calendar_changed;
    
    // Build update type description
    char update_type[64] = "";
    int first = 1;
    if (orch->status.weather_changed) {
        strcat(update_type, "weather");
        first = 0;
    }
    if (orch->status.menu_changed) {
        if (!first) strcat(update_type, "+");
        strcat(update_type, "menu");
        first = 0;
    }
    if (orch->status.calendar_changed) {
        if (!first) strcat(update_type, "+");
        strcat(update_type, "calendar");
    }
    
    // Generate PNG for e-ink display
    const char *temp_png = "dashboard_temp.png";
    
    const WeatherData *weather_ptr = orch->status.weather_available ? &orch->weather_data : NULL;
    const MenuData *menu_ptr = orch->status.menu_available ? &orch->menu_data : NULL;
    const CalendarData *calendar_ptr = orch->status.calendar_available ? &orch->calendar_data : NULL;
    
    if (generate_dashboard_png(temp_png, date, weather_ptr, menu_ptr, calendar_ptr)) {
        // Display PNG directly using C library
        int result = display_png_on_eink(temp_png);
        
        if (result == 0) {
            const char *refresh_type = use_fast_refresh ? "fast refresh" : "full refresh";
            LOG_INFO("✅ E-ink display refreshed successfully (%s - %s)", refresh_type, update_type);
        } else {
            LOG_ERROR("❌ Failed to refresh e-ink display");
        }
    } else {
        LOG_ERROR("❌ Failed to generate PNG for %s display", update_type);
    }
    
    // Reset all change flags after successful update
    orch->status.weather_changed = 0;
    orch->status.menu_changed = 0;
    orch->status.calendar_changed = 0;
}

// Schedule a batched display update with delay to collect multiple changes
static void schedule_batched_display_update(DataOrchestrator *orch, time_t date __attribute__((unused))) {
    if (orch->debug) return;  // Only in production mode
    
    time_t now = time(NULL);
    orch->status.last_change_time = now;
    
    // The actual update will be handled by checking pending updates periodically
}

// Check if enough time has passed since last change to perform batched update
static void check_and_perform_batched_update(DataOrchestrator *orch, time_t date) {
    if (orch->debug) return;  // Only in production mode
    
    // Check if there are any pending changes
    int has_changes = orch->status.weather_changed || orch->status.menu_changed || orch->status.calendar_changed;
    if (!has_changes) {
        return;  // No changes pending
    }
    
    time_t now = time(NULL);
    // Check if enough time has passed since the last change
    if (now - orch->status.last_change_time >= BATCH_DELAY_SECONDS) {
        update_eink_display_batched(orch, date);
    }
}


// Signal handler for graceful shutdown
void signal_handler(int sig __attribute__((unused))) {
    if (g_orchestrator) {
        g_orchestrator->running = 0;
        LOG_INFO("\n🛑 Interrupt detected");
        
        // Clean shutdown - let the main loop handle cleanup
        LOG_DEBUG("🛑 Shutdown in progress...");
    }
}

// Initialize orchestrator with comprehensive error checking
DataOrchestrator* orchestrator_init(int debug) {
    DataOrchestrator *orch = malloc(sizeof(DataOrchestrator));
    if (!orch) {
        fprintf(stderr, "Error: Failed to allocate memory for orchestrator\n");
        return NULL;
    }
    
    memset(orch, 0, sizeof(DataOrchestrator));
    orch->debug = debug;
    orch->running = 0;
    
    // Initialize logging system
    if (init_logging(debug) != 0) {
        fprintf(stderr, "Error: Failed to initialize logging\n");
        free(orch);
        return NULL;
    }
    
    if (pthread_mutex_init(&orch->data_mutex, NULL) != 0) {
        fprintf(stderr, "Error: Failed to initialize mutex\n");
        free(orch);
        return NULL;
    }
    
    // Initialize clients with error checking
    orch->weather_client = weather_client_init("https://api.open-meteo.com", 
                                              WEATHER_LATITUDE, WEATHER_LONGITUDE, debug);
    if (!orch->weather_client) {
        LOG_ERROR("Warning: Failed to initialize weather client");
    }
    
    // Get spreadsheet ID from environment variable
    const char *spreadsheet_id = getenv("DASHBOARD_SPREADSHEET_ID");
    if (spreadsheet_id) {
        orch->menu_client = menu_client_init(PROJECT_ROOT "/config/credentials.json", 
                                            spreadsheet_id, debug);
        if (!orch->menu_client) {
            LOG_ERROR("Warning: Failed to initialize menu client");
        }
    } else {
        LOG_ERROR("Warning: DASHBOARD_SPREADSHEET_ID environment variable not set, menu client not initialized");
        orch->menu_client = NULL;
    }
    
    // Get iCal URL from environment variable
    const char *ical_url = getenv("DASHBOARD_ICAL_URL");
    if (ical_url) {
        orch->calendar_client = calendar_client_init(ical_url, debug);
        if (!orch->calendar_client) {
            LOG_ERROR("Warning: Failed to initialize calendar client");
        }
    } else {
        LOG_ERROR("Warning: DASHBOARD_ICAL_URL environment variable not set, calendar client not initialized");
        orch->calendar_client = NULL;
    }
    
    // Initialize data structures
    memset(&orch->calendar_data, 0, sizeof(CalendarData));
    memset(&orch->weather_data, 0, sizeof(WeatherData));
    memset(&orch->menu_data, 0, sizeof(MenuData));
    
    // Initialize component status
    memset(&orch->status, 0, sizeof(ComponentStatus));
    orch->status.weather_available = 0;
    orch->status.menu_available = 0;
    orch->status.calendar_available = 0;
    orch->status.last_change_time = 0;
    
    LOG_DEBUG("🚀 Orchestrator initialized");
    
    // Initialize partial display for time updates (only in non-debug mode)
    if (!debug) {
        if (init_partial_display() != 0) {
            LOG_ERROR("⚠️  Failed to initialize partial display, time updates will be skipped");
        }
    }
    
    return orch;
}

// Free orchestrator with proper cleanup
void orchestrator_free(DataOrchestrator *orch) {
    if (!orch) return;
    
    orch->running = 0;

    // Wait for all threads to complete
    if (orch->clock_thread) {
        pthread_join(orch->clock_thread, NULL);
    }
    if (orch->weather_thread) {
        pthread_join(orch->weather_thread, NULL);
    }
    if (orch->menu_thread) {
        pthread_join(orch->menu_thread, NULL);
    }
    if (orch->calendar_thread) {
        pthread_join(orch->calendar_thread, NULL);
    }

    // Clean up clients
    weather_client_free(orch->weather_client);
    menu_client_free(orch->menu_client);
    calendar_client_free(orch->calendar_client);
    
    // Clean up partial display
    cleanup_partial_display();
    
    // Clean up logging system
    close_logging();
    
    // Clean up data structures
    calendar_data_free(&orch->calendar_data);
    
    // Clean up synchronization
    pthread_mutex_destroy(&orch->data_mutex);
    
    free(orch);
}

// Update weather data with status tracking
void update_weather(DataOrchestrator *orch) {
    if (!orch) return;
    
    pthread_mutex_lock(&orch->data_mutex);
    
    if (orch->weather_client) {
        int result = get_weather_data(orch->weather_client, &orch->weather_data);
        if (result == 0) {
            orch->status.weather_available = 1;
            orch->status.weather_error[0] = '\0';
            orch->status.weather_changed = 1;  // Mark weather as changed
            LOG_INFO("✅ Weather data updated successfully");
        } else {
            orch->status.weather_available = 0;
            snprintf(orch->status.weather_error, sizeof(orch->status.weather_error), 
                    "Failed to retrieve weather data");
            LOG_ERROR("Warning: Failed to update weather data");
        }
    } else {
        orch->status.weather_available = 0;
        snprintf(orch->status.weather_error, sizeof(orch->status.weather_error), 
                "Weather client not initialized");
    }
    
    pthread_mutex_unlock(&orch->data_mutex);
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        LOG_DEBUG("🌤️  Weather updated: %02d:%02d:%02d (status: %s)", 
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
               orch->status.weather_available ? "OK" : "FAILED");
    }
    
    // Schedule batched display update if weather changed
    if (orch->status.weather_changed) {
        schedule_batched_display_update(orch, now);
    }
}

// Update menu data with status tracking
void update_menu(DataOrchestrator *orch, time_t date) {
    if (!orch) return;
    
    pthread_mutex_lock(&orch->data_mutex);
    
    if (orch->menu_client) {
        int result = get_menus_data(orch->menu_client, &orch->menu_data, date);
        if (result == 0) {
            orch->status.menu_available = 1;
            orch->status.menu_error[0] = '\0';
            orch->status.menu_changed = 1;  // Mark menu as changed
            LOG_INFO("✅ Menu data updated successfully");
        } else {
            orch->status.menu_available = 0;
            snprintf(orch->status.menu_error, sizeof(orch->status.menu_error), 
                    "Failed to retrieve menu data");
            LOG_ERROR("Warning: Failed to update menu data");
        }
    } else {
        orch->status.menu_available = 0;
        snprintf(orch->status.menu_error, sizeof(orch->status.menu_error), 
                "Menu client not initialized");
    }
    
    pthread_mutex_unlock(&orch->data_mutex);
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        LOG_DEBUG("📋 Menu updated: %02d:%02d:%02d (status: %s)", 
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
               orch->status.menu_available ? "OK" : "FAILED");
    }
    
    // Schedule batched display update if menu changed
    if (orch->status.menu_changed) {
        schedule_batched_display_update(orch, date);
    }
}

// Update calendar data with status tracking
void update_calendar(DataOrchestrator *orch, time_t date) {
    if (!orch) return;
    
    pthread_mutex_lock(&orch->data_mutex);
    
    if (orch->calendar_client) {
        // Free previous calendar data
        calendar_data_free(&orch->calendar_data);
        
        // Fetch and process calendar data
        // Fetch and process calendar data (restored)
        int result = get_calendar_events_data(orch->calendar_client, &orch->calendar_data, date);
        if (result == 0) {
            orch->status.calendar_available = 1;
            orch->status.calendar_error[0] = '\0';
            orch->status.calendar_changed = 1;  // Mark calendar as changed
            LOG_INFO("✅ Calendar data updated successfully");
        } else {
            orch->status.calendar_available = 0;
            snprintf(orch->status.calendar_error, sizeof(orch->status.calendar_error), 
                    "Failed to retrieve calendar data");
            LOG_ERROR("Warning: Failed to update calendar data");
        }
    } else {
        orch->status.calendar_available = 0;
        snprintf(orch->status.calendar_error, sizeof(orch->status.calendar_error), 
                "Calendar client not initialized");
    }
    
    pthread_mutex_unlock(&orch->data_mutex);
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        LOG_DEBUG("📅 Calendar updated: %02d:%02d:%02d (status: %s)", 
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
               orch->status.calendar_available ? "OK" : "FAILED");
    }
    
    // Schedule batched display update if calendar changed
    if (orch->status.calendar_changed) {
        schedule_batched_display_update(orch, date);
    }
}

// Clock update thread with input validation
void* clock_updater(void *arg) {
    DataOrchestrator *orch = (DataOrchestrator*)arg;
    
    if (!orch) {
        return NULL;
    }
    
    while (orch->running) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        
        if (!tm_now) {
            break;
        }
        
        int seconds_until_next_minute = SECONDS_PER_MINUTE - tm_now->tm_sec;
        
        // Break the sleep into smaller chunks to check running flag
        for (int i = 0; i < seconds_until_next_minute && orch->running; i++) {
            sleep(THREAD_SLEEP_SEC);
        }
        
        if (orch->running) {
            // Update time display using partial refresh (only in non-debug mode)
            if (!orch->debug && is_partial_display_available()) {
                if (refresh_time_partial() == 0) {
                    LOG_DEBUG("⏰ Time display updated via partial refresh: %02d:%02d", tm_now->tm_hour, tm_now->tm_min);
                } else {
                    LOG_ERROR("❌ Failed to update time display via partial refresh");
                }
            } else {
                LOG_DEBUG("⏰ Clock updated: %02d:%02d", tm_now->tm_hour, tm_now->tm_min);
            }
        }
    }
    
    return NULL;
}

// Weather update thread with improved scheduling
void* weather_updater(void *arg) {
    DataOrchestrator *orch = (DataOrchestrator*)arg;
    
    if (!orch) {
        return NULL;
    }
    
    while (orch->running) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        
        if (!tm_now) {
            break;
        }
        
        int current_minute = tm_now->tm_min;
        int next_minute = ((current_minute / WEATHER_UPDATE_INTERVAL_MIN) + 1) * WEATHER_UPDATE_INTERVAL_MIN;
        
        struct tm tm_next = *tm_now;
        if (next_minute >= MINUTES_PER_HOUR) {
            tm_next.tm_hour += 1;
            tm_next.tm_min = 0;
        } else {
            tm_next.tm_min = next_minute;
        }
        tm_next.tm_sec = 0;
        tm_next.tm_isdst = -1;
        
        time_t next_update = mktime(&tm_next);
        if (next_update == -1) {
            break;
        }
        
        int seconds_until_next = (int)(next_update - now);
        
        // Break the sleep into smaller chunks to check running flag
        for (int i = 0; i < seconds_until_next && orch->running; i++) {
            sleep(THREAD_SLEEP_SEC);
        }
        
        if (orch->running) {
            update_weather(orch);
        }
    }
    
    return NULL;
}

// Menu update thread with improved scheduling
void* menu_updater(void *arg) {
    DataOrchestrator *orch = (DataOrchestrator*)arg;
    
    if (!orch) {
        return NULL;
    }
    
    while (orch->running) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        
        if (!tm_now) {
            break;
        }
        
        struct tm tm_tomorrow = *tm_now;
        tm_tomorrow.tm_mday += 1;
        tm_tomorrow.tm_hour = MENU_UPDATE_HOUR;
        tm_tomorrow.tm_min = MENU_UPDATE_MIN;
        tm_tomorrow.tm_sec = MENU_UPDATE_SEC;
        tm_tomorrow.tm_isdst = -1;
        
        time_t tomorrow = mktime(&tm_tomorrow);
        if (tomorrow == -1) {
            break;
        }
        
        int seconds_until_tomorrow = (int)(tomorrow - now);
        
        // Break the sleep into smaller chunks to check running flag
        for (int i = 0; i < seconds_until_tomorrow && orch->running; i++) {
            sleep(THREAD_SLEEP_SEC);
        }
        
        if (orch->running) {
            update_menu(orch, orch->date);
        }
    }
    
    return NULL;
}

// Calendar update thread with improved scheduling
void* calendar_updater(void *arg) {
    DataOrchestrator *orch = (DataOrchestrator*)arg;
    
    if (!orch) {
        return NULL;
    }
    
    while (orch->running) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        
        if (!tm_now) {
            break;
        }
        
        struct tm tm_next = *tm_now;
        tm_next.tm_hour += 1;
        tm_next.tm_min = CALENDAR_UPDATE_MIN;
        tm_next.tm_sec = CALENDAR_UPDATE_SEC;
        tm_next.tm_isdst = -1;
        
        time_t next_update = mktime(&tm_next);
        if (next_update == -1) {
            break;
        }
        
        int seconds_until_next = (int)(next_update - now);
        
        // Break the sleep into smaller chunks to check running flag
        for (int i = 0; i < seconds_until_next && orch->running; i++) {
            sleep(THREAD_SLEEP_SEC);
        }
        
        if (orch->running) {
            update_calendar(orch, orch->date);
        }
    }
    
    return NULL;
}

// Start orchestrator with comprehensive error handling
int orchestrator_start(DataOrchestrator *orch, time_t date) {
    if (!orch) {
        return -1;
    }
    
    orch->running = 1;
    
    LOG_DEBUG("🚀 Orchestrator started");
    LOG_DEBUG("⏰ Clock: updates every minute");
    LOG_DEBUG("🌤️  Weather: updates every %d minutes at XX:X0:00", WEATHER_UPDATE_INTERVAL_MIN);
    LOG_DEBUG("📋 Menu: updates daily at %02d:%02d:%02d", MENU_UPDATE_HOUR, MENU_UPDATE_MIN, MENU_UPDATE_SEC);
    LOG_DEBUG("📅 Calendar: updates hourly at XX:%02d:%02d", CALENDAR_UPDATE_MIN, CALENDAR_UPDATE_SEC);
    LOG_DEBUG("=====================================");
    
    // Store date for threads
    orch->date = date;
    
    // Initial data update
    update_weather(orch);
    update_menu(orch, date);
    update_calendar(orch, date);
    
    // Perform batched display update after all initial updates
    update_eink_display_batched(orch, date);
    
    // Start threads with error checking
    if (pthread_create(&orch->clock_thread, NULL, clock_updater, orch) != 0) {
        fprintf(stderr, "Error: Failed to create clock thread\n");
        return -1;
    }
    if (pthread_create(&orch->weather_thread, NULL, weather_updater, orch) != 0) {
        fprintf(stderr, "Error: Failed to create weather thread\n");
        return -1;
    }
    if (pthread_create(&orch->menu_thread, NULL, menu_updater, orch) != 0) {
        fprintf(stderr, "Error: Failed to create menu thread\n");
        return -1;
    }
    if (pthread_create(&orch->calendar_thread, NULL, calendar_updater, orch) != 0) {
        fprintf(stderr, "Error: Failed to create calendar thread\n");
        return -1;
    }
    
    return 0;
}

// Display error message for failed component
static void print_component_error(const char* component_name, const char* error_message) {
    LOG_INFO("\n⚠️  %s", component_name);
    LOG_INFO("════════════════════════════════");
    LOG_INFO("❌ %s\n", error_message);
}

// Run orchestrator once with graceful degradation
int orchestrator_run_once(DataOrchestrator *orch, time_t date) {
    if (!orch) {
        return -1;
    }
    
    LOG_DEBUG("🚀 Orchestrator single mode");
    LOG_DEBUG("📋 Retrieving data...");
    LOG_DEBUG("=====================================");
    
    // Show header first
    print_dashboard_header(date);
    
    // Fetch and display weather (continue regardless of result)
    update_weather(orch);
    if (orch->status.weather_available) {
        print_dashboard_weather(&orch->weather_data);
    } else {
        print_component_error("WEATHER", orch->status.weather_error);
    }
    
    // Fetch and display menu (continue regardless of result)
    update_menu(orch, date);
    if (orch->status.menu_available) {
        print_dashboard_menu(&orch->menu_data);
    } else {
        print_component_error("MENU", orch->status.menu_error);
    }
    
    // Fetch and display calendar (continue regardless of result)
    update_calendar(orch, date);
    if (orch->status.calendar_available) {
        print_dashboard_calendar(&orch->calendar_data);
    } else {
        print_component_error("CALENDAR", orch->status.calendar_error);
    }
    
    // Show summary of component status
    LOG_INFO("📊 COMPONENT SUMMARY");
    LOG_INFO("════════════════════════════════");
    LOG_INFO("🌤️  Weather: %s", orch->status.weather_available ? "✅ OK" : "❌ FAILED");
    LOG_INFO("🍽️  Menu: %s", orch->status.menu_available ? "✅ OK" : "❌ FAILED");
    LOG_INFO("📅 Calendar: %s", orch->status.calendar_available ? "✅ OK" : "❌ FAILED");
    
    const WeatherData *weather_ptr = orch->status.weather_available ? &orch->weather_data : NULL;
    const MenuData *menu_ptr = orch->status.menu_available ? &orch->menu_data : NULL;
    const CalendarData *calendar_ptr = orch->status.calendar_available ? &orch->calendar_data : NULL;
    
    // Debug mode: generate PNG dashboard and save to file
    LOG_INFO("🖼️  Generating dashboard PNG...");
    
    const char *png_filename = "dashboard_debug.png";
    if (generate_dashboard_png(png_filename, date, weather_ptr, menu_ptr, calendar_ptr)) {
        LOG_INFO("✅ PNG generated: %s", png_filename);
    } else {
        LOG_ERROR("❌ Failed to generate PNG");
    }
    
    return 0;
}

// ====================== DATE PARSING ======================

// Parse date string with comprehensive validation
time_t parse_date_string(const char* date_str) {
    if (!date_str || strlen(date_str) == 0) {
        fprintf(stderr, "Error: No date string provided\n");
        return 0;
    }
    
    struct tm tm = {0};
    int day, month, year;
    
    // Parse DD/MM/YYYY format
    if (sscanf(date_str, "%d/%d/%d", &day, &month, &year) != 3) {
        fprintf(stderr, "Error: Invalid date format. Use DD/MM/YYYY\n");
        return 0;
    }
    
    // Validate date ranges with constants
    if (day < 1 || day > MAX_DAYS_PER_MONTH || 
        month < 1 || month > MONTHS_PER_YEAR || 
        year < MIN_YEAR || year > MAX_YEAR) {
        fprintf(stderr, "Error: Invalid date values. Day: 1-%d, Month: 1-%d, Year: %d-%d\n", 
                MAX_DAYS_PER_MONTH, MONTHS_PER_YEAR, MIN_YEAR, MAX_YEAR);
        return 0;
    }
    
    // Set up tm structure
    tm.tm_mday = day;
    tm.tm_mon = month - 1; // tm_mon is 0-based
    tm.tm_year = year - 1900; // tm_year is years since 1900
    tm.tm_hour = DEFAULT_NOON_HOUR; // Set to noon to avoid DST issues
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1; // Let mktime determine DST
    
    time_t result = mktime(&tm);
    if (result == -1) {
        fprintf(stderr, "Error: Invalid date\n");
        return 0;
    }
    
    return result;
}

// ====================== ENVIRONMENT LOADING ======================

// Load environment variables from .env file
static void load_env_file(void) {
    FILE *env_file = fopen(PROJECT_ROOT "/.env", "r");
    if (!env_file) {
        LOG_DEBUG("No .env file found, using system environment variables only");
        return;
    }
    
    char line[512];
    int loaded_count = 0;
    
    while (fgets(line, sizeof(line), env_file)) {
        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#') {
            continue;
        }
        
        // Remove trailing newline
        line[strcspn(line, "\n")] = '\0';
        
        // Find the = separator
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        
        // Split into key and value
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        
        // Skip if environment variable already exists (system env takes priority)
        if (getenv(key)) {
            LOG_DEBUG("Environment variable %s already set, skipping .env value", key);
            continue;
        }
        
        // Set environment variable
        if (setenv(key, value, 0) == 0) {
            LOG_DEBUG("Loaded environment variable: %s", key);
            loaded_count++;
        }
    }
    
    fclose(env_file);
    LOG_DEBUG("Loaded %d environment variables from .env file", loaded_count);
}

// ====================== MAIN FUNCTION ======================

int main(int argc, char *argv[]) {
    int debug = 0;
    char *date_str = NULL;
    
    // Load environment variables from .env file first
    load_env_file();
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        } else if (strcmp(argv[i], "--date") == 0) {
            if (i + 1 < argc) {
                date_str = argv[i + 1];
                i++; // Skip the next argument since it's the date value
            } else {
                fprintf(stderr, "Error: --date requires a date argument (format: DD/MM/YYYY)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --debug          Run once in debug mode and exit\n");
            printf("  --date DD/MM/YYYY Override today's date for menu and calendar\n");
            printf("  --help           Show this help message\n");
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            printf("Use --help for usage information\n");
            return 1;
        }
    }
    
    // Parse date if provided, otherwise use current time
    time_t date;
    if (date_str) {
        date = parse_date_string(date_str);
        if (date == 0) {
            return 1; // Error already printed by parse_date_string
        }
        
        struct tm *tm_test = localtime(&date);
        LOG_DEBUG("📅 Using test date: %02d/%02d/%d", 
               tm_test->tm_mday, tm_test->tm_mon + 1, tm_test->tm_year + 1900);
    } else {
        date = time(NULL);
    }
    
    DataOrchestrator *orch = orchestrator_init(debug);
    if (!orch) {
        fprintf(stderr, "❌ Failed to initialize orchestrator\n");
        return 1;
    }
    
    g_orchestrator = orch;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (debug) {
        // Debug mode - run once and exit
        if (orchestrator_run_once(orch, date) != 0) {
            fprintf(stderr, "❌ Failed to run orchestrator once\n");
            orchestrator_free(orch);
            return 1;
        }
        
        LOG_DEBUG("✅ Single execution completed");
    } else {
        // Production mode - start threads and run continuously
        if (orchestrator_start(orch, date) != 0) {
            fprintf(stderr, "❌ Failed to start orchestrator\n");
            orchestrator_free(orch);
            return 1;
        }
        
        while (orch->running) {
            usleep(MAIN_LOOP_SLEEP_MS); // 100ms for responsive signal handling
            
            // Check for pending batched display updates
            check_and_perform_batched_update(orch, date);
        }
        
        LOG_DEBUG("🛑 Orchestrator stopped");
    }
    
    orchestrator_free(orch);
    return 0;
}