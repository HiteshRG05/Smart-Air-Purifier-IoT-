
# Smart Air Purifier – IoT

An IoT-enabled air purifier that measures real-time air quality and pushes live sensor data to a cloud dashboard for remote monitoring. Built as a custom PCB with integrated particulate and environmental sensing, paired with HEPA filtration and ESP32-based wireless communication.

## Overview

This project combines hardware design and embedded firmware to solve a simple problem: know your indoor air quality in real time, not just after the fact. The system reads PM2.5, PM10, temperature, and humidity, drives a HEPA filtration unit, and streams live data to the cloud so air quality can be checked remotely.

## Features

- Custom-designed PCB (schematic capture + routing in KiCad)
- Real-time PM2.5 / PM10 particulate sensing
- Temperature & humidity monitoring
- HEPA filtration control based on live air quality readings
- ESP32-based Wi-Fi connectivity for IoT communication
- Live sensor data pushed to a cloud dashboard (MQTT/HTTP → ThingSpeak) for remote monitoring
- Version-controlled build tracked on GitHub

## Hardware Used

| Component | Purpose |

| ESP32 | Main microcontroller, Wi-Fi/IoT communication |
| PM2.5 / PM10 sensor | Particulate matter sensing (UART) |
| Temperature & humidity sensor | Environmental monitoring |
| HEPA filter unit | Air filtration | Displaying (I2C)
| Custom PCB (KiCad) | Integrates all sensors and power/control circuitry |

## Software & Tools

- **Firmware:** Arduino IDE (C/C++)
- **PCB Design:** KiCad (schematic + PCB routing)
- **Cloud:** MQTT / HTTP to ThingSpeak dashboard
- **Version Control:** Git & GitHub

## How It Works

1. The PM2.5, PM10, and temperature/humidity sensors continuously read air quality data over I2C.
2. The ESP32 processes these readings and controls the HEPA filtration system based on air quality thresholds.
3. Sensor data is published over MQTT/HTTP to a cloud dashboard (ThingSpeak) at regular intervals.
4. Air quality can be monitored remotely in real time from the dashboard.

## Repository Structure
├── AirPuri_Gerbers_Zip.zip # Manufacturing Gerber files for the PCB
├── air_purifier_main.ino # Main ESP32 firmware
├── images/ # PCB and build photos
└── README.md

## Images

![PCB Top View](images/pcb_top_view.jpg)
![Assembled Build](images/assembled_build.jpg)

## Future Improvements

- Add local Inter active OLED display for on-device readings
- Add historical data logging and trend graphs
- Migrate to a custom web dashboard instead of ThingSpeak

