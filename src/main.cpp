// =--------------------------------------------------------------------------------= Libraries =--=

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <AutoConnect.h>
#include "main.h"


// =----------------------------------------------------------------------------------= Globals =--=

// Base Web Server
ESP8266WebServer Server;

// Captive Portal
AutoConnect Portal(Server);
AutoConnectConfig Config;
AutoConnectAux TimezoneContainer;
bool wifiFeaturesEnabled = false;

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
Timezone_t currentTZ = TZ_LIST[0];
bool initialTimeSync = false;

// Display Layout and Pixels
struct ledSescriptor_t descriptors[] = {
  {  0, 2, 3},
  { 42, 2, 3},
  { 86, 2, 3}, // skip two for colon pixels
  {128, 2, 3}
};

static const uint8_t colon1 = 84;  // the address of the first colon LED
static const uint8_t colon2 = 85;  // the address of the second colon LED

// Display
static Adafruit_NeoPixel LEDstrip(NUM_LEDS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// Colors
static const uint32_t colorBlack = Adafruit_NeoPixel::Color(0, 0, 0);
static const uint32_t colorRed = Adafruit_NeoPixel::Color(LUMINANCE, 0, 0); 
static const uint32_t colorGreen = Adafruit_NeoPixel::Color(LUMINANCE/3, LUMINANCE, 0);
static const uint32_t colorYellow = getPixelColorHsv(213, 255, LUMINANCE);

// max hue = 256*6
static const uint32_t colorHour = colorYellow;
static const uint32_t colorColon = getPixelColorHsv(853, 255, LUMINANCE);
static const uint32_t colorMinute = colorYellow;

// OTA
BearSSL::PublicKey signPubKey(OTA_PUBKEY);
BearSSL::HashSHA256 hash;
BearSSL::SigningVerifier sign(&signPubKey);
bool otaInProgress = false;

// =--------------------------------------------------------------------------= WiFi and Portal =--=

void setupPortal() {
  // Enable saved past credential by autoReconnect option, even once it is disconnected.
  Config.autoReconnect = true;
  Config.apid = "big-clock-" + String(ESP.getChipId(), HEX);
  Config.psk  = "ilikeclocks";
  Portal.config(Config);

  // Load aux. page
  TimezoneContainer.load(PORTAL_TIMEZONE_PAGE);

  // Retrieve the select element that holds the time zone code and
  // register the zone mnemonic in advance.
  AutoConnectSelect& tzSelector = TimezoneContainer["timezone"].as<AutoConnectSelect>();
  for (uint8_t index = 0; index < sizeof(TZ_LIST) / sizeof(Timezone_t); index++) {
    tzSelector.add(String(TZ_LIST[index].name));
  }
  tzSelector.select(String(currentTZ.name));

  Portal.join({ TimezoneContainer });        // Register aux. page

  // Behavior a root path of ESP8266WebServer.
  Server.on("/", portalRootPage);
  Server.on("/start", portalStartPage);   // Set NTP server trigger handler

  // Set display to show state
  Portal.whileCaptivePortal(loopCaptivePortal);
  Portal.onDetect(startCaptivePortal);
  Portal.onConnect(onWifiConnect);

  // Establish a connection with an autoReconnect option.
  writeAllDigits(CHAR_DASH, colorColon);

  // Fire up the network connection and portal with metrics
  unsigned long start = millis();
  Serial.println("Starting Portal.begin");

  if (Portal.begin()) {
    Serial.printf("Portal.begin complete in %ld\n", millis() - start);
    // Serial.println("WiFi connected: " + WiFi.localIP().toString());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
    }
  }

  Serial.printf("Portal setup complete in %ld\n", millis() - start);
}

void loopPortal() {
  Portal.handleClient();
  MDNS.update();
}

