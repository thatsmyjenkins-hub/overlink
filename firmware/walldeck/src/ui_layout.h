#pragma once

// Screen geometry — set by platformio build flags
#ifdef WALLDECK_UI_V2
// ESP32-3248S035 3.5" 320×480 (CTRL 3.5)
constexpr int kScreenW = 320;
constexpr int kScreenH = 480;
constexpr int kMargin = 6;
#else
// ESP32-2432S028 2.8" 240×320
constexpr int kScreenW = 240;
constexpr int kScreenH = 320;
constexpr int kMargin = 4;
#endif
