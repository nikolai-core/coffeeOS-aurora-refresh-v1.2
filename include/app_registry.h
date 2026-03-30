#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "app.h"

#define MAX_APPS 16

extern App *app_registry[MAX_APPS];
extern int app_count;

void app_registry_init(void);

#endif