#pragma once
// Host-test stubs for Vita touch types used by app.h.
#include <stdint.h>

#define SCE_TOUCH_MAX_REPORT 8

typedef struct {
    int16_t minAaX, minAaY, maxAaX, maxAaY;
    int16_t minDispX, minDispY, maxDispX, maxDispY;
    uint8_t minForce, maxForce;
    uint8_t reserved[30];
} SceTouchPanelInfo;

typedef struct {
    uint8_t id;
    uint8_t force;
    int16_t x, y;
    uint8_t reserved[8];
    uint16_t info;
} SceTouchReport;

typedef struct {
    uint64_t timeStamp;
    uint32_t status;
    uint32_t reportNum;
    SceTouchReport report[SCE_TOUCH_MAX_REPORT];
} SceTouchData;
