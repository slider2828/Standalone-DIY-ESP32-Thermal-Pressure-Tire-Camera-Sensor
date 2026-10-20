#include <Arduino.h>
#include <esp_display_panel.hpp>

#include <lvgl.h>
#include "lvgl_v8_port.h"

#include <WiFi.h>
#include <esp_now.h>

#include <NimBLEDevice.h>

#include "esp_sleep.h"
#include "driver/rtc_io.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;


// =====================================================
// CONFIGURATION
// =====================================================

// Hoosier tire temperature bands
#define TEMP_COLD_F          140.0f
#define TEMP_WARM_F          160.0f
#define TEMP_READY_F         180.0f
#define TEMP_OPTIMAL_HIGH_F  210.0f
#define TEMP_HOT_F           230.0f
#define TEMP_UPPER_F         240.0f

#define BAT_WARN_PERCENT 40
#define BAT_LOW_PERCENT  20

#define SIGNAL_TIMEOUT_MS 3000
#define SCREEN_UPDATE_MS 250

#define TOUCH_POLL_MS    15
#define TAP_MAX_MS       280
#define DOUBLE_TAP_MS    400
#define TAP_MAX_MOVE_PX  35

#define TOUCH_DEBUG 0


// =====================================================
// BOOT BUTTON / DEEP SLEEP
// =====================================================

#define BOOT_BUTTON_PIN 0
#define BOOT_HOLD_MS    1500


// =====================================================
// TOUCHSCREEN MONITOR BATTERY
//
// LiPo + -> 100k -> GPIO6 -> 100k -> GND
// =====================================================

#define MONITOR_BATTERY_PIN          6
#define MONITOR_BATTERY_UPDATE_MS    2000
#define MONITOR_BATTERY_CALIBRATION  1.000f
#define MONITOR_BATTERY_DEBUG        0


// =====================================================
// NIMBLE TPMS
// =====================================================

// Keep last valid reading visible.
// After 1 minute without a fresh TPMS packet,
// keep the reading but turn the line yellow.
#define TPMS_STALE_MS 60000UL

// Offset that best matches these TPMS sensors
#define TPMS_ATMOSPHERE_PSI 14.5f

#define TPMS_DEBUG 0


// =====================================================
// TPMS SENSOR MAC ADDRESSES
// =====================================================

const char *tpmsMac[4] =
{
    "54:88:00:00:35:39",   // FL
    "54:89:00:00:06:72",   // FR
    "54:84:00:00:43:41",   // RL
    "54:89:00:00:10:22"    // RR
};

NimBLEScan *tpmsScan =
    nullptr;


// =====================================================
// COLORS
// =====================================================

#define COLOR_BG            0x000000
#define COLOR_PANEL         0x080808
#define COLOR_PANEL_BORDER  0x808080

#define COLOR_TEXT          0xFFFFFF
#define COLOR_SUBTEXT       0xD8D8D8

#define COLOR_GOOD          0x00FF55
#define COLOR_WARN          0xFFD000
#define COLOR_BAD           0xFF2020

#define COLOR_IDLE          0x252525
#define COLOR_ACCENT        0xFFFFFF
#define COLOR_SELECT        0xFFFFFF

#define COLOR_MAX           0xB0B0B0


// =====================================================
// FOUR-TIRE PANEL GEOMETRY
// =====================================================

const int normalPanelX[4] =
{
    8,
    410,
    8,
    410
};

const int normalPanelY[4] =
{
    8,
    8,
    244,
    244
};

#define NORMAL_PANEL_W 382
#define NORMAL_PANEL_H 228

#define HEADER_TOUCH_H 55


// =====================================================
// DISPLAY MODE
// =====================================================

enum DisplayMode
{
    MODE_FOUR_TIRES,
    MODE_TWO_TIRES
};

DisplayMode displayMode =
    MODE_FOUR_TIRES;

int selectedTireA =
    -1;

int selectedTireB =
    -1;


// =====================================================
// BOARD / TOUCH
// =====================================================

Board *board =
    nullptr;

Touch *touchPanel =
    nullptr;


// =====================================================
// ESP-NOW PACKET
//
// MUST MATCH TRANSMITTER
// =====================================================

typedef struct __attribute__((packed))
{
    uint8_t tireID;
    uint8_t battery;

    int16_t zone[5];

    int16_t hottest;

} TirePacket;


// =====================================================
// THERMAL DATA
// =====================================================

struct TireData
{
    bool received;

    uint8_t battery;

    float zone[5];

    float zoneMax[5];

    unsigned long lastUpdate;
};

TireData tires[4] =
{};

portMUX_TYPE tireMux =
    portMUX_INITIALIZER_UNLOCKED;


// =====================================================
// TPMS DATA
// =====================================================

struct TPMSData
{
    bool received;

    float pressurePsi;
    float peakPressurePsi;

    int temperatureC;
    int peakTemperatureC;

    float batteryVoltage;

    int rssi;

    unsigned long lastUpdate;
};

TPMSData tpms[4] =
{};

portMUX_TYPE tpmsMux =
    portMUX_INITIALIZER_UNLOCKED;


// =====================================================
// TIRE NAMES
// =====================================================

const char *tireNames[4] =
{
    "FRONT LEFT",
    "FRONT RIGHT",
    "REAR LEFT",
    "REAR RIGHT"
};

const char *tireShortNames[4] =
{
    "FL",
    "FR",
    "RL",
    "RR"
};


// =====================================================
// LVGL OBJECTS
// =====================================================

lv_obj_t *tirePanel[4];

lv_obj_t *positionTag[4];

lv_obj_t *tireNameLabel[4];

lv_obj_t *batteryLabel[4];

lv_obj_t *statusLabel[4];

lv_obj_t *pressureLabel[4];

lv_obj_t *monitorBatteryLabel =
    nullptr;

float monitorBatteryVoltage =
    0.0f;

int monitorBatteryPercent =
    -1;

lv_obj_t *zoneBox[4][5];

lv_obj_t *zoneValueLabel[4][5];

lv_obj_t *zoneNumberLabel[4][5];

lv_obj_t *zoneMaxLabel[4][5];


// =====================================================
// DISPLAY CACHE
// =====================================================

enum CachedThermalState
{
    THERMAL_CACHE_UNKNOWN = -1,
    THERMAL_CACHE_WAITING = 0,
    THERMAL_CACHE_NO_SIGNAL = 1,
    THERMAL_CACHE_LIVE = 2
};

int cachedThermalState[4] =
{
    -1,
    -1,
    -1,
    -1
};

int cachedThermalBattery[4] =
{
    -999,
    -999,
    -999,
    -999
};

int cachedZoneTemp[4][5];

int cachedZoneMax[4][5];

int cachedZoneBand[4][5];


int cachedTpmsState[4] =
{
    -1,
    -1,
    -1,
    -1
};

int cachedTpmsPsiTenths[4] =
{
    -9999,
    -9999,
    -9999,
    -9999
};

int cachedTpmsPeakPsiTenths[4] =
{
    -9999,
    -9999,
    -9999,
    -9999
};

int cachedTpmsTempF[4] =
{
    -9999,
    -9999,
    -9999,
    -9999
};

int cachedTpmsPeakTempF[4] =
{
    -9999,
    -9999,
    -9999,
    -9999
};

int cachedTpmsBatteryPercent[4] =
{
    -9999,
    -9999,
    -9999,
    -9999
};

int cachedMonitorBatteryPercent =
    -9999;

int cachedMonitorBatteryColorState =
    -1;


// =====================================================
// TOUCH / GESTURE STATE
// =====================================================

bool pairGestureLatched =
    false;

bool ignoreTouchUntilRelease =
    false;

bool singleTouchActive =
    false;

unsigned long singleTouchDownMs =
    0;

