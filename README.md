# Pocket Pitwall

**Pocket Pitwall** is a specialized F1 race replay visualizer designed for the **M5Stack Cardputer**. It provides a real-time, handheld "pit wall" experience by fetching live or historic race data and rendering driver positions on a dynamic track layout.

## Purpose

The project aims to bridge the gap between complex F1 telemetry and a portable, hardware-based viewing experience. By utilizing the OpenF1 API (via a middleware server), it transforms raw coordinate data into a smooth, visual representation of a Grand Prix, allowing users to track driver positions, lap times, and fastest laps on a dedicated ESP32-S3 device.

## How It Works

### 1. Data Retrieval & Processing

The system operates through a multi-step data acquisition process:

* **Initial Setup**: Upon boot, the device connects to WiFi and fetches session-specific metadata, including driver names, team colors, and the fastest lap holder.
* **Track Mapping**: It downloads track and pit lane layouts (X/Y coordinates). The software calculates the track's bounding box to automatically scale and center the circuit on the Cardputer's 240x135 display.
* **Real-Time Streaming**: Driver positions are streamed via **WebSockets**. This ensures low-latency updates compared to standard HTTP polling.

### 2. Physics & Rendering Engine

To provide a premium viewing experience on limited hardware, the project implements several clever software techniques:

* **Bake-and-Sprite System**: The track layout is "baked" into a background sprite to save CPU cycles. Only the dynamic elements (the cars) are redrawn every frame.
* **Predictive Smoothing**: Because telemetry data typically arrives in intervals (e.g., every 100ms), the code uses a linear interpolation "Physics Engine". It calculates steps between the current and target coordinates to ensure cars move smoothly across the screen rather than teleporting.
* **Memory Optimization**: The project is configured to utilize the Cardputer’s **PSRAM**, allowing it to handle larger race datasets without crashing the ESP32-S3.

### 3. User Interface

* **Live Map**: Displays the track and pit lane with colored circles representing each driver. The race leader is highlighted with an additional white ring.
* **Dynamic Sidebar**: Shows a real-time leaderboard (top 6 drivers) with their current positions and acronyms.
* **Race Stats**: Displays the current race clock and the driver currently holding the fastest lap.

## Technical Stack

* **Hardware**: M5Stack Cardputer (ESP32-S3).
* **Framework**: Arduino with PlatformIO.
* **Core Libraries**:
* `M5Cardputer`: Hardware abstraction and display management.
* `ArduinoJson`: Efficient parsing of race telemetry.
* `WebSocketsClient`: Real-time data streaming.



## License

This project is licensed under the **MIT License**.
