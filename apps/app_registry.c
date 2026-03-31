#include "app_registry.h"

App *app_registry[MAX_APPS];
int app_count;

extern App hello_app;
extern App calc_app;
extern App filemanager_app;
extern App notepad_app;
extern App paint_app;
extern App mixer_app;

static void app_registry_add(App *app) {
    if (app_count >= MAX_APPS) {
        return;
    }

    app_registry[app_count++] = app;
}

void app_registry_init(void) {
    int i;

    app_count = 0;
    for (i = 0; i < MAX_APPS; i++) {
        app_registry[i] = (App *)0;
    }

    app_registry_add(&hello_app);
    app_registry_add(&calc_app);
    app_registry_add(&filemanager_app);
    app_registry_add(&notepad_app);
    app_registry_add(&paint_app);
    app_registry_add(&mixer_app);
}
