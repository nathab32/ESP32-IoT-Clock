# ESP32 IoT Clock

## About

Smart clock that has temperature/humidity sensor, LCD, RGB LEDs, 3 capacitive buttons, and a 7.5W USBC charging port. Connects to SinricPro for online functionality. The clock can then connect to Samsung Smartthings through SinricPro, allowing one of the capacitive buttons to control any device connected to your Smartthings account.

The clock should be powered by a wall adapter that can output 5V 3A as the 6x WS2812B LEDs will consume up to 1.8W on max brightness. Combined with the 7.5W charging port, the device can consume up to 9.3W excluding the ESP32's power draw.

### Known Issues

| Issue    | Cause   | Fix    |
|----------|---------|--------|
| Hotter DHT22 reading | Proximity to ESP32 and LEDs | Redesign enclosure with a wall between the sensor and other components, and connect the power to an IO port and only turn it on when necessary |
| Initial light parameters aren't sent to SinricPro | ? | ? |

## Parts

- 1x ESP-WROOM-32 Dev Board ![Pinout of board I used](https://www.upesy.com/cdn/shop/files/doc-esp32-pinout-reference-wroom-devkit.png?width=1038)
- 1x DHT22 Sensor ![DHT22](https://m.media-amazon.com/images/I/41Zf+0BP+DL._AC_SL1000_.jpg)
- 1x 128x64 I2C OLED ![OLED Screen](https://m.media-amazon.com/images/I/71H09-f27RL._AC_SL1500_.jpg)
- 2x Female USB-C Port ![USB-C Port](https://m.media-amazon.com/images/I/519XCsAIjML._AC_SL1001_.jpg)
- 2x 10cm long 30LEDs/m WS2812B strip (or any NeoPixel compatible LED strip)
- 3x 1cm diameter magnets
- Stranded wires
- 3D printed parts (1x Body and Cap, 4x Foot)
- 2x M3\*6, 4x M2\*4

## 3D Printing

STLs can be found in `/3D_Parts`, as well as an F3D file if modifications are necessary. I printed all parts in PLA with a 0.4mm nozzle with fuzzy skin turned on, but most filaments will work and fuzzy.

## SinricPro Setup

1. Create SinricPro account
2. When logged in and on the dashboard, click "Add a new Device"
3. Fill in the device name and description with "Switch", and make sure the Device Type is set to "Switch"
4. Leave everything as default and proceed until you reach the screen with the device ID
5. Once you reach that screen, go to devices on the sidebar and add a "Smart Light Bulb" and a "Temperature Sensor" device using the same method
6. On the Devices screen, there should now be a light, a switch, and a temperature sensor.

## Code

### Prerequisites

- PlatformIO extension in [VS Code](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
  - ESP32 Platform
  - Libraries
    - SinricPro
    - Adafruit NeoPixel
    - Adafruit SSD1306
    - Adafruit DHT22

### Sensitive.h

1. Download the repository and move it to a safe location on your computer. Open PlatformIO Home and select `Open Project`. Navigate to the folder directory and open it.
2. Open `/src/Sensitive.example.h` and rename it to `Sensitive.h`
3. Add your WiFi credentials to the arrays titled SSID and PASS. You may make the arrays longer if you need more spots.
4. In SinricPro, go to Credentials in the sidebar, then copy the default APP KEY and APP SECRET and fill them in in your `Sensitive.h` file.
5. Now go to Devices, and copy the device IDs and replace them in `Sensitive.h`.

### Test OTA Updates

1. Upload the program to your ESP32 via USB
2. Wait a few seconds for the ESP32 to boot up
3. Check the Modules section in SinricPro. If your ESP32 is online, the IP address should be listed, as well as the version number.
4. Back in VS Code, change the version number in /src/SmartClock.cpp at line 1.
5. Upload the new program OTA by running the command `pio run -t upload --upload-port ip_address`, replacing `ip_address` with the IP address of your ESP32.
6. Once the ESP32 has rebooted and reconnected to WiFi, the new version number you typed in should be reflected in SinricPro, indicating a successful update.

## Assembly

Assembly should be pretty self explanatory. Use double sided tape to secure the ESP32 to the center block in the enclosure. The DHT22 uses the M3 screws, and the OLED screen uses the M2s. The front 7.5W USB-C port needs to have a short between D+ and D- in order to output 7.5W.

The hardest part is wiring and soldering everything together since all of that is done inside of the enclosure. Use the images in `/images/Wiring` to help, as well as the pin numbers on line 30 of `/src/SmartClock.cpp`.

After I soldered everything together, I double checked everything to make sure nothing was wired wrong, then plugged in the back USB-C port while holding it down. Check to make sure everything works (apart from the touch buttons) through the SinricPro website, then secure the USB-C ports down. I used CA glue. Finally, I placed a piece of black electrical tape over my ESP32's power LED since it shone through the top cover of the clock.

## SmartClock.cpp

### Touch Thresholds (Line 36-38)

These values must be set correctly in order for the capacitive touch sensors to properly register inputs. To find the right values, proceed through the following steps.

1. Uncomment the debug block within the `handleScreen()` function and comment out everything within the brackets starting on line 230.
2. Upload the updated code. The OLED screen should now display the live values from the capacitive touch button.
3. For each of the three buttons, note the nominal value when the button is and isn't pressed.
4. Update the threshold for all three buttons by picking a value between the nominal pressed and unpressed values. Higher values will be more sensitive, and lower values are less sensitive.
5. Upload the new thresholds and test each button, adjusting thresholds as needed.

Once the thresholds are set, line 454 can be uncommented to enable the deep sleep function.

### Time Constants (Line 65)

These constants change what server to get the time from, the time zone, and whether or not to account for daylight savings time.

- `ntpServer` can likely be left as is. Search for NTP servers if you need to change it.
- `gmtOffset_sec` adjusts for the timezone you are in. Search up how many hours your timezone is off from GMT, then convert it to seconds and change the value.
- `daylightOffset_sec` will use daylight saving time if set to `3600`, and will not use it if set to `0`.
