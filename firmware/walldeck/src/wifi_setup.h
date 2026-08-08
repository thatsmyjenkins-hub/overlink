#pragma once

#include <lvgl.h>

typedef void (*WifiReadyCallback)();

void wifi_setup_start(WifiReadyCallback on_ready);
void wifi_setup_loop();
bool wifi_setup_is_active();
