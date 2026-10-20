#include <Arduino.h>
#include <esp_display_panel.hpp>

#include <lvgl.h>
#include "lvgl_v8_port.h"

#include <WiFi.h>
#include <esp_now.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;


// =====================================================
// CONFIGURATION
// =====================================================

// Hoosier temperature bands
#define TEMP_COLD_F          140.0f
#define TEMP_WARM_F          160.0f
#define TEMP_READY_F         180.0f
#define TEMP_OPTIMAL_HIGH_F  210.0f
#define TEMP_HOT_F           230.0f
#define TEMP_UPPER_F         240.0f

// Tire sensor battery warning thresholds
#define BAT_WARN_PERCENT 40
#define BAT_LOW_PERCENT  20

// Tire considered disconnected after this time
#define SIGNAL_TIMEOUT_MS 3000

// Dashboard refresh rate
#define SCREEN_UPDATE_MS 250

// Touch polling
#define TOUCH_POLL_MS 15

// Double-tap behavior
#define TAP_MAX_MS       280
#define DOUBLE_TAP_MS    400
#define TAP_MAX_MOVE_PX  35

// Set to 1 to see raw touch coordinates
#define TOUCH_DEBUG 0


// =====================================================
// HIGH-VISIBILITY DAYLIGHT THEME
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

// Session MAX numbers underneath zone bars
#define COLOR_MAX           0xB0B0B0


// =====================================================
// FOUR-TIRE PANEL GEOMETRY
// =====================================================

const int normalPanelX[4] = {
    8, 410, 8, 410
};

const int normalPanelY[4] = {
    8, 8, 244, 244
};

#define NORMAL_PANEL_W 382
#define NORMAL_PANEL_H 228

// Upper portion of each panel used for pair selection
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

int selectedTireA = -1;
int selectedTireB = -1;


// =====================================================
// BOARD / TOUCH
// =====================================================

Board *board = nullptr;
Touch *touchPanel = nullptr;


// =====================================================
// ESP-NOW PACKET
//
// MUST REMAIN IDENTICAL TO TRANSMITTER
//
// 0 = FRONT LEFT
// 1 = FRONT RIGHT
// 2 = REAR LEFT
// 3 = REAR RIGHT
//
// "hottest" remains in the packet for compatibility,
// but this dashboard no longer displays it.
// =====================================================

typedef struct __attribute__((packed))
{
    uint8_t tireID;
    uint8_t battery;
    int16_t zone[5];
    int16_t hottest;

} TirePacket;


// =====================================================
// TIRE DATA
// =====================================================

struct TireData
{
    bool received;

    uint8_t battery;

    // Current zone temperatures
    float zone[5];

    // Highest temperature reached by each individual
    // zone since THIS DISPLAY booted/reset.
    float zoneMax[5];

    unsigned long lastUpdate;
};


TireData tires[4] = {};


// Protect data shared by ESP-NOW callback and UI
portMUX_TYPE tireMux =
    portMUX_INITIALIZER_UNLOCKED;


// =====================================================
// TIRE NAMES
// =====================================================

const char *tireNames[4] = {
    "FRONT LEFT",
    "FRONT RIGHT",
    "REAR LEFT",
    "REAR RIGHT"
};