unsigned long lastTapMs =
    0;

int singleTouchStartX =
    0;

int singleTouchStartY =
    0;

int singleTouchMaxMoveX =
    0;

int singleTouchMaxMoveY =
    0;


// =====================================================
// RESET UI POINTERS / CACHE
// =====================================================

void clearUiPointers()
{
    monitorBatteryLabel =
        nullptr;

    cachedMonitorBatteryPercent =
        -9999;

    cachedMonitorBatteryColorState =
        -1;


    for (int id = 0; id < 4; id++)
    {
        cachedThermalState[id] =
            THERMAL_CACHE_UNKNOWN;

        cachedThermalBattery[id] =
            -999;


        cachedTpmsState[id] =
            -1;

        cachedTpmsPsiTenths[id] =
            -9999;

        cachedTpmsPeakPsiTenths[id] =
            -9999;

        cachedTpmsTempF[id] =
            -9999;

        cachedTpmsPeakTempF[id] =
            -9999;

        cachedTpmsBatteryPercent[id] =
            -9999;


        tirePanel[id] =
            nullptr;

        positionTag[id] =
            nullptr;

        tireNameLabel[id] =
            nullptr;

        batteryLabel[id] =
            nullptr;

        statusLabel[id] =
            nullptr;

        pressureLabel[id] =
            nullptr;


        for (int i = 0; i < 5; i++)
        {
            cachedZoneTemp[id][i] =
                -9999;

            cachedZoneMax[id][i] =
                -9999;

            cachedZoneBand[id][i] =
                -1;


            zoneBox[id][i] =
                nullptr;

            zoneValueLabel[id][i] =
                nullptr;

            zoneNumberLabel[id][i] =
                nullptr;

            zoneMaxLabel[id][i] =
                nullptr;
        }
    }
}


// =====================================================
// TEMPERATURE BAND
// =====================================================

int getTempBand(float temp)
{
    if (temp < TEMP_COLD_F)
        return 0;

    if (temp < TEMP_WARM_F)
        return 1;

    if (temp < TEMP_READY_F)
        return 2;

    if (temp < TEMP_OPTIMAL_HIGH_F)
        return 3;

    if (temp < TEMP_HOT_F)
        return 4;

    if (temp <= TEMP_UPPER_F)
        return 5;

    return 6;
}


// =====================================================
// TEMPERATURE COLOR
// =====================================================

lv_color_t getTempColor(float temp)
{
    if (temp < TEMP_COLD_F)
        return lv_color_hex(
            0x0066FF
        );

    else if (temp < TEMP_WARM_F)
        return lv_color_hex(
            0x00FFFF
        );

    else if (temp < TEMP_READY_F)
        return lv_color_hex(
            0x00FF44
        );

    else if (temp < TEMP_OPTIMAL_HIGH_F)
        return lv_color_hex(
            0xC8FF00
        );

    else if (temp < TEMP_HOT_F)
        return lv_color_hex(
            0xFF8C00
        );

    else if (temp <= TEMP_UPPER_F)
        return lv_color_hex(
            0xFF3030
        );

    return lv_color_hex(
        0xFF0000
    );
}


// =====================================================
// TEMPERATURE TEXT COLOR
// =====================================================

lv_color_t getTempTextColor(float temp)
{
    if (temp < TEMP_COLD_F)
        return lv_color_hex(
            0xFFFFFF
        );

    else if (temp < TEMP_HOT_F)
        return lv_color_hex(
            0x000000
        );

    return lv_color_hex(
        0xFFFFFF
    );
}


// =====================================================
// MONITOR BATTERY VOLTAGE
// =====================================================

float readMonitorBatteryVoltage()
{
    const int samples =
        32;

    uint32_t totalMv =
        0;


    for (int i = 0; i < samples; i++)
    {
        totalMv +=
            analogReadMilliVolts(
                MONITOR_BATTERY_PIN
            );
    }


    float adcVoltage =
        (
            totalMv /
            (float)samples
        )
        /
        1000.0f;


    return
        adcVoltage
        *
        2.0f
        *
        MONITOR_BATTERY_CALIBRATION;
}


// =====================================================
// MONITOR BATTERY PERCENT
// =====================================================

int monitorVoltageToPercent(float v)
{
    const float volts[] =
    {
        3.40f,
        3.50f,
        3.60f,
        3.65f,
        3.70f,
        3.75f,
        3.80f,
        3.85f,
        3.90f,
        3.95f,
        4.00f,
        4.06f
    };


    const int percents[] =
    {
        0,
        5,
        15,
        25,
        35,
        45,
        55,
        65,
        75,
        85,
        95,
        100
    };


    const int count =
        sizeof(volts)
        /
        sizeof(volts[0]);


    if (v <= volts[0])
        return 0;


    if (v >= volts[count - 1])
        return 100;


    for (int i = 0; i < count - 1; i++)
    {
        if (
            v >= volts[i]
            &&
            v <= volts[i + 1]
        )
        {
            float ratio =
                (
                    v -
                    volts[i]
                )
                /
                (
                    volts[i + 1]
                    -
                    volts[i]
                );


            float percent =
                percents[i]
                +
                (
                    percents[i + 1]
                    -
                    percents[i]
                )
                *
                ratio;


            return
                (int)(
                    percent + 0.5f
                );
        }
    }


    return 0;
}


// =====================================================
// UPDATE MONITOR BATTERY
// =====================================================

void updateMonitorBattery()
{
    static unsigned long lastBatteryRead =
        0;

    static bool firstRead =
        true;

    static float smoothedVoltage =
        0.0f;


    unsigned long now =
        millis();


    if (
        firstRead
        ||
        now - lastBatteryRead >=
            MONITOR_BATTERY_UPDATE_MS
    )
    {
        float newVoltage =
            readMonitorBatteryVoltage();


        if (firstRead)
        {
            smoothedVoltage =
                newVoltage;

            firstRead =
                false;
        }

        else
        {
            smoothedVoltage =
                (
                    smoothedVoltage *
                    0.75f
                )
                +
                (
                    newVoltage *
                    0.25f
                );
        }


        monitorBatteryVoltage =
            smoothedVoltage;


        monitorBatteryPercent =
            monitorVoltageToPercent(
                monitorBatteryVoltage
            );


        lastBatteryRead =
            now;
    }


    if (
        monitorBatteryLabel == nullptr
        ||
        monitorBatteryPercent < 0
    )
    {
        return;
    }


    int colorState;


    if (monitorBatteryPercent <= 5)
    {
        colorState =
            0;
    }

    else if (monitorBatteryPercent <= 20)
    {
        colorState =
            1;
    }

    else
    {
        colorState =
            2;
    }


    if (
        monitorBatteryPercent
        !=
        cachedMonitorBatteryPercent
    )
    {
        char buffer[40];


        snprintf(
            buffer,
            sizeof(buffer),
            "(Mon. Batt: %d%%)",
            monitorBatteryPercent
        );


        lv_label_set_text(
            monitorBatteryLabel,
            buffer
        );


        cachedMonitorBatteryPercent =
            monitorBatteryPercent;
    }


    if (
        colorState
        !=
        cachedMonitorBatteryColorState
    )
    {
        uint32_t color;


        if (colorState == 0)
        {
            color =
                COLOR_BAD;
        }

        else if (colorState == 1)
        {
            color =
                COLOR_WARN;
        }

        else
        {
            color =
                COLOR_GOOD;
        }


        lv_obj_set_style_text_color(
            monitorBatteryLabel,
            lv_color_hex(
                color
            ),
            0
        );


        cachedMonitorBatteryColorState =
            colorState;
    }
}


// =====================================================
// CELSIUS -> FAHRENHEIT
// =====================================================

