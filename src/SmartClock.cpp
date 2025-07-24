#define FIRMWARE_VERSION "1.0.0"
#include <SinricPro.h>
#include "SinricProTemperaturesensor.h"
#include "SinricProSwitch.h"
#include "SinricProLight.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>

#include <Arduino.h>

#include <Adafruit_NeoPixel.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <DHT.h>

#include "WiFi.h"
#include <WiFiMulti.h>
#include "time.h"

#include "Sensitive.h"

//variables
#define BAUD_RATE 115200       // Change baudrate to your need (used for serial monitor)
#define EVENT_WAIT_TIME 60000  // send event every 60 seconds

//GPIO pins
#define DHT_PIN 26
#define LED_PIN 13
#define CAP_0 4   //for controlling external light
#define CAP_1 33  //for controlling mode of rgbs
#define CAP_2 32  //for turning on and off device
#define TOUCH_THRESHOLD_0 71
#define TOUCH_THRESHOLD_1 80
#define TOUCH_THRESHOLD_2 85

//LED params
const int led = 2;                 // ESP32 Pin to which onboard LED is connected
#define NUM_LEDS 6
Adafruit_NeoPixel leds(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
int brightness = 255;
bool powerStateLED = false;

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} ledColor;

enum Effect {
  OFF,
  SOLID,
  GLOWING,
  ALTERNATING,
  RAINBOW
};
Effect effect = OFF;

//oled display
Adafruit_SSD1306 display(128, 64, &Wire, -1);

//time constants
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -28800; //adjust to time zone (-28800 for PST)
const int daylightOffset_sec = 3600; //adjust for daylight savings time

/* Variable for reading pin status*/
bool switchState = true;
float temp;
float humidity;
float lastTemp;
float lastHumidity;
unsigned long lastEvent = 0;

//devices
DHT dht(DHT_PIN, DHT22);

WiFiMulti wifiMulti;

bool onPowerState(const String &deviceId, bool &state) {
  Serial.printf("Device %s turned %s (via SinricPro) \r\n", deviceId.c_str(), state ? "on" : "off");
  switchState = state;
  return true;  // request handled properly
}

bool onPowerStateLED(const String &deviceId, bool &state) {
  Serial.printf("Device %s turned %s (via SinricPro) \r\n", deviceId.c_str(), state ? "on" : "off");
  powerStateLED = state;
  if (powerStateLED == false) {
    leds.clear();
  } else {
    leds.setBrightness(brightness);
    Serial.print("brightness set to ");
    Serial.println(brightness);
  }
  leds.show();
  return true;  // request handled properly
}

bool onPowerStateTemp(const String &deviceId, bool &state) {
  return true;  // request handled properly
}

bool onBrightness(const String &deviceId, int &val) {
  brightness = map(val, 0, 100, 0, 255);
  // leds.setBrightness(brightness);
  // leds.show();
  return true;
}

bool onAdjustBrightness(const String &deviceId, int brightnessDelta) {
  int percent = map(brightness, 0, 255, 0, 100) + brightnessDelta;
  brightness = map(constrain(percent, 0, 100), 0, 100, 0, 255);
  // leds.setBrightness(map(brightness, 0, 100, 0, 255));
  // leds.show();
  return true;
}

void setColorWithGamma(int red, int green, int blue) {
  // Apply gamma correction to each channel
  ledColor.r = leds.gamma8(red);
  ledColor.g = leds.gamma8(green);
  ledColor.b = leds.gamma8(blue);
}

bool onColor(const String &deviceId, byte &r, byte &g, byte &b) {
  setColorWithGamma(r, g, b);
  // ledColor.r = r;
  // ledColor.g = g;
  // ledColor.b = b;
  return true;
}

Color colorToRGB(int temp) {
  temp /= 100;
  int red;
  if (temp <= 66) red = 255;
  else {
    red = temp - 60;
    red = 329.698727446 * pow(red, -0.1332047592);
    if (red < 0) red = 0;
    if (red > 255) red = 255;
  }

  int green;
  if (temp <= 66) {
    green = 99.4708025861 * log(temp) - 161.1195681661;
  } else {
    green = temp - 60;
    green = 288.1221695283 * pow(green, -0.0755148492);
  }
  if (green < 0) green = 0;
  if (green > 255) green = 255;

  int blue;
  if (temp >= 66) blue = 255;
  else {
    if (temp <= 19) blue = 0;
    else {
      blue = temp - 10;
      blue = 138.5177312231 * log(blue) - 305.0447927307;
      if (blue < 0) blue = 0;
      if (blue > 255) blue = 255;
    }
  }
  setColorWithGamma(red, green, blue);
  return ledColor;
}

