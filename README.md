# ESP32 Wireless Thermal Tire Temperature System

A DIY motorsport tire temperature monitoring system using ESP32 microcontrollers, MLX90640 thermal cameras, and ESP-NOW wireless telemetry.

This project provides real-time multi-zone tread temperature analysis for track cars, race cars, and high-performance driving applications without requiring contact probes or expensive commercial equipment.

Full how-to walkthrough: https://youtu.be/BBLoeDo5oM8

---

**System Overview**
The system consists of three main components:
1. Wheel-mounted thermal sensor nodes
2. BLE TPMS sensors
3. A central Waveshare ESP32-S3 4.3" touchscreen receiver/display

**Wheel Thermal Sensor Nodes**
Each wheel thermal module contains:
- ESP32
- MLX90640 thermal camera
- Battery power
- ESP-NOW transmitter
- Local tire-detection and filtering logic

Each node continuously:
1. Captures thermal images from the MLX90640
2. Detects the tire surface
3. Applies an elliptical tire mask
4. Splits the tread into 5 temperature zones
5. Filters noisy pixels
6. Rejects brake-related hotspots
7. Applies EMA smoothing
8. Mirrors zone orientation where required for right-side tires
9. Tracks processed tire temperatures
10. Sends zone temperatures and sensor status wirelessly over ESP-NOW
Processing is performed on the wheel node so only compact processed telemetry is sent to the display rather than the full thermal image.
BLE TPMS Sensors

Each wheel can also use a BLE valve-stem TPMS sensor to provide:
- Tire pressure
- Internal tire temperature
- TPMS battery voltage / estimated battery level
- BLE RSSI signal strength
- Session peak pressure
- Session peak TPMS temperature

The display identifies each sensor by MAC address and maps it to:
- Front Left
- Front Right
- Rear Left
- Rear Right

The TPMS sensors broadcast their data over BLE advertising packets.
Waveshare Receiver / Display
The central display uses a Waveshare ESP32-S3 Touch LCD 4.3 with an 800×480 touchscreen.

It:
- Receives thermal telemetry over ESP-NOW
- Continuously scans TPMS advertisements using NimBLE
- Decodes tire pressure, temperature, battery, and RSSI
- Displays live thermal zone temperatures
- Displays live TPMS pressure and temperature
- Tracks session peak values
- Monitors sensor freshness
- Retains the last valid TPMS reading if packets are temporarily missed
- Highlights stale TPMS data visually
- Monitors remote sensor battery levels
- Monitors its own LiPo battery
- Supports multitouch interaction
- Supports deep sleep and physical-button wake
The display interface is built with LVGL and optimized to minimize unnecessary redraws for stable operation on the ESP32-S3 RGB display.

**Features**
Thermal Tire Monitoring
- Real-time thermal tire monitoring
- MLX90640 thermal camera integration
- Automatic tire region detection
- Ellipse-based tire masking
- 5-zone tread temperature analysis
- Right-side tire zone mirroring
- EMA smoothing and filtering
- Brake hotspot rejection
- Session peak zone temperature tracking
- Temperature-based zone color visualization
- 
Wireless Telemetry
- ESP-NOW wireless telemetry
- Low-latency sensor-to-display communication
- Battery-powered remote sensor nodes
- Local processing before transmission
- Compact processed telemetry packets
- Independent thermal and TPMS data paths
- Lightweight ESP32 firmware
- Designed for motorsport environments
  
TPMS Integration
- BLE valve-stem TPMS support
- Continuous NimBLE passive scanning
- Real-time tire pressure monitoring
- TPMS temperature monitoring
- TPMS battery monitoring
- RSSI signal-strength tracking
- Session peak pressure tracking
- Session peak TPMS temperature tracking
- Last-known-value retention
- Stale-data detection
- Visual stale-data warning
- Fixed FL / FR / RL / RR sensor mapping
- Absolute-to-gauge pressure conversion
- Duplicate advertisement handling
  