float celsiusToFahrenheit(int c)
{
    return
        (
            c *
            9.0f /
            5.0f
        )
        +
        32.0f;
}


// =====================================================
// CR1632 BATTERY PERCENT
// =====================================================

int cr1632VoltageToPercent(float v)
{
    const float volts[] =
    {
        2.50f,
        2.60f,
        2.70f,
        2.80f,
        2.90f,
        3.00f,
        3.10f,
        3.20f,
        3.30f
    };


    const int percents[] =
    {
        0,
        5,
        15,
        30,
        50,
        70,
        85,
        95,
        100
    };


    const int count =
        sizeof(volts)
        /
        sizeof(volts[0]);


    if (v <= volts[0])
        return 0;


    if (v >= volts[count - 1])
        return 100;


    for (int i = 0; i < count - 1; i++)
    {
        if (
            v >= volts[i]
            &&
            v <= volts[i + 1]
        )
        {
            float ratio =
                (
                    v -
                    volts[i]
                )
                /
                (
                    volts[i + 1]
                    -
                    volts[i]
                );


            float percent =
                percents[i]
                +
                (
                    percents[i + 1]
                    -
                    percents[i]
                )
                *
                ratio;


            return
                (int)(
                    percent + 0.5f
                );
        }
    }


    return 0;
}


// =====================================================
// TPMS MAC LOOKUP
// =====================================================

int tpmsIndexFromAddress(
    const String &address
)
{
    for (int i = 0; i < 4; i++)
    {
        if (
            address.equalsIgnoreCase(
                tpmsMac[i]
            )
        )
        {
            return i;
        }
    }


    return -1;
}


// =====================================================
// NIMBLE TPMS CALLBACK
// =====================================================

class TPMSScanCallbacks :
    public NimBLEScanCallbacks
{
    void onResult(
        const NimBLEAdvertisedDevice *advertisedDevice
    ) override
    {
        String address =
            advertisedDevice
                ->getAddress()
                .toString()
                .c_str();


        int id =
            tpmsIndexFromAddress(
                address
            );


        if (
            id < 0
            ||
            !advertisedDevice
                ->haveManufacturerData()
        )
        {
            return;
        }


        std::string mfg =
            advertisedDevice
                ->getManufacturerData();


        if (mfg.length() < 7)
        {
            return;
        }


        const uint8_t *data =
            reinterpret_cast<const uint8_t *>(
                mfg.data()
            );


        float batteryV =
            data[1]
            /
            10.0f;


        int8_t tempRaw =
            (int8_t)data[2];


        uint16_t pressureRaw =
            (
                (uint16_t)data[3]
                <<
                8
            )
            |
            (uint16_t)data[4];


        float gaugePsi =
            (
                pressureRaw /
                10.0f
            )
            -
            TPMS_ATMOSPHERE_PSI;


        if (gaugePsi < 0.0f)
        {
            gaugePsi =
                0.0f;
        }


        portENTER_CRITICAL(
            &tpmsMux
        );


        bool firstReading =
            !tpms[id].received;


        tpms[id].pressurePsi =
            gaugePsi;


        tpms[id].temperatureC =
            (int)tempRaw;


        // Session peak PSI / temperature
        if (firstReading)
        {
            tpms[id].peakPressurePsi =
                gaugePsi;


            tpms[id].peakTemperatureC =
                (int)tempRaw;
        }

        else
        {
            if (
                gaugePsi >
                tpms[id].peakPressurePsi
            )
            {
                tpms[id].peakPressurePsi =
                    gaugePsi;
            }


            if (
                (int)tempRaw >
                tpms[id].peakTemperatureC
            )
            {
                tpms[id].peakTemperatureC =
                    (int)tempRaw;
            }
        }


        tpms[id].batteryVoltage =
            batteryV;


        tpms[id].rssi =
            advertisedDevice
                ->getRSSI();


        tpms[id].lastUpdate =
            millis();


        tpms[id].received =
            true;


        portEXIT_CRITICAL(
            &tpmsMux
        );
    }
};


TPMSScanCallbacks tpmsScanCallbacks;


// =====================================================
// NIMBLE CONTINUOUS SCANNING
// =====================================================

void setupTPMS()
{
    NimBLEDevice::init(
        ""
    );


    tpmsScan =
        NimBLEDevice::getScan();


    // true = report duplicate advertisements.
    //
    // Important for TPMS because the same MAC address
    // continually broadcasts updated measurements.
    tpmsScan->setScanCallbacks(
        &tpmsScanCallbacks,
        true
    );


    // Passive scan.
    //
    // TPMS information is already contained directly
    // in the advertisement. No scan-response request
    // is required.
    tpmsScan->setActiveScan(
        false
    );


    // =================================================
    // CONTINUOUS 100% SCAN WINDOW
    //
    // Interval = 100 ms
    // Window   = 100 ms
    //
    // No intentional BLE blind period.
    // =====================================================

    tpmsScan->setInterval(
        100
    );


    tpmsScan->setWindow(
        100
    );


    // Do not store discovered devices in a results list.
    // We only need advertisements delivered to callback.
    tpmsScan->setMaxResults(
        0
    );


    // 0 = scan indefinitely.
    tpmsScan->start(
        0
    );
}


// =====================================================
// KEEP NIMBLE SCANNER RUNNING
//
// Normally start(0) runs indefinitely.
// This provides a simple recovery in case the scan
// unexpectedly stops.
// =====================================================

void processTPMSScan()
{
    if (
        tpmsScan == nullptr
    )
    {
        return;
    }


    if (
        !tpmsScan->isScanning()
    )
    {
        tpmsScan->start(
            0
        );
    }
}


// =====================================================
// UPDATE TPMS LABEL
//
// 0 = Never received
// 1 = Stale > 1 minute
// 2 = Fresh
// =====================================================

void updateTPMSLabel(int id)
{
    if (
        pressureLabel[id] ==
        nullptr
    )
    {
        return;
    }


    TPMSData t;


    portENTER_CRITICAL(
        &tpmsMux
    );


    t =
        tpms[id];


    portEXIT_CRITICAL(
        &tpmsMux
    );


    int state;


    int psiTenths =
        -9999;


    int peakPsiTenths =
        -9999;


    int tempF =
        -9999;


    int peakTempF =
        -9999;


    int batteryPercent =
        -9999;


    // =================================================
    // NEVER RECEIVED
    // =====================================================

    if (!t.received)
    {
        state =
            0;
    }

    else
    {
        psiTenths =
            (int)lroundf(
                t.pressurePsi
                *
                10.0f
            );


        peakPsiTenths =
            (int)lroundf(
                t.peakPressurePsi
                *
                10.0f
            );


        tempF =
            (int)lroundf(
                celsiusToFahrenheit(
                    t.temperatureC
                )
            );


        peakTempF =
            (int)lroundf(
                celsiusToFahrenheit(
                    t.peakTemperatureC
                )
            );


        batteryPercent =
            cr1632VoltageToPercent(
                t.batteryVoltage
            );


        // No TPMS update for more than 1 minute:
        // retain last measurement but show it yellow.
        if (
            millis()
            -
            t.lastUpdate
            >
            TPMS_STALE_MS
        )
        {
            state =
                1;
        }

        else
        {
            state =
                2;
        }
    }


    // =================================================
    // NOTHING VISIBLE CHANGED
    // =====================================================

    if (
        state ==
            cachedTpmsState[id]
        &&
        psiTenths ==
            cachedTpmsPsiTenths[id]
        &&
        peakPsiTenths ==
            cachedTpmsPeakPsiTenths[id]
        &&
        tempF ==
            cachedTpmsTempF[id]
        &&
        peakTempF ==
            cachedTpmsPeakTempF[id]
        &&
        batteryPercent ==
            cachedTpmsBatteryPercent[id]
    )
    {
        return;
    }


    // =================================================
    // NEVER RECEIVED
    // =====================================================

    if (state == 0)
    {
        lv_label_set_text(
            pressureLabel[id],
            "--.- PSI (--.-)  ---F (---)  --%"
        );


        lv_obj_set_style_text_color(
            pressureLabel[id],
            lv_color_hex(
                COLOR_SUBTEXT
            ),
            0
        );
    }


    // =================================================
    // VALID TPMS READING
    // =====================================================

    else
    {
        char pressureBuffer[64];


        snprintf(
            pressureBuffer,
            sizeof(pressureBuffer),
            "%.1f PSI (%.1f)  %dF (%d)  %d%%",
            psiTenths / 10.0f,
            peakPsiTenths / 10.0f,
            tempF,
            peakTempF,
            batteryPercent
        );


        lv_label_set_text(
            pressureLabel[id],
            pressureBuffer
        );


        if (state == 1)
        {
            // Stale > 1 minute
            lv_obj_set_style_text_color(
                pressureLabel[id],
                lv_color_hex(
                    COLOR_WARN
                ),
                0
            );
        }

        else
        {
            // Fresh
            lv_obj_set_style_text_color(
                pressureLabel[id],
                lv_color_hex(
                    COLOR_TEXT
                ),
                0
            );
        }
    }


    // =================================================
    // UPDATE CACHE
    // =====================================================

    cachedTpmsState[id] =
        state;


    cachedTpmsPsiTenths[id] =
        psiTenths;


    cachedTpmsPeakPsiTenths[id] =
        peakPsiTenths;


    cachedTpmsTempF[id] =
        tempF;


    cachedTpmsPeakTempF[id] =
        peakTempF;


    cachedTpmsBatteryPercent[id] =
        batteryPercent;
}