bool onColorTemperature(const String &deviceId, int &colorTemperature) {
  colorToRGB(colorTemperature);  // set rgb values from corresponding colortemperauture
  return true;
}

void handleTemperatureSensor() {
  unsigned long actualMillis = millis();
  if (actualMillis - lastEvent < EVENT_WAIT_TIME) return;  //only check every EVENT_WAIT_TIME milliseconds

  temp = dht.readTemperature(true);  // get actual temperature in °F
  humidity = dht.readHumidity();     // get actual humidity

  if (isnan(temp) || isnan(humidity)) {        // reading failed...
    Serial.printf("DHT reading failed!\r\n");  // print error message
    return;                                    // try again next time
  } else {
    Serial.printf("Temp: %2.1f, Humidity: %2.1f%%\r\n", temp, humidity);
  }

  if (temp == lastTemp && humidity == lastHumidity) return;  // if no values changed do nothing...

  SinricProTemperaturesensor &mySensor = SinricPro[TEMP_ID];     // get temperaturesensor device
  bool success = mySensor.sendTemperatureEvent(temp, humidity);  // send event

  // Retry if event fails
  int retries = 0;
  while (!success && retries < 3) {  // Retry up to 3 times
    Serial.printf("Retrying to send event...\r\n");
    success = mySensor.sendTemperatureEvent(temp, humidity);
    retries++;
    delay(500);  // Delay for retry
  }

  if (success) {  // if event was sent successfuly, print temperature and humidity to serial
    Serial.printf("Temperature: %2.1f Celsius\tHumidity: %2.1f%%\r\n", temp, humidity);
  } else {  // if sending event failed, print error message
    Serial.printf("Something went wrong...could not send temp to server!\r\n");
  }

  lastTemp = temp;           // save actual temperature for next compare
  lastHumidity = humidity;   // save actual humidity for next compare
  lastEvent = actualMillis;  // save actual time for next compare
}

void handleScreen() {
  display.setCursor(0, 0);
  display.setTextSize(1);

  // debug block
  // display.print("CAP_0:");
  // display.println(touchRead(CAP_0));
  // display.print("CAP_1:");
  // display.println(touchRead(CAP_1));
  // display.print("CAP_2:");
  // display.println(touchRead(CAP_2));
  // display.print(brightness);

  //temperature
  display.print(temp, 1);
  display.write(0xF7);
  display.print("F");

  //version number
  display.setCursor(49, 0);
  String version = "v" + String(FIRMWARE_VERSION);
  display.print(version);

  //humidity
  display.setCursor(98, 0);
  display.print(humidity, 1);
  display.print("%");

  //line between temp/humid and time
  display.drawFastHLine(0, 10, 127, SSD1306_WHITE);

  //time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time");
  } else {
    display.setCursor(4, 16);
    display.setTextSize(2);
    display.print(&timeinfo, "%a,%b %d");

    display.setCursor(14, 40);
    display.println(&timeinfo, "%T");
  }


  display.display();
  display.clearDisplay();
}

unsigned long previousPress0;
unsigned long previousPress1;

void handleTouchSensors() {
  //button1, 4
  if (touchRead(CAP_0) < TOUCH_THRESHOLD_0 && millis() - previousPress0 > 500) {
    switchState = !switchState;
    // get Switch device back
    SinricProSwitch &mySwitch = SinricPro[SWITCH_ID];
    // send powerstate event
    bool success = mySwitch.sendPowerStateEvent(switchState);  // send the new powerState to SinricPro server
    if (!success) {
      Serial.printf("Something went wrong...could not send switch state to server!\r\n");
    }
    Serial.printf("Device %s turned %s (manually via flashbutton)\r\n", mySwitch.getDeviceId().c_str(), switchState ? "on" : "off");
    previousPress0 = millis();  // update last button press variable
  }


  //button2, 32, led control
  if (touchRead(CAP_1) < TOUCH_THRESHOLD_1 && millis() - previousPress1 > 500) {
    effect = static_cast<Effect>(((int)effect + 1) % 5);

    if (effect == OFF) {
      powerStateLED = false;
      SinricProLight &myLight = SinricPro[LIGHT_ID];
      myLight.sendPowerStateEvent(powerStateLED);
    }
    else if (effect == SOLID) {
      powerStateLED = true;
      SinricProLight &myLight = SinricPro[LIGHT_ID];
      myLight.sendPowerStateEvent(powerStateLED);
    }
    previousPress1 = millis();  // update last button press variable
  }

  //button3, 33, on off
  if (touchRead(CAP_2) < TOUCH_THRESHOLD_2) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("Sleeping...", 0, 0, &x1, &y1, &w, &h);
    int16_t x = (display.width() - w) / 2;
    int16_t y = (display.height() - h) / 2;
    display.setCursor(x, y);
    display.print("Sleeping...");
    display.display();
    delay(750);
    esp_deep_sleep_start();
  }
}

