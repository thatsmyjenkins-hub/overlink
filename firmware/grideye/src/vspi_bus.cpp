#include <Arduino.h>
#include "vspi_bus.h"
#include "config.h"
#include "soc/gpio_sig_map.h"

SPIClass vspiBus(VSPI);

static int s_owner = -1;  // -1 none, 0 touch, 1 sd
static bool s_inited = false;

void vspiBusInit() {
    if (s_inited) return;
    // Primary begin on the touch pins (SCK/MISO/MOSI). CS is managed in software
    // by each device, so pass -1 for the hardware SS.
    vspiBus.begin(TOUCH_CLK_PIN, TOUCH_DOUT_PIN, TOUCH_DIN_PIN, -1);

    // Fan the clock + MOSI outputs out to the SD pins as well. Output signals can
    // drive multiple GPIOs; whichever device has its CS asserted responds.
    pinMode(SD_SCK_PIN, OUTPUT);
    pinMode(SD_MOSI_PIN, OUTPUT);
    pinMatrixOutAttach(SD_SCK_PIN, VSPICLK_OUT_IDX, false, false);
    pinMatrixOutAttach(SD_MOSI_PIN, VSPID_OUT_IDX, false, false);

    s_inited = true;
    vspiSelectTouch();
}

void vspiSelectTouch() {
    if (s_owner == 0) return;
    pinMode(TOUCH_DOUT_PIN, INPUT);
    pinMatrixInAttach(TOUCH_DOUT_PIN, VSPIQ_IN_IDX, false);
    s_owner = 0;
}

void vspiSelectSd() {
    if (s_owner == 1) return;
    pinMode(SD_MISO_PIN, INPUT);
    pinMatrixInAttach(SD_MISO_PIN, VSPIQ_IN_IDX, false);
    s_owner = 1;
}
