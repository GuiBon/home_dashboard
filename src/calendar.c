#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <regex.h>
#include <curl/curl.h>
#include "common.h"
#include "http.h"
#include "calendar.h"
#include "logging.h"

// Calendar client structure (implementation)
struct CalendarClient {
    char ical_url[MAX_URL_LENGTH];
    int debug;
};

// Check if event is all-day with null checking
static int is_all_day_event(const char* dtstart_line) {
    return dtstart_line && strstr(dtstart_line, "VALUE=DATE") != NULL;
}

// Convert time_t to YYYYMMDD integer format for date comparison
static int time_to_date_int(time_t timestamp) {
    if (timestamp == 0) {
        return 0;
    }
    struct tm *tm = localtime(&timestamp);
    if (!tm) {
        return 0;
    }
    return (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
}

// Create start of day timestamp with null checking
static time_t get_start_of_day(const struct tm *tm_day) {
    if (!tm_day) {
        return 0;
    }
    struct tm start_tm = *tm_day;
    start_tm.tm_hour = 0;
    start_tm.tm_min = 0;
    start_tm.tm_sec = 0;
    start_tm.tm_isdst = -1;
    return mktime(&start_tm);
}

// Create end of day timestamp with null checking
static time_t get_end_of_day(const struct tm *tm_day) {
    if (!tm_day) {
        return 0;
    }
    struct tm end_tm = *tm_day;
    end_tm.tm_hour = 23;
    end_tm.tm_min = 59;
    end_tm.tm_sec = 59;
    end_tm.tm_isdst = -1;
    return mktime(&end_tm);
}

// Remove whitespace from string in-place
static void remove_whitespace(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src != ' ' && *src != '\t' && *src != '\n' && *src != '\r') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

// Parse iCal datetime with optimized whitespace removal
static time_t parse_ical_datetime(const char* dt_string) {
    if (!dt_string || strlen(dt_string) == 0) {
        return 0;
    }
    
    struct tm tm = {0};
    char clean_dt[64];
    strncpy(clean_dt, dt_string, sizeof(clean_dt) - 1);
    clean_dt[sizeof(clean_dt) - 1] = '\0';
    
    // Remove any whitespace efficiently
    remove_whitespace(clean_dt);
    
    // Try different formats in order of likelihood
    if (strptime(clean_dt, "%Y%m%dT%H%M%SZ", &tm) ||
        strptime(clean_dt, "%Y%m%dT%H%M%S", &tm) ||
        strptime(clean_dt, "%Y%m%d", &tm)) {
        
        tm.tm_isdst = -1;  // Let mktime determine DST
        return mktime(&tm);
    }
    
    return 0;
}

// Extract datetime from DTSTART or DTEND line with bounds checking
static time_t extract_datetime_from_line(const char* field_name, const char* event_block) {
    if (!field_name || !event_block) {
        return 0;
    }
    
    char *field_line = strstr(event_block, field_name);
    if (!field_line) {
        return 0;
    }
    
    // Find the end of the field line
    char *line_end = strchr(field_line, '\n');
    if (!line_end) {
        return 0;
    }
    
    // Find the last colon within this line only
    char *colon = NULL;
    for (char *p = field_line; p < line_end; p++) {
        if (*p == ':') {
            colon = p;
        }
    }
    
    if (!colon) {
        return 0;
    }
    
    int len = line_end - colon - 1;
    if (len <= 0 || len >= MAX_BUFFER_SIZE - 1) {
        return 0;
    }
    
    char buffer[MAX_BUFFER_SIZE];
    strncpy(buffer, colon + 1, len);
    buffer[len] = '\0';
    
    return parse_ical_datetime(buffer);
}

// Clean iCal escape sequences in-place
static void clean_ical_escapes(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '\\' && *(src + 1) == 'n') {
            *dst++ = ' ';
            src += 2;
        } else if (*src == '\\' && *(src + 1) == ',') {
            *dst++ = ',';
            src += 2;
        } else if (*src == '\r') {
            // Skip carriage return
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Extract field from iCal event block with input validation
static int extract_ical_field(const char* event_block, const char* field, char* result, size_t result_size) {
    if (!event_block || !field || !result || result_size == 0) {
        return -1;
    }
    
    char search_pattern[64];
    int pattern_len = snprintf(search_pattern, sizeof(search_pattern), "%s:", field);
    if (pattern_len >= (int)sizeof(search_pattern)) {
        return -1;
    }
    
    char *field_start = strstr(event_block, search_pattern);
    if (!field_start) {
        return -1;
    }
    
    // Move past the field name and colon
    field_start += pattern_len;
    
    // Find the end of the line
    char *field_end = strchr(field_start, '\n');
    if (!field_end) {
        field_end = field_start + strlen(field_start);
    }
    
    // Calculate length with bounds checking
    int len = field_end - field_start;
    if (len <= 0 || (size_t)len >= result_size - 1) {
        return -1;
    }
    
    // Copy the field value
    strncpy(result, field_start, len);
    result[len] = '\0';
    
    // Clean up escape sequences
    clean_ical_escapes(result);
    
    return 0;
}

// Sort events by start time using insertion sort (efficient for small arrays)
static void sort_events_by_start_time(CalendarEvent *events, int count) {
    for (int i = 1; i < count; i++) {
        CalendarEvent key = events[i];
        int j = i - 1;
        while (j >= 0 && events[j].start > key.start) {
            events[j + 1] = events[j];
            j--;
        }
        events[j + 1] = key;
    }
}

// Validate calendar data structure
static int validate_calendar_data(CalendarData *data) {
    if (!data || !data->today.events || !data->tomorrow.events) {
        return -1;
    }
    return 0;
}

// Handle multi-day event creation for today and tomorrow
static void create_multiday_events(CalendarData *data, int max_events_per_day,
                                   const CalendarEvent *base_event, int start_date, int end_date,
                                   int today_date, int tomorrow_date) {
    if (!data || !base_event) {
        return;
    }
    
    time_t current_day = base_event->start;
    const time_t day_seconds = 24 * 60 * 60;
    
    while (current_day < base_event->end + day_seconds) {
        int current_date = time_to_date_int(current_day);
        
        // Break if we've passed the end date
        if (current_date > end_date) break;
        
        // Only create event if it's for today or tomorrow
        if (current_date == today_date || current_date == tomorrow_date) {
            struct tm *current_tm = localtime(&current_day);
            if (!current_tm) break;
            
            CalendarEvent daily_event = *base_event;
            
            // Determine event type and adjust times
            if (current_date == start_date) {
                daily_event.event_type = EVENT_TYPE_START;
                daily_event.start = base_event->start;
                daily_event.end = get_end_of_day(current_tm);
            } else if (current_date == end_date) {
                daily_event.event_type = EVENT_TYPE_END;
                daily_event.start = get_start_of_day(current_tm);
                daily_event.end = base_event->end;
            } else {
                daily_event.event_type = EVENT_TYPE_ALL_DAY;
                daily_event.start = get_start_of_day(current_tm);
                daily_event.end = get_end_of_day(current_tm);
            }
            
            // Add to appropriate day's events if there's space
            if (current_date == today_date && data->today.count < max_events_per_day) {
                data->today.events[data->today.count++] = daily_event;
            }
            else if (current_date == tomorrow_date && data->tomorrow.count < max_events_per_day) {
                data->tomorrow.events[data->tomorrow.count++] = daily_event;
            }
        }
        
        // Move to next day
        current_day += day_seconds;
    }
}

// Parse single iCal event
static CalendarEvent parse_ical_event(const char* event_block) {
    CalendarEvent event = {0};
    char buffer[MAX_BUFFER_SIZE];
    
    // Extract title (SUMMARY)
    if (extract_ical_field(event_block, "SUMMARY", buffer, sizeof(buffer)) == 0) {
        strncpy(event.title, buffer, sizeof(event.title) - 1);
        event.title[sizeof(event.title) - 1] = '\0';
    }

    // Extract start date (DTSTART) - handle timezone format properly
    event.start = extract_datetime_from_line("DTSTART", event_block);
    
    // Extract end date (DTEND) - handle timezone format properly
    event.end = extract_datetime_from_line("DTEND", event_block);
    if (event.end == 0 || event.end < event.start) {
        // If no DTEND or DTEND before DTSTART, use DTSTART as end
        event.end = event.start;
    }

    if (is_all_day_event(strstr(event_block, "DTSTART"))) {
        event.event_type = EVENT_TYPE_ALL_DAY;
        event.start = get_start_of_day(localtime(&event.start));
        event.end = get_end_of_day(localtime(&event.start));
    }

    return event;
}

// Process a single event block and add relevant events to the array
static void process_event_block(const CalendarEvent *event, CalendarData *data, 
                               int max_events_per_day, const struct tm *today_tm, 
                               const struct tm *tomorrow_tm) {
    // Input validation
    if (!event || !data || !today_tm || !tomorrow_tm) {
        return;
    }
    
    // Skip events without title or start time
    if (strlen(event->title) == 0 || event->start == 0) {
        return;
    }
    
    // Pre-calculate dates to avoid repeated mktime calls
    struct tm today_copy = *today_tm;
    struct tm tomorrow_copy = *tomorrow_tm;
    int today_date = time_to_date_int(mktime(&today_copy));
    int tomorrow_date = time_to_date_int(mktime(&tomorrow_copy));
    
    int start_date = time_to_date_int(event->start);
    int end_date = time_to_date_int(event->end);
    
    if (start_date != end_date) {
        // Multi-day event - create separate events for each day
        create_multiday_events(data, max_events_per_day, event, 
                              start_date, end_date, today_date, tomorrow_date);
    } else {
        // Single day event
        if (start_date == today_date && data->today.count < max_events_per_day) {
            CalendarEvent single_event = *event;
            single_event.event_type = EVENT_TYPE_NORMAL;
            data->today.events[data->today.count++] = single_event;
        }
        else if (start_date == tomorrow_date && data->tomorrow.count < max_events_per_day) {
            CalendarEvent single_event = *event;
            single_event.event_type = EVENT_TYPE_NORMAL;
            data->tomorrow.events[data->tomorrow.count++] = single_event;
        }
    }
}

// Internal function to get raw events from calendar for a specific date
static int get_raw_calendar_events(CalendarClient *client, CalendarData *data, char* ical_data, int max_events_per_day, time_t date) {
    // Input validation
    if (!client || !data || !ical_data) {
        return -1;
    }
    
    if (validate_calendar_data(data) != 0) {
        return -1;
    }
    
    // Get today's date
    struct tm *now_tm = localtime(&date);
    struct tm today_tm = *now_tm;
    
    // Calculate tomorrow's date properly
    struct tm tomorrow_tm = today_tm;
    tomorrow_tm.tm_mday += 1;
    tomorrow_tm.tm_isdst = -1;
    mktime(&tomorrow_tm); // Normalize the date (handles month/year boundaries)
    
    // Find event blocks
    char *event_start = strstr(ical_data, "BEGIN:VEVENT");
    
    while (event_start && (data->today.count < max_events_per_day || data->tomorrow.count < max_events_per_day)) {
        char *event_end = strstr(event_start, "END:VEVENT");
        if (!event_end) break;

        // Extract event block
        size_t block_size = event_end - event_start;
        char *event_block = malloc(block_size + 1);
        if (!event_block) {
            return -1; // Memory allocation failed
        }
    
        strncpy(event_block, event_start, block_size);
        event_block[block_size] = '\0';

        // Parse event
        CalendarEvent parsed_event_block = parse_ical_event(event_block);

        // Process the event block using helper function
        process_event_block(&parsed_event_block, data, max_events_per_day, &today_tm, &tomorrow_tm);
        
        // Find next event
        free(event_block);
        event_start = strstr(event_end, "BEGIN:VEVENT");
    }
    
    // Sort data by start time
    sort_events_by_start_time(data->today.events, data->today.count);
    sort_events_by_start_time(data->tomorrow.events, data->tomorrow.count);

    return 0;
}

// Get processed calendar events for a specific date
int get_calendar_events_data(CalendarClient *client, CalendarData *data, time_t date) {
    if (!client || !data) return -1;

    // Initialize the structure
    data->today.events = malloc(MAX_EVENTS_PER_DAY * sizeof(CalendarEvent));
    data->tomorrow.events = malloc(MAX_EVENTS_PER_DAY * sizeof(CalendarEvent));
    if (!data->today.events || !data->tomorrow.events) {
        // Clean up on allocation failure
        free(data->today.events);
        free(data->tomorrow.events);
        return -1;
    }
    
    data->today.count = 0;
    data->tomorrow.count = 0;

    char *ical_data = http_get(client->ical_url);
    if (!ical_data) {
        LOG_ERROR("❌ Error retrieving iCal");
        calendar_data_free(data);
        return -1;
    }
    
    int result = get_raw_calendar_events(client, data, ical_data, MAX_EVENTS_PER_DAY, date);
    free(ical_data);  // Free ical_data here after processing
    
    return result;
}

// Initialize calendar client with input validation
CalendarClient* calendar_client_init(const char* ical_url, int debug) {
    if (!ical_url || strlen(ical_url) == 0) {
        return NULL;
    }
    
    CalendarClient *client = malloc(sizeof(CalendarClient));
    if (!client) {
        return NULL;
    }
    
    strncpy(client->ical_url, ical_url, sizeof(client->ical_url) - 1);
    client->ical_url[sizeof(client->ical_url) - 1] = '\0';
    client->debug = debug;
    
    return client;
}

// Free calendar client
void calendar_client_free(CalendarClient *client) {
    if (client) {
        free(client);
    }
}

// Free calendar data
void calendar_data_free(CalendarData *data) {
    if (data) {
        if (data->today.events) {
            free(data->today.events);
            data->today.events = NULL;
        }
        if (data->tomorrow.events) {
            free(data->tomorrow.events);
            data->tomorrow.events = NULL;
        }
        data->today.count = 0;
        data->tomorrow.count = 0;
    }
}