const char *tireShortNames[4] = {
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


// Current temperature colored bars
lv_obj_t *zoneBox[4][5];

lv_obj_t *zoneValueLabel[4][5];

lv_obj_t *zoneNumberLabel[4][5];


// Session maximum number BELOW each colored bar
lv_obj_t *zoneMaxLabel[4][5];


// =====================================================
// TOUCH / GESTURE STATE
// =====================================================

bool pairGestureLatched =
    false;

bool ignoreTouchUntilRelease =
    false;


// Single-finger double-tap tracking
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
// CLEAR STORED LVGL POINTERS
// =====================================================

void clearUiPointers()
{
    for (int id = 0; id < 4; id++)
    {
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


        for (int i = 0; i < 5; i++)
        {
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
// HOOSIER TEMPERATURE COLORS
// =====================================================

lv_color_t getTempColor(float temp)
{
    // <140°F
    // Vivid blue
    if (temp < TEMP_COLD_F)
    {
        return lv_color_hex(
            0x0066FF
        );
    }

    // 140-160°F
    // Cyan
    else if (temp < TEMP_WARM_F)
    {
        return lv_color_hex(
            0x00FFFF
        );
    }

    // 160-180°F
    // Green
    else if (temp < TEMP_READY_F)
    {
        return lv_color_hex(
            0x00FF44
        );
    }

    // 180-210°F
    // Bright lime / yellow-green
    else if (temp < TEMP_OPTIMAL_HIGH_F)
    {
        return lv_color_hex(
            0xC8FF00
        );
    }

    // 210-230°F
    // Orange
    else if (temp < TEMP_HOT_F)
    {
        return lv_color_hex(
            0xFF8C00
        );
    }

    // 230-240°F
    // Red
    else if (temp <= TEMP_UPPER_F)
    {
        return lv_color_hex(
            0xFF3030
        );
    }

    // >240°F
    // Maximum red
    else
    {
        return lv_color_hex(
            0xFF0000
        );
    }
}


// =====================================================
// LIVE TEMPERATURE TEXT COLOR
//
// This remains exactly the same logic used before.
// =====================================================

lv_color_t getTempTextColor(float temp)
{
    // Blue background
    if (temp < TEMP_COLD_F)
    {
        return lv_color_hex(
            0xFFFFFF
        );
    }

    // Cyan through orange
    else if (temp < TEMP_HOT_F)
    {
        return lv_color_hex(
            0x000000
        );
    }

    // Red
    else
    {
        return lv_color_hex(
            0xFFFFFF
        );
    }
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
    // Validate packet size
    if (len != sizeof(TirePacket))
    {
        Serial.print(
            "Wrong packet size: "
        );

        Serial.println(
            len
        );

        return;
    }


    TirePacket packet;


    memcpy(
        &packet,
        incomingData,
        sizeof(packet)
    );


    // Validate tire ID
    if (packet.tireID > 3)
    {
        Serial.print(
            "Invalid Tire ID: "
        );

        Serial.println(
            packet.tireID
        );

        return;
    }


    int id =
        packet.tireID;


    portENTER_CRITICAL(
        &tireMux
    );


    // Remember whether this is the first valid
    // packet received from this tire.
    bool firstPacket =
        !tires[id].received;


    // =================================================
    // BATTERY
    // =====================================================

    tires[id].battery =
        packet.battery;


    // =================================================
    // FIVE CURRENT TEMPERATURES + FIVE SESSION MAXES
    // =====================================================

    for (int i = 0; i < 5; i++)
    {
        float newTemp =
            packet.zone[i] / 10.0f;


        // First packet establishes initial MAX
        if (firstPacket)
        {
            tires[id].zoneMax[i] =
                newTemp;
        }

        // After that, MAX can only increase
        else if (
            newTemp >
            tires[id].zoneMax[i]
        )
        {
            tires[id].zoneMax[i] =
                newTemp;
        }


        // Current live temperature
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


    delay(100);


    Serial.println();

    Serial.println(
        "============================"
    );

    Serial.println(
        "ESP-NOW DISPLAY RECEIVER"
    );

    Serial.println(
        "============================"
    );


    Serial.print(
        "DISPLAY MAC: "
    );

    Serial.println(
        WiFi.macAddress()
    );


    if (
        esp_now_init()
        != ESP_OK
    )
    {
        Serial.println(
            "ESP-NOW INIT FAILED"
        );

        return;
    }


    esp_now_register_recv_cb(
        onDataRecv
    );


    Serial.println(
        "ESP-NOW READY"
    );

    Serial.println(
        "============================"
    );
}


// =====================================================
// BATTERY COLOR
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
//
// No packet has EVER been received.
// Therefore there is no MAX yet.
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
        // Current temp
        lv_label_set_text(
            zoneValueLabel[id][i],
            "---"
        );


        // Session max
        lv_label_set_text(
            zoneMaxLabel[id][i],
            "---"
        );


        // Neutral background
        lv_obj_set_style_bg_color(
            zoneBox[id][i],
            lv_color_hex(
                COLOR_IDLE
            ),
            0
        );


        // Current number
        lv_obj_set_style_text_color(
            zoneValueLabel[id][i],
            lv_color_hex(
                COLOR_TEXT
            ),
            0
        );


        // Z1-Z5
        lv_obj_set_style_text_color(
            zoneNumberLabel[id][i],
            lv_color_hex(
                COLOR_SUBTEXT
            ),
            0
        );
    }
}


// =====================================================
// NO SIGNAL STATE
//
// Current reading becomes unavailable,
// but MAX values remain visible and remembered.
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
        // Current reading unavailable
        lv_label_set_text(
            zoneValueLabel[id][i],
            "---"
        );


        // Keep remembered session maximum
        char maxBuffer[16];


        snprintf(
            maxBuffer,
            sizeof(maxBuffer),
            "%.0f",
            t.zoneMax[i]
        );


        lv_label_set_text(
            zoneMaxLabel[id][i],
            maxBuffer
        );


        // Neutral bar because live data is unavailable
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
    }
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
    // =================================================
    // PANEL
    // =====================================================

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


    // =================================================
    // FL / FR / RL / RR
    // =====================================================

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


    // =================================================
    // FULL TIRE NAME
    // =====================================================

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


    // =================================================
    // BATTERY
    // =====================================================

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


    // =================================================
    // FIVE TEMPERATURE ZONES
    //
    // IMPORTANT:
    // The zone bars themselves are unchanged from
    // the version you already liked.
    // =====================================================

    for (int i = 0; i < 5; i++)
    {
        // -------------------------------------------------
        // COLORED ZONE BAR
        // -------------------------------------------------

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


        // -------------------------------------------------
        // Z1-Z5
        //
        // SAME FONT / LOCATION / STYLE AS BEFORE
        // -------------------------------------------------

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


        // -------------------------------------------------
        // CURRENT LIVE TEMPERATURE
        //
        // SAME 30pt FONT / LOCATION / STYLE AS BEFORE
        // -------------------------------------------------

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


        // -------------------------------------------------
        // SESSION MAXIMUM
        //
        // NEW FEATURE.
        // ONLY THE NUMBER IS DISPLAYED.
        //
        // It sits OUTSIDE and BELOW the colored bar.
        // -------------------------------------------------

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


    // =================================================
    // STATUS
    // =====================================================

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
    // =================================================
    // PANEL
    // =====================================================

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


    // =================================================
    // FL / FR / RL / RR
    // =====================================================

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


    // =================================================
    // TIRE NAME
    // =====================================================

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


    // =================================================
    // BATTERY
    // =====================================================

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


    // =================================================
    // LARGE TEMPERATURE ZONES
    // =====================================================

    for (int i = 0; i < 5; i++)
    {
        // -------------------------------------------------
        // COLORED BAR
        // -------------------------------------------------

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


        // -------------------------------------------------
        // Z1-Z5
        //
        // SAME STYLE AS EXISTING LARGE VIEW
        // -------------------------------------------------

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


        // -------------------------------------------------
        // CURRENT LIVE TEMP
        //
        // SAME STYLE AS EXISTING LARGE VIEW
        // -------------------------------------------------

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


        // -------------------------------------------------
        // SESSION MAX NUMBER
        //
        // Directly underneath the colored bar.
        // -------------------------------------------------

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
    // STATUS
    // =====================================================

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


    // Left
    createLargeTirePanel(
        tireA,
        8
    );


    // Right
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
        id < 0 ||
        id > 3
    )
    {
        return;
    }


    if (
        tirePanel[id] ==
        nullptr
    )
    {
        return;
    }


    TireData t;


    portENTER_CRITICAL(
        &tireMux
    );


    t =
        tires[id];


    portEXIT_CRITICAL(
        &tireMux
    );


    // =================================================
    // WAITING FOR FIRST PACKET
    // =====================================================

    if (!t.received)
    {
        setPanelWaitingState(
            id
        );

        return;
    }


    // =================================================
    // SIGNAL LOST
    //
    // Current temperature disappears.
    // MAX remains.
    // =====================================================

    if (
        millis() -
        t.lastUpdate >
        SIGNAL_TIMEOUT_MS
    )
    {
        setPanelNoSignalState(
            id,
            t
        );

        return;
    }


    // =================================================
    // LIVE
    // =====================================================

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


    // =================================================
    // BATTERY
    // =====================================================

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


    // =================================================
    // FIVE LIVE TEMPS + FIVE SESSION MAX VALUES
    // =====================================================

    for (int i = 0; i < 5; i++)
    {
        // -------------------------------------------------
        // CURRENT TEMP
        // -------------------------------------------------

        char tempBuffer[16];


        snprintf(
            tempBuffer,
            sizeof(tempBuffer),
            "%.0f",
            t.zone[i]
        );


        lv_label_set_text(
            zoneValueLabel[id][i],
            tempBuffer
        );


        // -------------------------------------------------
        // SESSION MAX
        //
        // Number only.
        // -------------------------------------------------

        char maxBuffer[16];


        snprintf(
            maxBuffer,
            sizeof(maxBuffer),
            "%.0f",
            t.zoneMax[i]
        );


        lv_label_set_text(
            zoneMaxLabel[id][i],
            maxBuffer
        );


        // -------------------------------------------------
        // LIVE THERMAL BACKGROUND
        // -------------------------------------------------

        lv_obj_set_style_bg_color(
            zoneBox[id][i],
            getTempColor(
                t.zone[i]
            ),
            0
        );


        // -------------------------------------------------
        // LIVE TEMP TEXT CONTRAST
        // -------------------------------------------------

        lv_obj_set_style_text_color(
            zoneValueLabel[id][i],
            getTempTextColor(
                t.zone[i]
            ),
            0
        );


        // -------------------------------------------------
        // Z1-Z5 TEXT CONTRAST
        // -------------------------------------------------

        lv_obj_set_style_text_color(
            zoneNumberLabel[id][i],
            getTempTextColor(
                t.zone[i]
            ),
            0
        );


        // -------------------------------------------------
        // MAX ALWAYS STAYS SUBTLE GRAY
        // -------------------------------------------------

        lv_obj_set_style_text_color(
            zoneMaxLabel[id][i],
            lv_color_hex(
                COLOR_MAX
            ),
            0
        );
    }
}


// =====================================================
// UPDATE DASHBOARD
// =====================================================

void updateDashboard()
{
    static unsigned long
        lastScreenUpdate = 0;


    if (
        millis() -
        lastScreenUpdate <
        SCREEN_UPDATE_MS
    )
    {
        return;
    }


    lastScreenUpdate =
        millis();


    lvgl_port_lock(-1);


    if (
        displayMode ==
        MODE_FOUR_TIRES
    )
    {
        for (
            int id = 0;
            id < 4;
            id++
        )
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
// FIND WHICH TIRE HEADER WAS TOUCHED
// =====================================================

int getHeaderTireAt(
    int x,
    int y
)
{
    for (
        int id = 0;
        id < 4;
        id++
    )
    {
        int left =
            normalPanelX[id];


        int right =
            normalPanelX[id]
            + NORMAL_PANEL_W;


        int top =
            normalPanelY[id];


        int bottom =
            normalPanelY[id]
            + HEADER_TOUCH_H;


        if (
            x >= left &&
            x <= right &&
            y >= top &&
            y <= bottom
        )
        {
            return id;
        }
    }


    return -1;
}


// =====================================================
// HIGHLIGHT SELECTED PAIR
// =====================================================

void flashPairSelection(
    int tireA,
    int tireB
)
{
    lvgl_port_lock(-1);


    if (
        tirePanel[tireA]
        != nullptr
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
        != nullptr
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


    delay(120);
}


// =====================================================
// OPEN TWO-TIRE VIEW
// =====================================================

void openTwoTireView(
    int tireA,
    int tireB
)
{
    // Keep predictable tire ordering
    if (tireA > tireB)
    {
        int temp =
            tireA;


        tireA =
            tireB;


        tireB =
            temp;
    }


    Serial.print(
        "Opening two-tire view: "
    );


    Serial.print(
        tireShortNames[tireA]
    );


    Serial.print(
        " + "
    );


    Serial.println(
        tireShortNames[tireB]
    );


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


    lvgl_port_lock(-1);


    createTwoTireDashboard(
        selectedTireA,
        selectedTireB
    );


    lvgl_port_unlock();


    // Ignore selection fingers until released
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
    Serial.println(
        "Returning to four-tire view"
    );


    displayMode =
        MODE_FOUR_TIRES;


    selectedTireA =
        -1;


    selectedTireB =
        -1;


    lvgl_port_lock(-1);


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
// PROCESS TOUCH GESTURES
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


    if (
        pointCount <
        0
    )
    {
        return;
    }


#if TOUCH_DEBUG

    if (pointCount > 0)
    {
        Serial.print(
            "Touches: "
        );


        Serial.println(
            pointCount
        );


        for (
            int i = 0;
            i < pointCount;
            i++
        )
        {
            Serial.print(
                "P"
            );

            Serial.print(
                i
            );

            Serial.print(
                " X="
            );

            Serial.print(
                points[i].x
            );

            Serial.print(
                " Y="
            );

            Serial.println(
                points[i].y
            );
        }
    }

#endif


    // =================================================
    // IGNORE EXISTING TOUCHES AFTER SCREEN CHANGE
    // =====================================================

    if (
        ignoreTouchUntilRelease
    )
    {
        if (
            pointCount ==
            0
        )
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
    // FOUR-TIRE MODE
    //
    // Two different headers touched simultaneously
    // =====================================================

    if (
        displayMode ==
        MODE_FOUR_TIRES
    )
    {
        singleTouchActive =
            false;


        lastTapMs =
            0;


        if (
            pointCount >= 2 &&
            !pairGestureLatched
        )
        {
            int firstTire =
                -1;


            int secondTire =
                -1;


            for (
                int i = 0;
                i < pointCount;
                i++
            )
            {
                int hit =
                    getHeaderTireAt(
                        points[i].x,
                        points[i].y
                    );


                if (
                    hit < 0
                )
                {
                    continue;
                }


                if (
                    firstTire < 0
                )
                {
                    firstTire =
                        hit;
                }

                else if (
                    hit !=
                    firstTire
                )
                {
                    secondTire =
                        hit;


                    break;
                }
            }


            if (
                firstTire >= 0 &&
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


        if (
            pointCount ==
            0
        )
        {
            pairGestureLatched =
                false;
        }


        return;
    }


    // =================================================
    // TWO-TIRE MODE
    //
    // One-finger double tap returns to 4 tires
    // =====================================================

    if (
        displayMode ==
        MODE_TWO_TIRES
    )
    {
        // -------------------------------------------------
        // ONE FINGER DOWN
        // -------------------------------------------------

        if (
            pointCount ==
            1
        )
        {
            int x =
                points[0].x;


            int y =
                points[0].y;


            if (
                !singleTouchActive
            )
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
                    moveX >
                    singleTouchMaxMoveX
                )
                {
                    singleTouchMaxMoveX =
                        moveX;
                }


                if (
                    moveY >
                    singleTouchMaxMoveY
                )
                {
                    singleTouchMaxMoveY =
                        moveY;
                }
            }


            return;
        }


        // -------------------------------------------------
        // MULTIPLE FINGERS
        //
        // Not a valid double-tap
        // -------------------------------------------------

        if (
            pointCount >
            1
        )
        {
            singleTouchActive =
                false;


            return;
        }


        // -------------------------------------------------
        // FINGER RELEASED
        // -------------------------------------------------

        if (
            pointCount ==
                0
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


            if (
                !validTap
            )
            {
                lastTapMs =
                    0;


                return;
            }


            // =================================================
            // SECOND TAP
            // =================================================

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


            // =================================================
            // FIRST TAP
            // =================================================

            lastTapMs =
                now;
        }
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


    delay(500);


    Serial.println();

    Serial.println(
        "============================"
    );

    Serial.println(
        "TIRE TEMPERATURE SYSTEM"
    );

    Serial.println(
        "============================"
    );


    // =================================================
    // INITIALIZE WAVESHARE
    // =====================================================

    Serial.println(
        "Initializing board"
    );


    board =
        new Board();


    board->init();


    delay(1500);


    // =================================================
    // DISPLAY BUFFER CONFIGURATION
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
            * 10
        );
    }

#endif
#endif


    // =================================================
    // START BOARD
    // =====================================================

    Serial.println(
        "Starting board"
    );


    bool boardResult =
        board->begin();


    if (
        !boardResult
    )
    {
        Serial.println(
            "BOARD BEGIN FAILED"
        );


        while (true)
        {
            delay(1000);
        }
    }


    Serial.println(
        "Board ready"
    );


    // =================================================
    // TOUCH
    // =====================================================

    touchPanel =
        board->getTouch();


    if (
        touchPanel ==
        nullptr
    )
    {
        Serial.println(
            "WARNING: TOUCH NOT AVAILABLE"
        );
    }

    else
    {
        Serial.println(
            "GT911 touch ready"
        );
    }


    // =================================================
    // INITIALIZE LVGL
    //
    // nullptr is intentional.
    //
    // LVGL controls the display only.
    // Our own code reads GT911 directly so
    // multitouch continues to work.
    // =====================================================

    Serial.println(
        "Starting LVGL"
    );


    bool lvglResult =
        lvgl_port_init(
            board->getLCD(),
            nullptr
        );


    if (
        !lvglResult
    )
    {
        Serial.println(
            "LVGL INIT FAILED"
        );


        while (true)
        {
            delay(1000);
        }
    }


    Serial.println(
        "LVGL ready"
    );


    // =================================================
    // CREATE INITIAL DASHBOARD
    // =====================================================

    lvgl_port_lock(-1);


    createFourTireDashboard();


    lvgl_port_unlock();


    Serial.println(
        "Dashboard ready"
    );


    // =================================================
    // ESP-NOW
    // =====================================================

    setupEspNow();


    Serial.println();

    Serial.println(
        "============================"
    );

    Serial.println(
        "SYSTEM READY"
    );

    Serial.println(
        "============================"
    );


    Serial.println(
        "Zone session MAX tracking enabled"
    );


    Serial.println(
        "Two-finger tire selection enabled"
    );


    Serial.println(
        "Double tap exits two-tire view"
    );
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
    // Multitouch / double-tap gestures
    processTouchGestures();


    // Live telemetry
    updateDashboard();


    delay(
        TOUCH_POLL_MS
    );
}