Touchscreen Dashboard
- Waveshare ESP32-S3 Touch LCD 4.3 integration
- 800×480 LVGL interface
- Four-tire overview screen
- Multitouch two-tire zoom mode
- High-contrast motorsport UI
- Live 5-zone temperature display
- Live pressure and TPMS temperature display
- Session peak values
- Sensor battery indicators
- TPMS stale-state indicators
- Color-coded thermal zones
- Change-only UI redraws for improved stability
Power Management
- Onboard display LiPo battery monitoring
- Remote thermal-node battery monitoring
- TPMS coin-cell battery estimation
- BOOT-button deep sleep
- Physical-button wake
- Session peak reset after reboot / wake
Calibration and Processing
- Live browser-based calibration UI
- Tire-region calibration
- Automatic tire surface detection
- Ellipse mask configuration
- Zone positioning and orientation
- Per-wheel mapping
- Thermal noise filtering
- Brake hotspot rejection
- EMA temperature smoothing
Reliability and Display Optimization
- Independent timeout handling for thermal and TPMS data
- Last-valid TPMS reading preservation
- Stale TPMS indication without clearing data
- Continuous NimBLE scanning
- Reduced memory usage by avoiding stored BLE scan results
- Optimized LVGL redraw behavior
- RGB anti-tearing configuration
- Tuned RGB bounce buffer
- Stable simultaneous BLE, ESP-NOW, touchscreen, and display operation

---

# Hardware

## Main Components

| Component               | Qty         | Notes                    |
| ----------------------- | ----------- | ------------------------ |
| ESP32 Dev Board         | 1 per wheel | ESP32 lite w/ jst recommended   |
| MLX90640 Thermal Camera | 1 per wheel | 55° or 110° FOV          |
| LiPo Battery            | 1 per wheel          | 2000mah recommended      |
| Power Switch            | 2 per wheel           |                          |
| Wheel arch mount encl.  | 1 per wheel          | Heat resistant preferred PETG or ASA |
| Waveshare ESP32-S3 Touch LCD 4.3
| Touchscreen enclosure with Vent Mount

---

# Recommended Parts
Disclaimer: Some of the links below are affiliate links, which means I may earn a commission at no extra cost to you.

As an Amazon Associate I earn from qualifying purchases.

1. Esp32 Lites 4 pack  https://www.amazon.com/dp/B0BZP3GXT7?th=1&linkCode=ll2&tag=traviscea05-20&linkId=61597949ec335a3ab2f504bd427caaa9&language=en_US&ref_=as_li_ss_tl
2. 55° Thermal Camera(MLX90640) x4 needed https://www.amazon.com/dp/B07ZKK8QWY?&linkCode=ll2&tag=traviscea05-20&linkId=89015596541410dd8700740aabaff986&language=en_US&ref_=as_li_ss_tl <br>
Other 55° Thermal Camera link if first is sold out https://www.amazon.com/dp/B0F312VWW3?&linkCode=ll2&tag=traviscea05-20&linkId=9bcd7ac8df73fbbeacc3a000f99c4359&language=en_US&ref_=as_li_ss_tl
3. 2000mah battery 4 pack https://www.amazon.com/dp/B0FR9GH966?th=1&linkCode=ll2&tag=traviscea05-20&linkId=ce733239115802f7ae2e96530c1f4ace&language=en_US&ref_=as_li_ss_tl
4. Additional 2000mah battery 1 pack https://www.amazon.com/dp/B0FR9GH966?th=1&linkCode=ll2&tag=traviscea05-20&linkId=ce733239115802f7ae2e96530c1f4ace&language=en_US&ref_=as_li_ss_tl
5. Usb extension cable x4 needed https://www.amazon.com/dp/B08BYRH9DR?th=1&linkCode=ll2&tag=traviscea05-20&linkId=64e65b45900524723d50228c386b2803&language=en_US&ref_=as_li_ss_tl
6. SS12F15 Slide Switch, SPDT 1P2T 2 Position Mini Micro Toggle Switch https://www.amazon.com/dp/B0FNRDWD4B?ref=ppx_yo2ov_dt_b_fed_asin_title
7. Waveshare ESP32-S3 4.3inch Capacitive Touch Display Development Board, 800×480, 5-Point Touch, 32-Bit LX7 Dual-Core Processor, Up to 240MHz Frequency, with Onboard Antenna https://www.amazon.com/dp/B0CNCRZWVB?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_1
8. Car Valve Steam TPMS Bluetooth 5.0 Tire Pressure Monitoring System 4pcs External Sensor App https://www.ebay.com/itm/407071428316

---

# Wiring

**Waveshare ESP32-S3 Touch LCD 4.3 integration Battery Monitor Wiring**

LiPo Battery +
      |
    100kΩ
      |
      +----------> GPIO6
      |
    100kΩ
      |
     GND

## MLX90640 → ESP32

| MLX90640 | ESP32   |
| -------- | ------- |
| VIN      | 3.3V    |
| GND      | GND     |
| SDA      | GPIO 23 |
| SCL      | GPIO 19 |

---

## Calibration Mode Jumper

| ESP32   | Connection             |
| ------- | ---------------------- |
| GPIO 13 | GND = Calibration Mode |

---

## Battery Monitor