void portalRootPage() {
  String  content =
    "<html>"
    "<head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<script type=\"text/javascript\">setTimeout(\"location.reload()\", 10000);</script>"
    "</head>"
    "<body>"
    "<h2 align=\"center\" style=\"color:black;margin:20px;\">Big Clock</h2>"
    "<h3 align=\"center\" style=\"color:gray;margin:10px;\">{{DateTime}}</h3>"
    "<p style=\"text-align:center;\">Reload the page to update the time.</p>"
    "<p></p><p style=\"padding-top:15px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
    "</body>"
    "</html>";

  char dateTime[32];

  if (initialTimeSync) {
    time_t t = currentTZ.timezone.toLocal(now());
    sprintf(dateTime, "%02d:%02d:%02d, %s", hour(t), minute(t), second(t), currentTZ.name);
  } else {
    sprintf(dateTime, "Waiting for NTP sync");
  }

  content.replace("{{DateTime}}", String(dateTime));
  Server.send(200, "text/html", content);
}

void portalStartPage() {
  // Retrieve the value of AutoConnectElement with arg function of WebServer class.
  // Values are accessible with the element name.
  String selectedTimezone = Server.arg("timezone");
  AutoConnectSelect& tzSelector = TimezoneContainer["timezone"].as<AutoConnectSelect>();
  tzSelector.select(selectedTimezone);

  for (uint8_t index = 0; index < sizeof(TZ_LIST) / sizeof(Timezone_t); index++) {
    String tzName = String(TZ_LIST[index].name);
    if (selectedTimezone.equalsIgnoreCase(tzName)) {
      currentTZ = TZ_LIST[index];
      Serial.println("Selected time Zone: " + String(TZ_LIST[index].name));
      saveConfig(tzName);
      break;
    }
  }

  // The /start page just constitutes timezone,
  // it redirects to the root page without the content response.
  Server.sendHeader("Location", String("http://") + Server.client().localIP().toString() + String("/"));
  Server.send(302, "text/plain", "");
  Server.client().flush();
  Server.client().stop();
}

bool loopCaptivePortal(void) {
  static unsigned long updateTimer = millis();
  static bool tick = true;

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - updateTimer > CAPTIVE_PORTAL_BLINK_MS) {
      updateTimer = millis();
      uint32_t color = tick ? colorRed : colorBlack;
      writeAllDigits(CHAR_DASH, color);
      tick = !tick;
    }
  }
  return true;
}

bool startCaptivePortal(IPAddress& ip) {
  Serial.println("Portal started, IP: " + WiFi.localIP().toString());
  writeAllDigits(CHAR_DASH, colorRed);

  return true;
}

void onWifiConnect(IPAddress& ipaddr) {
  Serial.printf("WiiFi connected to %s, IP: %s\n", WiFi.SSID().c_str(), ipaddr.toString().c_str());
  writeAllDigits(CHAR_DASH, colorGreen);

  if (WiFi.getMode() & WIFI_AP) {
    WiFi.softAPdisconnect(true);
    WiFi.enableAP(false);
    Serial.printf("SoftAP: %s shut down\n", WiFi.softAPSSID().c_str());
  }
}


// =----------------------------------------------------------------------------= NTP and Clock =--=

void setupClock() {
  timeClient.begin();
  syncLocalClock();
}

void loopClock() {
  static unsigned long updateTimer = 0;

  // If the initial time sync failed, keep trying every few seconds until it succeeds
  if (millis() - updateTimer > (initialTimeSync ? NTP_UPDATE_MS : NTP_RETRY_MS)) {
    updateTimer = millis();
    syncLocalClock();
  }
}

void syncLocalClock() {
  time_t before = now();

  if (timeClient.update()) {

    unsigned long epoch = timeClient.getEpochTime();
    setTime(epoch);

    time_t t = currentTZ.timezone.toLocal(epoch);
    if (initialTimeSync) {
      // Update over time
      Serial.printf(
        "Adjust local clock. Offset is %ld, set to %02d:%02d:%02d\n",
        (long) epoch - (long) before, hour(t), minute(t), second(t)
      );
    } else {
      initialTimeSync = true;
      Serial.printf(
        "Initial time sync. Setting clock to %02d:%02d:%02d\n",
        hour(t), minute(t), second(t)
      );
    }
  } else {
    Serial.println(F("NTP Update Failed"));
  }
}

// =--------------------------------------------------------------------= Seven Segment Display =--=

void setupDisplay() {
  LEDstrip.begin();
  LEDstrip.show(); // No colors set yet.  So this will set all pixels to 'off'
}