// =====================================================
// ESP-NOW RECEIVE CALLBACK
// =====================================================

void onDataRecv(
    const esp_now_recv_info_t *info,
    const uint8_t *incomingData,
    int len
)
{
    if (
        len !=
        sizeof(TirePacket)
    )
    {
        return;
    }


    TirePacket packet;


    memcpy(
        &packet,
        incomingData,
        sizeof(packet)
    );


    if (
        packet.tireID >
        3
    )
    {
        return;
    }


    int id =
        packet.tireID;


    portENTER_CRITICAL(
        &tireMux
    );


    bool firstPacket =
        !tires[id].received;


    tires[id].battery =
        packet.battery;


    for (int i = 0; i < 5; i++)
    {
        float newTemp =
            packet.zone[i]
            /
            10.0f;


        if (firstPacket)
        {
            tires[id].zoneMax[i] =
                newTemp;
        }

        else if (
            newTemp >
            tires[id].zoneMax[i]
        )
        {
            tires[id].zoneMax[i] =
                newTemp;
        }


        tires[id].zone[i] =
            newTemp;
    }


    tires[id].received =
        true;


    tires[id].lastUpdate =
        millis();


    portEXIT_CRITICAL(
        &tireMux
    );
}


// =====================================================
// ESP-NOW SETUP
// =====================================================

void setupEspNow()
{
    WiFi.mode(
        WIFI_STA
    );


    delay(
        100
    );


    if (
        esp_now_init()
        !=
        ESP_OK
    )
    {
        return;
    }


    esp_now_register_recv_cb(
        onDataRecv
    );
}


// =====================================================
// THERMAL SENSOR BATTERY COLOR
// =====================================================

void updateBatteryColor(
    int id,
    int battery
)
{
    if (
        battery <=
        BAT_LOW_PERCENT
    )
    {
        lv_obj_set_style_text_color(
            batteryLabel[id],
            lv_color_hex(
                COLOR_BAD
            ),
            0
        );
    }

    else if (
        battery <=
        BAT_WARN_PERCENT
    )
    {
        lv_obj_set_style_text_color(
            batteryLabel[id],
            lv_color_hex(
                COLOR_WARN
            ),
            0
        );
    }

    else
    {
        lv_obj_set_style_text_color(
            batteryLabel[id],
            lv_color_hex(
                COLOR_GOOD
            ),
            0
        );
    }
}


// =====================================================
// WAITING STATE
// =====================================================

