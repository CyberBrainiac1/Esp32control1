# ESP32 Control Setup

Public app URL:

- https://cyberbrainiac1.github.io/Esp32control1/

What to upload to the ESP32:

- `ESP32ServoWiFi.ino`

What this version does:

- The ESP32 automatically creates its own Wi-Fi hotspot named `ESP32-Control` when no home Wi-Fi is configured.
- The web app defaults to `http://192.168.4.1`, so you do not need to type an IP address.
- The web app has an **Auto connect to ESP32** button that tries the hotspot address and `http://esp32-control.local`.
- The ESP32 also advertises Bluetooth as `ESP32-Control-BLE` for browsers that support Web Bluetooth.
- The web app has a **Make this an iPhone app** button that shows the Home Screen steps.

Fast iPhone Wi-Fi setup:

1. Upload `ESP32ServoWiFi.ino` to the ESP32.
2. Open iPhone **Settings** -> **Wi-Fi**.
3. Join `ESP32-Control`.
4. Password: `control123`.
5. Open `https://cyberbrainiac1.github.io/Esp32control1/`.
6. Tap **Auto connect to ESP32**.

Optional home Wi-Fi setup:

1. Open `ESP32ServoWiFi.ino`.
2. Replace `YOUR_WIFI_NAME` and `YOUR_WIFI_PASSWORD` with your normal Wi-Fi credentials.
3. Install the `ESP32Servo` library in Arduino IDE.
4. Select your ESP32 board and upload the sketch.
5. In the app, tap **Home Wi‑Fi `.local`** or **Auto connect to ESP32**.

Servo wiring:

- Signal -> `GPIO 18`
- Power -> external `5V`
- Ground -> shared ground between servo supply and ESP32

Wi-Fi API used by the app:

- `GET /api/status`
- `POST /api/servo` with `{"angle": 90}`

Bluetooth mode:

- ESP32 name: `ESP32-Control-BLE`
- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Write characteristic UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Send a text angle like `90` to move the servo.

Important iPhone note:

- iPhone Safari does not currently support Web Bluetooth for websites or Home Screen web apps, so use Wi-Fi on iPhone.
- Websites cannot automatically add themselves to an iPhone Home Screen. Open the app in Safari, tap **Share**, tap **Add to Home Screen**, then tap **Add**.
