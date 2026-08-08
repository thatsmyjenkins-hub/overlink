#pragma once

// Waveshare ESP32-S3-LCD-1.47

// ST7789 172x320
static const int PIN_LCD_MOSI = 45;
static const int PIN_LCD_SCLK = 40;
static const int PIN_LCD_CS = 42;
static const int PIN_LCD_DC = 41;
static const int PIN_LCD_RST = 39;
static const int PIN_LCD_BL = 48;

// WS2812 RGB
static const int PIN_RGB = 38;

// SD_MMC (1-bit or 4-bit)
static const int PIN_SD_CLK = 14;
static const int PIN_SD_CMD = 15;
static const int PIN_SD_D0 = 16;
static const int PIN_SD_D1 = 18;
static const int PIN_SD_D2 = 17;
static const int PIN_SD_D3 = 21;

static const int LCD_WIDTH = 172;
static const int LCD_HEIGHT = 320;
static const int LCD_COL_OFFSET = 34;
