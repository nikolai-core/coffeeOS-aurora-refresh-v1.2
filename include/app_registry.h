#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "app.h"

#define MAX_APPS 16

extern App *app_registry[MAX_APPS];
extern int app_count;

void app_registry_init(void);
int app_registry_register(App *app);
App *app_registry_get(int index);
App *app_registry_find_by_title(const char *title);
App *app_registry_find_by_id(const char *id);

#endif