// setup function for WiFi connection
void setupWiFi() {
  Serial.printf("\r\n[Wifi]: Connecting");

  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);

  for (int i = 0; i < (sizeof(SSID)/sizeof(SSID[0])); i++) {
    wifiMulti.addAP(SSID[i].c_str(), PASS[i].c_str());
  }


  Serial.println("Connecting Wifi...");
  if (wifiMulti.run() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    // digitalWrite(led, HIGH);  // turn on led to indicate successful connection
  }

}

// setup function for SinricPro
void setupSinricPro() {
  // add device to SinricPro
  SinricProTemperaturesensor &mySensor = SinricPro[TEMP_ID];

  SinricProSwitch &mySwitch = SinricPro[SWITCH_ID];
  mySwitch.onPowerState(onPowerState);

  SinricProLight &myLight = SinricPro[LIGHT_ID];
  myLight.onPowerState(onPowerStateLED);
  myLight.onBrightness(onBrightness);
  myLight.onAdjustBrightness(onAdjustBrightness);
  myLight.onColor(onColor);
  myLight.onColorTemperature(onColorTemperature);

  // setup SinricPro
  SinricPro.onConnected([]() {
    Serial.printf("Connected to SinricPro\r\n");
  });
  SinricPro.onDisconnected([]() {
    Serial.printf("Disconnected from SinricPro\r\n");
  });
  
  SinricPro.begin(APP_KEY, APP_SECRET);
}

void setupDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // digitalWrite(led, HIGH);
  }
  display.setCursor(0, 0);
  display.setTextColor(WHITE, BLACK);
  display.clearDisplay();
  display.display();
}

void setupLED() {
  ledColor.r = 255;
  ledColor.g = 255;
  ledColor.b = 255;

  leds.begin();
  leds.clear();
  leds.show();
  leds.setBrightness(brightness);

  SinricProLight &myLight = SinricPro[LIGHT_ID];
  myLight.sendColorEvent(ledColor.r, ledColor.g, ledColor.b);
  myLight.sendBrightnessEvent(brightness);
  myLight.sendPowerStateEvent(powerStateLED);
}

void handleLED() {
  if (powerStateLED == false) {
    leds.clear();
    leds.show();
    return;
  }

  int time = millis();

  switch (effect)
  {
    case SOLID:
      {
        leds.fill(leds.Color(ledColor.r, ledColor.g, ledColor.b));
        leds.setBrightness(brightness);
      }
      break;
    case GLOWING:
      {
        float glow = fabs(sin(time / 6000.0 * 2 * 3.14159));
        int mappedBrightness = map((int)(glow * 1000), 0, 1000, 0, brightness);
        leds.setBrightness(mappedBrightness);
        leds.fill(leds.Color(ledColor.r, ledColor.g, ledColor.b));
      } 
      break;
    case ALTERNATING:
      {
        for (int i = leds.numPixels(); i > 0; i--) {
          if (time % 1000 < 500) {
            if (i % 2 == 0) leds.setPixelColor(i-1, leds.Color(ledColor.r, ledColor.g, ledColor.b));
            else leds.setPixelColor(i-1, 0, 0, 0);
          }
          else {
            if (i % 2 == 0) leds.setPixelColor(i-1, 0, 0, 0);
            else leds.setPixelColor(i-1, leds.Color(ledColor.r, ledColor.g, ledColor.b));
          }
        }
      }
      break;
    case RAINBOW:
      {
        // leds.setBrightness(brightness);
        leds.rainbow(map(time % 2000, 0, 1999, 0, 65535), 1, ledColor.r, brightness, true);
      }
      break;
    
    }
  
  leds.show();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(BAUD_RATE);
  pinMode(led, OUTPUT);
  setupWiFi();
  touchSleepWakeUpEnable(CAP_2, TOUCH_THRESHOLD_2);

  //DO NOT CHANGE
  {
    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else  // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

    ArduinoOTA.begin();
    for (int i = 0; i < 5000; i += 100) {
      ArduinoOTA.handle();
      delay(100);
    } 
  }

  setupSinricPro();

  dht.begin();

  setupDisplay();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  setupLED();
}


void loop() {
  // put your main code here, to run repeatedly:
  SinricPro.handle();
  ArduinoOTA.handle();
  handleTemperatureSensor();
  handleTouchSensors();
  handleScreen();
  handleLED();

  if (wifiMulti.run() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
  }

  Serial.println(FIRMWARE_VERSION);
}