void loopDisplay() {
  static unsigned long updateTimer = millis();

  if (millis() - updateTimer > CLOCK_UPDATE_MS) {
    updateTimer = millis();

    if (initialTimeSync) {
      time_t t = currentTZ.timezone.toLocal(now());

      writeDigit(minute(t)%10, 0, colorMinute);
      writeDigit(minute(t)/10, 1, colorMinute);
      writeDigit(hour(t)%10, 2, colorHour);
      writeDigit(hour(t)/10, 3, colorHour);

      if (second(t) % 2) {
        LEDstrip.setPixelColor(colon1, colorColon);
        LEDstrip.setPixelColor(colon2, colorColon);
      } else {
        LEDstrip.setPixelColor(colon1, colorBlack);
        LEDstrip.setPixelColor(colon2, colorBlack);
      }

      LEDstrip.show();  // Flush the settings to the LEDs
    }
  }
}

void clearDisplay() {
  LEDstrip.clear();
  LEDstrip.show();
}

/**
 * @brief Use the digits to create a progress bar that snakes along the display
 *
 * Serpentine layout starting at upper-left (index 3) across the top, down one segment, back along
 * the middle, down to the bottom and across to the right again.
 */
void writeProgressBar(uint8_t percentage, uint32_t color) {
  LEDstrip.clear();

  uint8_t totalBars = sizeof(progressSegmentMap)/sizeof(progressSegmentMap[0]);
  uint8_t numBars = (float)percentage / (100.0 / (float)(totalBars / 2));
  for (uint8_t bar = 0; bar < numBars; bar++) {
    writeSegment(progressSegmentMap[bar * 2], progressSegmentMap[bar * 2 + 1], color);
  }

  LEDstrip.show();
}

/**
 * Write the same character to every digit
 */
void writeAllDigits(uint8_t character, uint32_t color) {
  int numDigits = sizeof(descriptors) / sizeof(descriptors[0]);
  for (uint8_t digit = 0; digit < numDigits; digit++) {
    writeDigit(character, digit, color);
  }
  LEDstrip.show();
}

/**
 * @brief Set the character for a digit.
 *
 * @param character The hex digit value to value to display.
 * @param place The place value (position) to display the digit.
 * @param color The color of the segment to use.
 */
void writeDigit(uint8_t character, uint16_t place, uint32_t color) {
  uint8_t segmentMap = font[character];
  
  for(uint8_t segment = 0; segment < 7; ++segment) {
    writeSegment(place, segment, segmentMap & 0x01 ? color : colorBlack);
    segmentMap >>= 1;
  }
}

/**
 * @brief A utility function to light an individual segment of one digit
 *
 * @param place The digit to control.
 * @param segment The segment of the digit to light.
 * @param color The color value to which the LEDs will be set.
 */
void writeSegment(uint16_t place, uint8_t segment, uint32_t color) {
  uint16_t startingLed = descriptors[place].startingLedNumber;
  uint8_t ledsPerStrip = descriptors[place].ledsPerStrip;
  uint8_t numStrips = descriptors[place].numStrips;

  for (uint8_t strip = 0; strip < numStrips; ++strip) {
    uint16_t stripNum = strip % 2 ? (strip + 1) * 7 - 1 - segment : strip * 7 + segment;
    uint16_t start = startingLed + stripNum * ledsPerStrip;
    uint16_t last = start + ledsPerStrip;
    
    while (start < last) {
      LEDstrip.setPixelColor(start, color);
      ++start;
    }
  }
}

/**
 * @brief Convert an HSV color to a Neopixel compatible RGB color
 *
 * @param h The hue portion of the color
 * @param s The saturation portion of the color
 * @param v The value (brightness) portion of the color
 */
