#ifndef CALENDAR_H
#define CALENDAR_H

#include <time.h>

// Constants
#define MAX_EVENTS_PER_DAY 50
#define MAX_TITLE_LENGTH 256
#define MAX_URL_LENGTH 1024
#define MAX_BUFFER_SIZE 1024

// Event types
typedef enum {
    EVENT_TYPE_NORMAL,
    EVENT_TYPE_START,
    EVENT_TYPE_ALL_DAY,
    EVENT_TYPE_END
} EventType;

// Event structure
typedef struct {
    char title[512];
    time_t start;
    time_t end;
    EventType event_type;
} CalendarEvent;

// Calendar client structure (opaque)
typedef struct CalendarClient CalendarClient;

// Processed calendar data structures
typedef struct {
    CalendarEvent *events;
    int count;
} DayEvents;

typedef struct {
    DayEvents today;
    DayEvents tomorrow;
} CalendarData;

// Public functions
CalendarClient* calendar_client_init(const char* ical_url, int debug);
void calendar_client_free(CalendarClient *client);
void calendar_data_free(CalendarData *data);
int get_calendar_events_data(CalendarClient *client, CalendarData *data, time_t date);

#endif // CALENDAR_H