The tire sensor battery voltage is measured using a 100kΩ / 100kΩ voltage divider.

| Circuit                | ESP32   |
| ---------------------- | ------- |
| Voltage Divider Output | GPIO 34 |
| Divider Top            | LiPo +  |
| Divider Bottom         | GND     |

### Voltage Divider


LiPo +
  |
100kΩ
  |
  +------ GPIO 34
  |
100kΩ
  |
 GND

### Switch Wiring

3-Pin SPDT Switch

       OUTER     CENTER     OUTER
         |          |          |
        GND         EN       UNUSED

---

# Software Features

## Calibration Web Interface

When GPIO13 is grounded during boot:

* ESP32 launches WiFi AP
* Browser UI becomes available
* Live thermal image displayed
* Tire region can be adjusted
* Zone dividers can be dragged
* Calibration saved to onboard flash

### Default WiFi

SSID:

```text
TIRE-CAL-1
```

Password:

```text
12345678
```

---

# Temperature Processing

The firmware performs:

* Ambient temperature detection
* Tire isolation
* Elliptical masking
* Zone segmentation
* Noise filtering
* EMA smoothing
* Hotspot rejection

---

# Tire Zones

The tread is divided into 5 regions:

```text
[ OUTER ] [ MID-OUTER ] [ CENTER ] [ MID-INNER ] [ INNER ]
```

Right-side tires are automatically mirrored.

---

# ESP-NOW Communication

The node transmits:

* Tire ID
* Battery %
* 5 tread temperatures
* Hottest detected temperature

Low latency makes this suitable for:

* Real-time dashboards
* Pit telemetry
* Driver displays

---

# Receiver MAC Address

Update this section in the firmware:

```cpp
uint8_t receiverAddress[] = {
    0x11,
    0x53,
    0xCB,
    0x8F,
    0x89,
    0x49
};
```

Replace with your receiver ESP32 MAC.

---

# Tire IDs

Configure each node:

```cpp
#define TIRE_ID 0
```

| ID | Wheel       |
| -- | ----------- |
| 0  | Front Left  |
| 1  | Front Right |
| 2  | Rear Left   |
| 3  | Rear Right  |

---

# Arduino IDE Setup

## Required Libraries

Install:

* Adafruit MLX90640
* ESPAsyncWebServer
* AsyncTCP

---

# Build Settings

Recommended:

| Setting          | Value   |
| ---------------- | ------- |
| CPU Frequency    | 80MHz   |
| Flash Mode       | QIO     |
| Partition Scheme | Default |
| Upload Speed     | 921600  |

---

# Power Consumption

Optimizations include:

* 80MHz CPU operation
* ESP-NOW instead of WiFi runtime
* Minimal display overhead
* Frame filtering

Typical runtime depends on:

* Battery size
* Refresh rate
* ESP32 variant

---

# Mounting Recommendations

## Best Results

Mount the sensor:

* Facing tread surface
* Slightly downward angled
* Away from direct brake rotor view
* Protected from debris/water
* Rigidly mounted

---

# Suggested Placement

Typical mounting locations:

* Fender liner
* Suspension upright
* Chassis-mounted bracket
* Wheel well liner

---

# Thermal Considerations

Avoid:

* Rotor visibility
* Exhaust heat
* Direct sunlight exposure into lens
* Vibration-induced movement

---

# Known Limitations

* Surface temperature only
* Weather affects readings
* Dirt/water can impact accuracy
* Extreme brake glow may influence data

---

# Future Improvements

Planned ideas:

* SD card logging
* CAN bus integration
* OLED display support
* MQTT/WiFi telemetry
* Lap correlation
* Tire pressure estimation
* Multi-camera fusion
* Cloud telemetry dashboard

---

# Example Applications

* Track day analysis
* Tire pressure tuning
* Camber tuning
* Suspension setup
* Drift tire monitoring
* Endurance racing telemetry
* Autocross optimization

---

# Safety Notice

This project is experimental motorsport hardware.

Do NOT rely on it for:

* Safety-critical systems
* Autonomous control
* Road safety decisions

Always secure wiring and batteries properly in vehicle environments.

---

# License

MIT License

Feel free to modify, improve, and contribute, but never use to sell and licensed as such.

---

# Credits

Built using:

* ESP32
* MLX90640
* ESP-NOW
* ESPAsyncWebServer
* Adafruit MLX90640 Library

# Example Output

```text
FL:
185F 192F 198F 201F 195F

FR:
181F 188F 194F 196F 191F
```

---

# Author

Travis Way

DIY motorsport telemetry and embedded systems development.
