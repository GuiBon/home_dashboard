#ifndef MENU_H
#define MENU_H

#include <time.h>

// Menu structure
typedef struct {
    char date[64];
    char midi[256];
    char soir[256];
} DayMenuData;

typedef struct {
    DayMenuData today;
    DayMenuData tomorrow;
} MenuData;

// Menu client structure (opaque)
typedef struct MenuClient MenuClient;

// Public functions
MenuClient* menu_client_init(const char* credentials_file, const char* spreadsheet_id, int debug);
void menu_client_free(MenuClient *client);
int get_menus_data(MenuClient *client, MenuData *data, time_t date);

#endif // MENU_H