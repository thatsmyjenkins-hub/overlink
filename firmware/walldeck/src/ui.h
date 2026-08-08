#pragma once

#include "core_client.h"

struct AppContext {
  CoreClient *core;
};

void ui_init(AppContext *ctx);
void ui_refresh(AppContext *ctx);
void ui_log(const char *msg);
