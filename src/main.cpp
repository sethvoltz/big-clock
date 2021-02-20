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

// Display
CRGB leds[NUM_LEDS];

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
      CRGB color = tick ? CRGB::Red : CRGB::Black;
      writeAllDigits(CHAR_DASH, color);
      tick = !tick;
    }
  }
  return true;
}

bool startCaptivePortal(IPAddress& ip) {
  Serial.println("Portal started, IP: " + WiFi.localIP().toString());
  writeAllDigits(CHAR_DASH, CRGB::Red);

  return true;
}

void onWifiConnect(IPAddress& ipaddr) {
  Serial.printf("WiiFi connected to %s, IP: %s\n", WiFi.SSID().c_str(), ipaddr.toString().c_str());
  writeAllDigits(CHAR_DASH, CRGB::Green);

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
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(LUMINANCE);
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
        leds[colon1] = colorColon;
        leds[colon2] = colorColon;
      } else {
        leds[colon1] = CRGB::Black;
        leds[colon2] = CRGB::Black;
      }

      FastLED.show();  // Flush the settings to the LEDs
    }
  }
}

void clearDisplay() {
  FastLED.clear();
  FastLED.show();
}

/**
 * @brief Use the digits to create a progress bar that snakes along the display
 *
 * Serpentine layout starting at upper-left (index 3) across the top, down one segment, back along
 * the middle, down to the bottom and across to the right again.
 */
void writeProgressBar(uint8_t percentage, CRGB color) {
  FastLED.clear();

  uint8_t totalBars = sizeof(progressSegmentMap)/sizeof(progressSegmentMap[0]);
  uint8_t numBars = (float)percentage / (100.0 / (float)(totalBars / 2));
  for (uint8_t bar = 0; bar < numBars; bar++) {
    writeSegment(progressSegmentMap[bar * 2], progressSegmentMap[bar * 2 + 1], color);
  }

  FastLED.show();
}

/**
 * Write the same character to every digit
 */
void writeAllDigits(uint8_t character, CRGB color) {
  int numDigits = sizeof(descriptors) / sizeof(descriptors[0]);
  for (uint8_t digit = 0; digit < numDigits; digit++) {
    writeDigit(character, digit, color);
  }
  FastLED.show();
}

/**
 * @brief Set the character for a digit.
 *
 * @param character The hex digit value to value to display.
 * @param place The place value (position) to display the digit.
 * @param color The color of the segment to use.
 */
void writeDigit(uint8_t character, uint16_t place, CRGB color) {
  uint8_t segmentMap = font[character];
  
  for(uint8_t segment = 0; segment < 7; ++segment) {
    writeSegment(place, segment, segmentMap & 0x01 ? color : CRGB::Black);
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
void writeSegment(uint16_t place, uint8_t segment, CRGB color) {
  uint16_t startingLed = descriptors[place].startingLedNumber;
  uint8_t ledsPerStrip = descriptors[place].ledsPerStrip;
  uint8_t numStrips = descriptors[place].numStrips;

  for (uint8_t strip = 0; strip < numStrips; ++strip) {
    uint16_t stripNum = strip % 2 ? (strip + 1) * 7 - 1 - segment : strip * 7 + segment;
    uint16_t start = startingLed + stripNum * ledsPerStrip;
    uint16_t last = start + ledsPerStrip;
    
    while (start < last) {
      leds[start] = color;
      ++start;
    }
  }
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
