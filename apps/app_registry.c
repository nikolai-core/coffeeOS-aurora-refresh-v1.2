#include "app_registry.h"
#include "ascii_util.h"

App *app_registry[MAX_APPS];
int app_count;

extern App hello_app;
extern App calc_app;
extern App filemanager_app;
extern App notepad_app;
extern App paint_app;
extern App mixer_app;
extern App netmon_app;

int app_registry_register(App *app) {
    if (app_count >= MAX_APPS) {
        return 0;
    }
    if (app == (App *)0 || app->title == (const char *)0) {
        return 0;
    }

    app_registry[app_count++] = app;
    return 1;
}

App *app_registry_get(int index) {
    if (index < 0 || index >= app_count) {
        return (App *)0;
    }
    return app_registry[index];
}

App *app_registry_find_by_title(const char *title) {
    int i;

    if (title == (const char *)0) {
        return (App *)0;
    }

    for (i = 0; i < app_count; i++) {
        App *app = app_registry[i];

        if (app != (App *)0 && app->title != (const char *)0 && ascii_streq(app->title, title)) {
            return app;
        }
    }
    return (App *)0;
}

App *app_registry_find_by_id(const char *id) {
    int i;

    if (id == (const char *)0) {
        return (App *)0;
    }

    for (i = 0; i < app_count; i++) {
        App *app = app_registry[i];

        if (app != (App *)0 && app->id != (const char *)0 && ascii_streq(app->id, id)) {
            return app;
        }
    }
    return (App *)0;
}

void app_registry_init(void) {
    int i;

    app_count = 0;
    for (i = 0; i < MAX_APPS; i++) {
        app_registry[i] = (App *)0;
    }

    (void)app_registry_register(&hello_app);
    (void)app_registry_register(&calc_app);
    (void)app_registry_register(&filemanager_app);
    (void)app_registry_register(&notepad_app);
    (void)app_registry_register(&paint_app);
    (void)app_registry_register(&mixer_app);
    (void)app_registry_register(&netmon_app);
}