uint32_t getPixelColorHsv(uint16_t h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;

  if (!s) {
    // Monochromatic, all components are V
    r = g = b = v;
  } else {
    uint8_t sextant = h >> 8;
    if (sextant > 5)
      sextant = 5;  // Limit hue sextants to defined space

    g = v; // Top level

    // Perform actual calculations

    /*
       Bottom level:
       --> (v * (255 - s) + error_corr + 1) / 256
    */
    uint16_t ww; // Intermediate result
    ww = v * (uint8_t)(~s);
    ww += 1;       // Error correction
    ww += ww >> 8; // Error correction
    b = ww >> 8;

    uint8_t h_fraction = h & 0xff; // Position within sextant
    uint32_t d; // Intermediate result

    if (!(sextant & 1)) {
      // r = ...slope_up...
      // --> r = (v * ((255 << 8) - s * (256 - h)) + error_corr1 + error_corr2) / 65536
      d = v * (uint32_t)(0xff00 - (uint16_t)(s * (256 - h_fraction)));
      d += d >> 8;  // Error correction
      d += v;       // Error correction
      r = d >> 16;
    } else {
      // r = ...slope_down...
      // --> r = (v * ((255 << 8) - s * h) + error_corr1 + error_corr2) / 65536
      d = v * (uint32_t)(0xff00 - (uint16_t)(s * h_fraction));
      d += d >> 8;  // Error correction
      d += v;       // Error correction
      r = d >> 16;
    }

    // Swap RGB values according to sextant. This is done in reverse order with
    // respect to the original because the swaps are done after the
    // assignments.
    if (!(sextant & 6)) {
      if (!(sextant & 1)) {
        uint8_t tmp = r;
        r = g;
        g = tmp;
      }
    } else {
      if (sextant & 1) {
        uint8_t tmp = r;
        r = g;
        g = tmp;
      }
    }
    if (sextant & 4) {
      uint8_t tmp = g;
      g = b;
      b = tmp;
    }
    if (sextant & 2) {
      uint8_t tmp = r;
      r = b;
      b = tmp;
    }
  }
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}


// =-------------------------------------------------------------------------------= Filesystem =--=

void setupFilesystem() {
  loadConfig();
}

void loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println(F("Failed to mount FS"));
    return;
  }

  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println(F("Config file doesn't exist."));
    return;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "r");

  if (!configFile) {
    Serial.println(F("Failed to open config file for reading"));
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, configFile);

  if (error) {
    Serial.println(F("Failed to read config file"));
  }

  String tz = doc["timezone"] | String(TZ_LIST[0].name);

  for (uint8_t n = 0; n < sizeof(TZ_LIST) / sizeof(Timezone_t); n++) {
    String tzName = String(TZ_LIST[n].name);
    if (tz.equalsIgnoreCase(tzName)) {
      currentTZ = TZ_LIST[n];
      Serial.println("Loaded time zone: " + tz);
      break;
    }
  }

  configFile.close();
  LittleFS.end();
}

void saveConfig(String timezone) {
  if (!LittleFS.begin()) {
    Serial.println(F("Failed to mount FS"));
    return;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "w");

  if (!configFile) {
    Serial.println(F("Failed to open config file for writing"));
    return;
  }

  StaticJsonDocument<512> doc;
  doc["timezone"] = timezone;

  if (serializeJson(doc, configFile) == 0) {
    Serial.println(("Dailed to write to file"));
  }

  Serial.println("Saved time zone: " + timezone);

  configFile.close();
  LittleFS.end();
}


// =------------------------------------------------------------------------------= OTA Updates =--=

void setupOTA() {
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(MDNS_HOSTNAME);

  // Update.installSignature( &hash, &sign );
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    otaInProgress = true;
    clearDisplay();

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("OTA: Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    Serial.println("\nOTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t percent = (progress / (total / 100));
    writeProgressBar(percent, colorYellow);

    Serial.printf("OTA Progress: %u%%\r", percent);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA: Error[%u]: ", error);
    otaInProgress = false;

    if (error == OTA_AUTH_ERROR) {
      Serial.println("OTA: Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("OTA: Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("OTA: Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("OTA: Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("OTA: End Failed");
      delay(2000);
      ESP.restart(); // NTP seems to fail after OTA failures, reboot
    }
  });

  Serial.println("OTA Setup");
  ArduinoOTA.begin();
}

void loopOTA() {
  ArduinoOTA.handle();
}

// =---------------------------------------------------------------------------= Setup and Loop =--=

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(""); // ESP8266 spits gibberish on reset, push actual output down

  // Change Watchdog Timer to longer wait
  ESP.wdtDisable();
  ESP.wdtEnable(WDTO_8S);

  setupFilesystem();
  setupDisplay();
  setupPortal();
  setupOTA();
  setupClock();
}

void loop() {
  loopPortal();
  loopOTA();
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaInProgress) loopDisplay();
    loopClock();
  }
}