void setPanelWaitingState(int id)
{
    if (
        tirePanel[id] ==
        nullptr
    )
    {
        return;
    }


    if (
        cachedThermalState[id]
        ==
        THERMAL_CACHE_WAITING
    )
    {
        return;
    }


    lv_label_set_text(
        statusLabel[id],
        "WAITING"
    );


    lv_obj_set_style_text_color(
        statusLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_set_style_border_color(
        tirePanel[id],
        lv_color_hex(
            COLOR_PANEL_BORDER
        ),
        0
    );


    lv_label_set_text(
        batteryLabel[id],
        "BAT --%"
    );


    lv_obj_set_style_text_color(
        batteryLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    for (int i = 0; i < 5; i++)
    {
        lv_label_set_text(
            zoneValueLabel[id][i],
            "---"
        );


        lv_label_set_text(
            zoneMaxLabel[id][i],
            "---"
        );


        lv_obj_set_style_bg_color(
            zoneBox[id][i],
            lv_color_hex(
                COLOR_IDLE
            ),
            0
        );


        lv_obj_set_style_text_color(
            zoneValueLabel[id][i],
            lv_color_hex(
                COLOR_TEXT
            ),
            0
        );


        lv_obj_set_style_text_color(
            zoneNumberLabel[id][i],
            lv_color_hex(
                COLOR_SUBTEXT
            ),
            0
        );


        cachedZoneTemp[id][i] =
            -9999;


        cachedZoneMax[id][i] =
            -9999;


        cachedZoneBand[id][i] =
            -1;
    }


    cachedThermalBattery[id] =
        -999;


    cachedThermalState[id] =
        THERMAL_CACHE_WAITING;
}


// =====================================================
// NO SIGNAL STATE
// =====================================================

void setPanelNoSignalState(
    int id,
    const TireData &t
)
{
    if (
        tirePanel[id] ==
        nullptr
    )
    {
        return;
    }


    if (
        cachedThermalState[id]
        ==
        THERMAL_CACHE_NO_SIGNAL
    )
    {
        return;
    }


    lv_label_set_text(
        statusLabel[id],
        "NO SIGNAL"
    );


    lv_obj_set_style_text_color(
        statusLabel[id],
        lv_color_hex(
            COLOR_BAD
        ),
        0
    );


    lv_obj_set_style_border_color(
        tirePanel[id],
        lv_color_hex(
            COLOR_BAD
        ),
        0
    );


    lv_label_set_text(
        batteryLabel[id],
        "BAT --%"
    );


    lv_obj_set_style_text_color(
        batteryLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    for (int i = 0; i < 5; i++)
    {
        lv_label_set_text(
            zoneValueLabel[id][i],
            "---"
        );


        int maxRounded =
            (int)lroundf(
                t.zoneMax[i]
            );


        char maxBuffer[16];


        snprintf(
            maxBuffer,
            sizeof(maxBuffer),
            "%d",
            maxRounded
        );


        lv_label_set_text(
            zoneMaxLabel[id][i],
            maxBuffer
        );


        lv_obj_set_style_bg_color(
            zoneBox[id][i],
            lv_color_hex(
                COLOR_IDLE
            ),
            0
        );


        lv_obj_set_style_text_color(
            zoneValueLabel[id][i],
            lv_color_hex(
                COLOR_TEXT
            ),
            0
        );


        lv_obj_set_style_text_color(
            zoneNumberLabel[id][i],
            lv_color_hex(
                COLOR_SUBTEXT
            ),
            0
        );


        cachedZoneTemp[id][i] =
            -9999;


        cachedZoneMax[id][i] =
            maxRounded;


        cachedZoneBand[id][i] =
            -1;
    }


    cachedThermalBattery[id] =
        -999;


    cachedThermalState[id] =
        THERMAL_CACHE_NO_SIGNAL;
}


// =====================================================
// CREATE NORMAL FOUR-TIRE PANEL
// =====================================================

void createNormalTirePanel(
    int id,
    int x,
    int y
)
{
    tirePanel[id] =
        lv_obj_create(
            lv_scr_act()
        );


    lv_obj_set_size(
        tirePanel[id],
        NORMAL_PANEL_W,
        NORMAL_PANEL_H
    );


    lv_obj_set_pos(
        tirePanel[id],
        x,
        y
    );


    lv_obj_clear_flag(
        tirePanel[id],
        LV_OBJ_FLAG_SCROLLABLE
    );


    lv_obj_set_style_bg_color(
        tirePanel[id],
        lv_color_hex(
            COLOR_PANEL
        ),
        0
    );


    lv_obj_set_style_border_color(
        tirePanel[id],
        lv_color_hex(
            COLOR_PANEL_BORDER
        ),
        0
    );


    lv_obj_set_style_border_width(
        tirePanel[id],
        2,
        0
    );


    lv_obj_set_style_radius(
        tirePanel[id],
        8,
        0
    );


    lv_obj_set_style_pad_all(
        tirePanel[id],
        8,
        0
    );


    // Tire position
    positionTag[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        positionTag[id],
        tireShortNames[id]
    );


    lv_obj_set_style_text_font(
        positionTag[id],
        &lv_font_montserrat_30,
        0
    );


    lv_obj_set_style_text_color(
        positionTag[id],
        lv_color_hex(
            COLOR_ACCENT
        ),
        0
    );


    lv_obj_align(
        positionTag[id],
        LV_ALIGN_TOP_LEFT,
        2,
        -2
    );


    // Tire name
    tireNameLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        tireNameLabel[id],
        tireNames[id]
    );


    lv_obj_set_style_text_font(
        tireNameLabel[id],
        &lv_font_montserrat_16,
        0
    );


    lv_obj_set_style_text_color(
        tireNameLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_align(
        tireNameLabel[id],
        LV_ALIGN_TOP_LEFT,
        48,
        6
    );


    // Monitor battery only in FL
    if (id == 0)
    {
        monitorBatteryLabel =
            lv_label_create(
                tirePanel[id]
            );


        lv_label_set_text(
            monitorBatteryLabel,
            "(Mon. Batt: --%)"
        );


        lv_obj_set_style_text_font(
            monitorBatteryLabel,
            &lv_font_montserrat_14,
            0
        );


        lv_obj_set_style_text_color(
            monitorBatteryLabel,
            lv_color_hex(
                COLOR_GOOD
            ),
            0
        );


        lv_obj_align_to(
            monitorBatteryLabel,
            tireNameLabel[id],
            LV_ALIGN_OUT_RIGHT_MID,
            7,
            0
        );
    }


    // Thermal node battery
    batteryLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        batteryLabel[id],
        "BAT --%"
    );


    lv_obj_set_style_text_font(
        batteryLabel[id],
        &lv_font_montserrat_16,
        0
    );


    lv_obj_align(
        batteryLabel[id],
        LV_ALIGN_TOP_RIGHT,
        -4,
        4
    );


    // Five thermal zones
    for (int i = 0; i < 5; i++)
    {
        zoneBox[id][i] =
            lv_obj_create(
                tirePanel[id]
            );


        lv_obj_set_size(
            zoneBox[id][i],
            66,
            100
        );


        lv_obj_set_pos(
            zoneBox[id][i],
            8 + (i * 74),
            48
        );


        lv_obj_clear_flag(
            zoneBox[id][i],
            LV_OBJ_FLAG_SCROLLABLE
        );


        lv_obj_set_style_bg_color(
            zoneBox[id][i],
            lv_color_hex(
                COLOR_IDLE
            ),
            0
        );


        lv_obj_set_style_border_width(
            zoneBox[id][i],
            0,
            0
        );


        lv_obj_set_style_radius(
            zoneBox[id][i],
            6,
            0
        );


        lv_obj_set_style_pad_all(
            zoneBox[id][i],
            2,
            0
        );


        zoneNumberLabel[id][i] =
            lv_label_create(
                zoneBox[id][i]
            );


        lv_label_set_text_fmt(
            zoneNumberLabel[id][i],
            "Z%d",
            i + 1
        );


        lv_obj_set_style_text_font(
            zoneNumberLabel[id][i],
            &lv_font_montserrat_14,
            0
        );


        lv_obj_set_style_text_color(
            zoneNumberLabel[id][i],
            lv_color_hex(
                COLOR_SUBTEXT
            ),
            0
        );


        lv_obj_align(
            zoneNumberLabel[id][i],
            LV_ALIGN_TOP_MID,
            0,
            4
        );


        zoneValueLabel[id][i] =
            lv_label_create(
                zoneBox[id][i]
            );


        lv_label_set_text(
            zoneValueLabel[id][i],
            "---"
        );


        lv_obj_set_style_text_font(
            zoneValueLabel[id][i],
            &lv_font_montserrat_30,
            0
        );


        lv_obj_set_style_text_color(
            zoneValueLabel[id][i],
            lv_color_hex(
                COLOR_TEXT
            ),
            0
        );


        lv_obj_align(
            zoneValueLabel[id][i],
            LV_ALIGN_CENTER,
            0,
            10
        );


        zoneMaxLabel[id][i] =
            lv_label_create(
                tirePanel[id]
            );


        lv_label_set_text(
            zoneMaxLabel[id][i],
            "---"
        );


        lv_obj_set_style_text_font(
            zoneMaxLabel[id][i],
            &lv_font_montserrat_14,
            0
        );


        lv_obj_set_style_text_color(
            zoneMaxLabel[id][i],
            lv_color_hex(
                COLOR_MAX
            ),
            0
        );


        lv_obj_align_to(
            zoneMaxLabel[id][i],
            zoneBox[id][i],
            LV_ALIGN_OUT_BOTTOM_MID,
            0,
            5
        );
    }


    // TPMS line
    pressureLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        pressureLabel[id],
        "--.- PSI (--.-)  ---F (---)  --%"
    );


    lv_obj_set_style_text_font(
        pressureLabel[id],
        &lv_font_montserrat_16,
        0
    );


    lv_obj_set_style_text_color(
        pressureLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_align(
        pressureLabel[id],
        LV_ALIGN_BOTTOM_LEFT,
        4,
        -4
    );


    // Thermal sensor status
    statusLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        statusLabel[id],
        "WAITING"
    );


    lv_obj_set_style_text_font(
        statusLabel[id],
        &lv_font_montserrat_14,
        0
    );


    lv_obj_set_style_text_color(
        statusLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_align(
        statusLabel[id],
        LV_ALIGN_BOTTOM_RIGHT,
        -4,
        -4
    );


    setPanelWaitingState(
        id
    );
}


// =====================================================
// CREATE LARGE TWO-TIRE PANEL
// =====================================================

void createLargeTirePanel(
    int id,
    int x
)
{
    tirePanel[id] =
        lv_obj_create(
            lv_scr_act()
        );


    lv_obj_set_size(
        tirePanel[id],
        390,
        464
    );


    lv_obj_set_pos(
        tirePanel[id],
        x,
        8
    );


    lv_obj_clear_flag(
        tirePanel[id],
        LV_OBJ_FLAG_SCROLLABLE
    );


    lv_obj_set_style_bg_color(
        tirePanel[id],
        lv_color_hex(
            COLOR_PANEL
        ),
        0
    );


    lv_obj_set_style_border_color(
        tirePanel[id],
        lv_color_hex(
            COLOR_PANEL_BORDER
        ),
        0
    );


    lv_obj_set_style_border_width(
        tirePanel[id],
        2,
        0
    );


    lv_obj_set_style_radius(
        tirePanel[id],
        8,
        0
    );


    lv_obj_set_style_pad_all(
        tirePanel[id],
        8,
        0
    );


    positionTag[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        positionTag[id],
        tireShortNames[id]
    );


    lv_obj_set_style_text_font(
        positionTag[id],
        &lv_font_montserrat_30,
        0
    );


    lv_obj_set_style_text_color(
        positionTag[id],
        lv_color_hex(
            COLOR_TEXT
        ),
        0
    );


    lv_obj_align(
        positionTag[id],
        LV_ALIGN_TOP_LEFT,
        4,
        4
    );


    tireNameLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        tireNameLabel[id],
        tireNames[id]
    );


    lv_obj_set_style_text_font(
        tireNameLabel[id],
        &lv_font_montserrat_26,
        0
    );


    lv_obj_set_style_text_color(
        tireNameLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_align(
        tireNameLabel[id],
        LV_ALIGN_TOP_LEFT,
        58,
        6
    );


    batteryLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        batteryLabel[id],
        "BAT --%"
    );


    lv_obj_set_style_text_font(
        batteryLabel[id],
        &lv_font_montserrat_16,
        0
    );


    lv_obj_align(
        batteryLabel[id],
        LV_ALIGN_TOP_RIGHT,
        -4,
        44
    );


    for (int i = 0; i < 5; i++)
    {
        zoneBox[id][i] =
            lv_obj_create(
                tirePanel[id]
            );


        lv_obj_set_size(
            zoneBox[id][i],
            68,
            180
        );


        lv_obj_set_pos(
            zoneBox[id][i],
            8 + (i * 72),
            80
        );


        lv_obj_clear_flag(
            zoneBox[id][i],
            LV_OBJ_FLAG_SCROLLABLE
        );


        lv_obj_set_style_bg_color(
            zoneBox[id][i],
            lv_color_hex(
                COLOR_IDLE
            ),
            0
        );


        lv_obj_set_style_border_width(
            zoneBox[id][i],
            0,
            0
        );


        lv_obj_set_style_radius(
            zoneBox[id][i],
            7,
            0
        );


        lv_obj_set_style_pad_all(
            zoneBox[id][i],
            2,
            0
        );


        zoneNumberLabel[id][i] =
            lv_label_create(
                zoneBox[id][i]
            );


        lv_label_set_text_fmt(
            zoneNumberLabel[id][i],
            "Z%d",
            i + 1
        );


        lv_obj_set_style_text_font(
            zoneNumberLabel[id][i],
            &lv_font_montserrat_16,
            0
        );


        lv_obj_set_style_text_color(
            zoneNumberLabel[id][i],
            lv_color_hex(
                COLOR_SUBTEXT
            ),
            0
        );


        lv_obj_align(
            zoneNumberLabel[id][i],
            LV_ALIGN_TOP_MID,
            0,
            12
        );


        zoneValueLabel[id][i] =
            lv_label_create(
                zoneBox[id][i]
            );


        lv_label_set_text(
            zoneValueLabel[id][i],
            "---"
        );


        lv_obj_set_style_text_font(
            zoneValueLabel[id][i],
            &lv_font_montserrat_30,
            0
        );


        lv_obj_set_style_text_color(
            zoneValueLabel[id][i],
            lv_color_hex(
                COLOR_TEXT
            ),
            0
        );


        lv_obj_align(
            zoneValueLabel[id][i],
            LV_ALIGN_CENTER,
            0,
            15
        );


        zoneMaxLabel[id][i] =
            lv_label_create(
                tirePanel[id]
            );


        lv_label_set_text(
            zoneMaxLabel[id][i],
            "---"
        );


        lv_obj_set_style_text_font(
            zoneMaxLabel[id][i],
            &lv_font_montserrat_16,
            0
        );


        lv_obj_set_style_text_color(
            zoneMaxLabel[id][i],
            lv_color_hex(
                COLOR_MAX
            ),
            0
        );


        lv_obj_align_to(
            zoneMaxLabel[id][i],
            zoneBox[id][i],
            LV_ALIGN_OUT_BOTTOM_MID,
            0,
            8
        );
    }


    // =================================================
    // TPMS LINE
    //
    // Raised so it does not overlap the thermal
    // WAITING / LIVE / NO SIGNAL line.
    // =====================================================

    pressureLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        pressureLabel[id],
        "--.- PSI (--.-)  ---F (---)  --%"
    );


    lv_obj_set_style_text_font(
        pressureLabel[id],
        &lv_font_montserrat_26,
        0
    );


    lv_obj_set_style_text_color(
        pressureLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_align(
        pressureLabel[id],
        LV_ALIGN_BOTTOM_LEFT,
        8,
        -40
    );


    statusLabel[id] =
        lv_label_create(
            tirePanel[id]
        );


    lv_label_set_text(
        statusLabel[id],
        "WAITING"
    );


    lv_obj_set_style_text_font(
        statusLabel[id],
        &lv_font_montserrat_16,
        0
    );


    lv_obj_set_style_text_color(
        statusLabel[id],
        lv_color_hex(
            COLOR_SUBTEXT
        ),
        0
    );


    lv_obj_align(
        statusLabel[id],
        LV_ALIGN_BOTTOM_RIGHT,
        -8,
        -8
    );


    setPanelWaitingState(
        id
    );
}


// =====================================================
// SCREEN BACKGROUND
// =====================================================

void setupScreenBackground()
{
    lv_obj_set_style_bg_color(
        lv_scr_act(),
        lv_color_hex(
            COLOR_BG
        ),
        0
    );


    lv_obj_set_style_bg_opa(
        lv_scr_act(),
        LV_OPA_COVER,
        0
    );
}


// =====================================================
// CREATE FOUR-TIRE DASHBOARD
// =====================================================

void createFourTireDashboard()
{
    lv_obj_clean(
        lv_scr_act()
    );


    clearUiPointers();


    setupScreenBackground();


    createNormalTirePanel(
        0,
        normalPanelX[0],
        normalPanelY[0]
    );


    createNormalTirePanel(
        1,
        normalPanelX[1],
        normalPanelY[1]
    );


    createNormalTirePanel(
        2,
        normalPanelX[2],
        normalPanelY[2]
    );


    createNormalTirePanel(
        3,
        normalPanelX[3],
        normalPanelY[3]
    );
}


// =====================================================
// CREATE TWO-TIRE DASHBOARD
// =====================================================

void createTwoTireDashboard(
    int tireA,
    int tireB
)
{
    lv_obj_clean(
        lv_scr_act()
    );


    clearUiPointers();


    setupScreenBackground();


    createLargeTirePanel(
        tireA,
        8
    );


    createLargeTirePanel(
        tireB,
        402
    );
}


// =====================================================
// UPDATE ONE TIRE PANEL
// =====================================================

void updateOneTirePanel(int id)
{
    if (
        id < 0
        ||
        id > 3
    )
    {
        return;
    }


    if (
        tirePanel[id]
        ==
        nullptr
    )
    {
        return;
    }


    // TPMS is independent of thermal ESP-NOW sensors.
    updateTPMSLabel(
        id
    );


    TireData t;


    portENTER_CRITICAL(
        &tireMux
    );


    t =
        tires[id];


    portEXIT_CRITICAL(
        &tireMux
    );


    if (!t.received)
    {
        setPanelWaitingState(
            id
        );

        return;
    }


    if (
        millis()
        -
        t.lastUpdate
        >
        SIGNAL_TIMEOUT_MS
    )
    {
        setPanelNoSignalState(
            id,
            t
        );

        return;
    }


    // Transition to LIVE
    if (
        cachedThermalState[id]
        !=
        THERMAL_CACHE_LIVE
    )
    {
        lv_label_set_text(
            statusLabel[id],
            "LIVE"
        );


        lv_obj_set_style_text_color(
            statusLabel[id],
            lv_color_hex(
                COLOR_GOOD
            ),
            0
        );


        lv_obj_set_style_border_color(
            tirePanel[id],
            lv_color_hex(
                COLOR_PANEL_BORDER
            ),
            0
        );


        cachedThermalBattery[id] =
            -999;


        for (int i = 0; i < 5; i++)
        {
            cachedZoneTemp[id][i] =
                -9999;


            cachedZoneMax[id][i] =
                -9999;


            cachedZoneBand[id][i] =
                -1;
        }


        cachedThermalState[id] =
            THERMAL_CACHE_LIVE;
    }


    // Thermal sensor battery
    if (
        (int)t.battery
        !=
        cachedThermalBattery[id]
    )
    {
        char batteryBuffer[32];


        snprintf(
            batteryBuffer,
            sizeof(batteryBuffer),
            "BAT %d%%",
            t.battery
        );


        lv_label_set_text(
            batteryLabel[id],
            batteryBuffer
        );


        updateBatteryColor(
            id,
            t.battery
        );


        cachedThermalBattery[id] =
            (int)t.battery;
    }


    // Thermal zones
    for (int i = 0; i < 5; i++)
    {
        int tempRounded =
            (int)lroundf(
                t.zone[i]
            );


        int maxRounded =
            (int)lroundf(
                t.zoneMax[i]
            );


        int tempBand =
            getTempBand(
                t.zone[i]
            );


        if (
            tempRounded
            !=
            cachedZoneTemp[id][i]
        )
        {
            char tempBuffer[16];


            snprintf(
                tempBuffer,
                sizeof(tempBuffer),
                "%d",
                tempRounded
            );


            lv_label_set_text(
                zoneValueLabel[id][i],
                tempBuffer
            );


            cachedZoneTemp[id][i] =
                tempRounded;
        }


        if (
            maxRounded
            !=
            cachedZoneMax[id][i]
        )
        {
            char maxBuffer[16];


            snprintf(
                maxBuffer,
                sizeof(maxBuffer),
                "%d",
                maxRounded
            );


            lv_label_set_text(
                zoneMaxLabel[id][i],
                maxBuffer
            );


            cachedZoneMax[id][i] =
                maxRounded;
        }


        if (
            tempBand
            !=
            cachedZoneBand[id][i]
        )
        {
            lv_color_t bgColor =
                getTempColor(
                    t.zone[i]
                );


            lv_color_t textColor =
                getTempTextColor(
                    t.zone[i]
                );


            lv_obj_set_style_bg_color(
                zoneBox[id][i],
                bgColor,
                0
            );


            lv_obj_set_style_text_color(
                zoneValueLabel[id][i],
                textColor,
                0
            );


            lv_obj_set_style_text_color(
                zoneNumberLabel[id][i],
                textColor,
                0
            );


            cachedZoneBand[id][i] =
                tempBand;
        }
    }
}


// =====================================================
// UPDATE DASHBOARD
// =====================================================

void updateDashboard()
{
    static unsigned long lastScreenUpdate =
        0;


    if (
        millis()
        -
        lastScreenUpdate
        <
        SCREEN_UPDATE_MS
    )
    {
        return;
    }


    lastScreenUpdate =
        millis();


    lvgl_port_lock(
        -1
    );


    updateMonitorBattery();


    if (
        displayMode
        ==
        MODE_FOUR_TIRES
    )
    {
        for (int id = 0; id < 4; id++)
        {
            updateOneTirePanel(
                id
            );
        }
    }

    else
    {
        updateOneTirePanel(
            selectedTireA
        );


        updateOneTirePanel(
            selectedTireB
        );
    }


    lvgl_port_unlock();
}


// =====================================================
// FIND TIRE HEADER
// =====================================================

int getHeaderTireAt(
    int x,
    int y
)
{
    for (int id = 0; id < 4; id++)
    {
        int left =
            normalPanelX[id];


        int right =
            normalPanelX[id]
            +
            NORMAL_PANEL_W;


        int top =
            normalPanelY[id];


        int bottom =
            normalPanelY[id]
            +
            HEADER_TOUCH_H;


        if (
            x >= left
            &&
            x <= right
            &&
            y >= top
            &&
            y <= bottom
        )
        {
            return id;
        }
    }


    return -1;
}


// =====================================================
// FLASH SELECTED PAIR
// =====================================================

void flashPairSelection(
    int tireA,
    int tireB
)
{
    lvgl_port_lock(
        -1
    );


    if (
        tirePanel[tireA]
        !=
        nullptr
    )
    {
        lv_obj_set_style_border_color(
            tirePanel[tireA],
            lv_color_hex(
                COLOR_SELECT
            ),
            0
        );


        lv_obj_set_style_border_width(
            tirePanel[tireA],
            4,
            0
        );
    }


    if (
        tirePanel[tireB]
        !=
        nullptr
    )
    {
        lv_obj_set_style_border_color(
            tirePanel[tireB],
            lv_color_hex(
                COLOR_SELECT
            ),
            0
        );


        lv_obj_set_style_border_width(
            tirePanel[tireB],
            4,
            0
        );
    }


    lvgl_port_unlock();


    delay(
        120
    );
}


// =====================================================
// OPEN TWO-TIRE VIEW
// =====================================================

void openTwoTireView(
    int tireA,
    int tireB
)
{
    if (tireA > tireB)
    {
        int temp =
            tireA;

        tireA =
            tireB;

        tireB =
            temp;
    }


    flashPairSelection(
        tireA,
        tireB
    );


    selectedTireA =
        tireA;


    selectedTireB =
        tireB;


    displayMode =
        MODE_TWO_TIRES;


    lvgl_port_lock(
        -1
    );


    createTwoTireDashboard(
        selectedTireA,
        selectedTireB
    );


    lvgl_port_unlock();


    ignoreTouchUntilRelease =
        true;


    singleTouchActive =
        false;


    lastTapMs =
        0;


    pairGestureLatched =
        true;
}


// =====================================================
// RETURN TO FOUR-TIRE VIEW
// =====================================================

void returnToFourTireView()
{
    displayMode =
        MODE_FOUR_TIRES;


    selectedTireA =
        -1;


    selectedTireB =
        -1;


    lvgl_port_lock(
        -1
    );


    createFourTireDashboard();


    lvgl_port_unlock();


    ignoreTouchUntilRelease =
        true;


    singleTouchActive =
        false;


    lastTapMs =
        0;


    pairGestureLatched =
        false;
}


// =====================================================
// TOUCH GESTURES
// =====================================================

void processTouchGestures()
{
    if (
        touchPanel ==
        nullptr
    )
    {
        return;
    }


    TouchPoint points[5];


    int pointCount =
        touchPanel->readPoints(
            points,
            5,
            0
        );


    if (pointCount < 0)
    {
        return;
    }


    if (ignoreTouchUntilRelease)
    {
        if (pointCount == 0)
        {
            ignoreTouchUntilRelease =
                false;


            singleTouchActive =
                false;


            pairGestureLatched =
                false;
        }


        return;
    }


    // =================================================
    // FOUR-TIRE VIEW
    // =====================================================

    if (
        displayMode
        ==
        MODE_FOUR_TIRES
    )
    {
        singleTouchActive =
            false;


        lastTapMs =
            0;


        if (
            pointCount >= 2
            &&
            !pairGestureLatched
        )
        {
            int firstTire =
                -1;


            int secondTire =
                -1;


            for (int i = 0; i < pointCount; i++)
            {
                int hit =
                    getHeaderTireAt(
                        points[i].x,
                        points[i].y
                    );


                if (hit < 0)
                {
                    continue;
                }


                if (firstTire < 0)
                {
                    firstTire =
                        hit;
                }

                else if (
                    hit
                    !=
                    firstTire
                )
                {
                    secondTire =
                        hit;

                    break;
                }
            }


            if (
                firstTire >= 0
                &&
                secondTire >= 0
            )
            {
                pairGestureLatched =
                    true;


                openTwoTireView(
                    firstTire,
                    secondTire
                );


                return;
            }
        }


        if (pointCount == 0)
        {
            pairGestureLatched =
                false;
        }


        return;
    }


    // =================================================
    // TWO-TIRE VIEW
    // =====================================================

    if (
        displayMode
        ==
        MODE_TWO_TIRES
    )
    {
        if (pointCount == 1)
        {
            int x =
                points[0].x;


            int y =
                points[0].y;


            if (!singleTouchActive)
            {
                singleTouchActive =
                    true;


                singleTouchDownMs =
                    millis();


                singleTouchStartX =
                    x;


                singleTouchStartY =
                    y;


                singleTouchMaxMoveX =
                    0;


                singleTouchMaxMoveY =
                    0;
            }

            else
            {
                int moveX =
                    abs(
                        x -
                        singleTouchStartX
                    );


                int moveY =
                    abs(
                        y -
                        singleTouchStartY
                    );


                if (
                    moveX
                    >
                    singleTouchMaxMoveX
                )
                {
                    singleTouchMaxMoveX =
                        moveX;
                }


                if (
                    moveY
                    >
                    singleTouchMaxMoveY
                )
                {
                    singleTouchMaxMoveY =
                        moveY;
                }
            }


            return;
        }


        if (pointCount > 1)
        {
            singleTouchActive =
                false;


            return;
        }


        if (
            pointCount == 0
            &&
            singleTouchActive
        )
        {
            unsigned long now =
                millis();


            unsigned long tapDuration =
                now -
                singleTouchDownMs;


            bool validTap =
                tapDuration <=
                    TAP_MAX_MS
                &&
                singleTouchMaxMoveX <=
                    TAP_MAX_MOVE_PX
                &&
                singleTouchMaxMoveY <=
                    TAP_MAX_MOVE_PX;


            singleTouchActive =
                false;


            if (!validTap)
            {
                lastTapMs =
                    0;


                return;
            }


            if (
                lastTapMs != 0
                &&
                now - lastTapMs <=
                    DOUBLE_TAP_MS
            )
            {
                lastTapMs =
                    0;


                returnToFourTireView();


                return;
            }


            lastTapMs =
                now;
        }
    }
}


// =====================================================
// BOOT BUTTON / WAKE
// =====================================================

void configureBootButton()
{
    esp_sleep_wakeup_cause_t wakeCause =
        esp_sleep_get_wakeup_cause();


    if (
        wakeCause
        ==
        ESP_SLEEP_WAKEUP_EXT0
    )
    {
        rtc_gpio_deinit(
            GPIO_NUM_0
        );
    }


    pinMode(
        BOOT_BUTTON_PIN,
        INPUT_PULLUP
    );
}


// =====================================================
// DEEP SLEEP
// =====================================================

void enterDeepSleep()
{
    // Wait until BOOT is released.
    while (
        digitalRead(
            BOOT_BUTTON_PIN
        )
        ==
        LOW
    )
    {
        delay(
            10
        );
    }


    delay(
        50
    );


    rtc_gpio_pullup_en(
        GPIO_NUM_0
    );


    rtc_gpio_pulldown_dis(
        GPIO_NUM_0
    );


    esp_err_t wakeResult =
        esp_sleep_enable_ext0_wakeup(
            GPIO_NUM_0,
            0
        );


    if (
        wakeResult
        !=
        ESP_OK
    )
    {
        return;
    }


    // Stop NimBLE scan before sleep.
    if (
        tpmsScan != nullptr
        &&
        tpmsScan->isScanning()
    )
    {
        tpmsScan->stop();
    }


    esp_now_deinit();


    WiFi.mode(
        WIFI_OFF
    );


    delay(
        50
    );


    // Backlight OFF
    if (
        board != nullptr
        &&
        board->getBacklight()
        != nullptr
    )
    {
        board
            ->getBacklight()
            ->off();
    }


    delay(
        100
    );


    Serial.flush();


    esp_deep_sleep_start();
}


// =====================================================
// BOOT BUTTON PROCESSING
// =====================================================

void processBootSleepButton()
{
    static unsigned long pressStartMs =
        0;


    static bool sleepTriggered =
        false;


    bool pressed =
        digitalRead(
            BOOT_BUTTON_PIN
        )
        ==
        LOW;


    if (pressed)
    {
        if (pressStartMs == 0)
        {
            pressStartMs =
                millis();


            sleepTriggered =
                false;
        }


        if (
            !sleepTriggered
            &&
            millis() - pressStartMs >=
                BOOT_HOLD_MS
        )
        {
            sleepTriggered =
                true;


            enterDeepSleep();


            pressStartMs =
                0;


            sleepTriggered =
                false;
        }
    }

    else
    {
        pressStartMs =
            0;


        sleepTriggered =
            false;
    }
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(
        500
    );


    configureBootButton();


    // =================================================
    // INITIALIZE WAVESHARE
    // =====================================================

    board =
        new Board();


    board->init();


    delay(
        1500
    );


    // =================================================
    // RGB DISPLAY
    //
    // Keep known-good bounce buffer at *10.
    // =====================================================

#if LVGL_PORT_AVOID_TEARING_MODE

    auto lcd =
        board->getLCD();


    lcd->configFrameBufferNumber(
        LVGL_PORT_DISP_BUFFER_NUM
    );


#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3

    auto lcd_bus =
        lcd->getBus();


    if (
        lcd_bus
            ->getBasicAttributes()
            .type
        ==
        ESP_PANEL_BUS_TYPE_RGB
    )
    {
        static_cast<BusRGB *>(
            lcd_bus
        )->configRGB_BounceBufferSize(
            lcd->getFrameWidth()
            *
            10
        );
    }

#endif
#endif


    // =================================================
    // START BOARD
    // =====================================================

    bool boardResult =
        board->begin();


    if (!boardResult)
    {
        while (true)
        {
            delay(
                1000
            );
        }
    }


    // =================================================
    // MONITOR BATTERY ADC
    // =====================================================

    pinMode(
        MONITOR_BATTERY_PIN,
        INPUT
    );


    analogReadResolution(
        12
    );


    analogSetPinAttenuation(
        MONITOR_BATTERY_PIN,
        ADC_11db
    );


    // =================================================
    // TOUCH
    // =====================================================

    touchPanel =
        board->getTouch();


    // =================================================
    // LVGL
    // =====================================================

    bool lvglResult =
        lvgl_port_init(
            board->getLCD(),
            nullptr
        );


    if (!lvglResult)
    {
        while (true)
        {
            delay(
                1000
            );
        }
    }


    lvgl_port_lock(
        -1
    );


    createFourTireDashboard();


    lvgl_port_unlock();


    // =================================================
    // ESP-NOW
    // =====================================================

    setupEspNow();


    // =================================================
    // NIMBLE TPMS
    //
    // Continuous passive scanning.
    // 100 ms interval / 100 ms window.
    // =====================================================

    setupTPMS();
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
    processBootSleepButton();


    processTouchGestures();


    // NimBLE normally scans forever.
    // This simply restarts it if it ever stops.
    processTPMSScan();


    updateDashboard();


    delay(
        TOUCH_POLL_MS
    );
}