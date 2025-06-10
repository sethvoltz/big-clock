#include "main.h"


// =----------------------------------------------------------------------------------= Globals =--=

// Program
uint8_t currentProgram = 0;

// Base Web Server
ESP8266WebServer Server;

// Captive Portal
AutoConnect Portal(Server);
AutoConnectConfig Config;
AutoConnectAux ConfigureContainer;
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

// Palettes
CRGBPalette16 currentPalette;


// =--------------------------------------------------------------------------= WiFi and Portal =--=

void setupPortal() {
  // Enable saved past credential by autoReconnect option, even once it is disconnected.
  Config.autoReconnect = true;
  Config.apid = "big-clock-" + String(ESP.getChipId(), HEX);
  Config.psk  = "ilikeclocks";
  Portal.config(Config);

  // Load aux. page
  ConfigureContainer.load(PORTAL_CONFIGURE_PAGE);

  // Fill the time zone selector from config and pre-select any saved value
  AutoConnectSelect& timezoneSelector = ConfigureContainer["timezone"].as<AutoConnectSelect>();
  for (uint8_t index = 0; index < sizeof(TZ_LIST) / sizeof(Timezone_t); index++) {
    timezoneSelector.add(String(TZ_LIST[index].name));
  }
  timezoneSelector.select(String(currentTZ.name));

  // Fill the program selector from config and pre-select from runtime value
  AutoConnectSelect& programSelector = ConfigureContainer["program"].as<AutoConnectSelect>();
  for (uint8_t index = 0; index < PROGRAM_COUNT; index++) {
    programSelector.add(String(programNames[index]));
  }
  programSelector.select(String(programNames[currentProgram]));

  Portal.join({ ConfigureContainer });        // Register aux. page

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
  AutoConnectSelect& timezoneSelector = ConfigureContainer["timezone"].as<AutoConnectSelect>();
  timezoneSelector.select(selectedTimezone);

  for (uint8_t index = 0; index < sizeof(TZ_LIST) / sizeof(Timezone_t); index++) {
    String tzName = String(TZ_LIST[index].name);
    if (selectedTimezone.equalsIgnoreCase(tzName)) {
      currentTZ = TZ_LIST[index];
      Serial.println("Selected time Zone: " + String(TZ_LIST[index].name));
      saveConfig(tzName);
      break;
    }
  }

  String selectedProgram = Server.arg("program");
  AutoConnectSelect& programSelector = ConfigureContainer["program"].as<AutoConnectSelect>();
  programSelector.select(selectedProgram);

  for (size_t program = 0; program < PROGRAM_COUNT; program++) {
    if (selectedProgram.equals(programNames[program])) {
      setProgram(program);
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

// =----------------------------------------------------------------------------------= Display =--=

void clearDisplay() {
  FastLED.clear();
  FastLED.show();
}

void setupDisplay() {
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(LUMINANCE);
  clearDisplay();
}

void loopDisplay(bool first = false) {
  (*renderFunc[currentProgram])(first);
}

void setProgram(uint8_t program) {
  if (program >= 0 && program < PROGRAM_COUNT && program != currentProgram) {
    currentProgram = program;
    Serial.printf("Setting program to %s\n", programNames[program]);
    loopDisplay(true);
  }
}

void surpriseAndDelight() {
  static unsigned long updateTimer = millis();

  if (millis() - updateTimer > 1000) {
    updateTimer = millis();

    time_t t = currentTZ.timezone.toLocal(now());
    if (minute(t) == 0) {
      // Top o' the hour, let's throw an animation in for a few seconds
      if (currentProgram == 0 && second(t) < 10) {
        // We're on the clock, switch to a random one
        setProgram(random(1, PROGRAM_COUNT - 1));
      } else if (second(t) > 10) {
        // Time's up, go back to clock
        setProgram(0);
      }
    }
  }
}

/**
 * @brief Use the digits to create a progress bar that snakes along the display
 *
 * Serpentine layout starting at upper-left (index 3) across the top, down one segment, back along
 * the middle, down to the bottom and across to the right again.
 */
void writeProgressBar(uint8_t percentage, CRGB color) {
  static uint8_t lastPercent = 255; // impossible value to force first update
  if (percentage == lastPercent) return; // Only update if percent changed
  lastPercent = percentage;

  FastLED.clear();

  uint8_t totalBars = sizeof(progressSegmentMap)/sizeof(progressSegmentMap[0]);
  uint8_t numBars = (float)percentage / (100.0 / (float)(totalBars / 2));
  for (uint8_t bar = 0; bar < numBars; bar++) {
    writeSegment(progressSegmentMap[bar * 2], progressSegmentMap[bar * 2 + 1], color);
  }

  FastLED.show();
  FastLED.delay(1); // Yield to WiFi/OTA stack safely
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
  FastLED.delay(1); // Yield to WiFi/OTA stack safely
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

uint16_t XY(uint8_t x, uint8_t y) {
  // any out of bounds address maps to the first hidden pixel
  if ( (x >= MATRIX_WIDTH) || (y >= MATRIX_HEIGHT) ) {
    return (LAST_VISIBLE_LED + 1);
  }

  return XYTable[(y * MATRIX_WIDTH) + x];
}

void programClock(bool first) {
  static unsigned long updateTimer = millis();

  if (first || millis() - updateTimer > CLOCK_UPDATE_MS) {
    updateTimer = millis();

    if (initialTimeSync) {
      time_t t = currentTZ.timezone.toLocal(now());

      writeDigit(minute(t) % 10, 0, colorMinute);
      writeDigit(minute(t) / 10, 1, colorMinute);
      writeDigit(hour(t) % 10, 2, colorHour);
      writeDigit(hour(t) / 10, 3, colorHour);

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

void programMatrix(bool first) {
  static unsigned long updateTimer = millis();

  if (first || millis() - updateTimer > ANIMATION_UPDATE_MS) {
    updateTimer = millis();

    // Move code downward
    // Start with lowest row to allow proper overlapping on each column
    for (int8_t row = MATRIX_HEIGHT - 1; row >= 0; row--) {
      for (int8_t col = 0; col < MATRIX_WIDTH; col++) {
        if (leds[XY(col, row)] == CRGB(175, 255, 175)) {
          leds[XY(col, row)] = CRGB(27, 130, 39); // create trail
          if (row < MATRIX_HEIGHT - 1) leds[XY(col, row + 1)] = CRGB(175, 255, 175);
        }
      }
    }

    // fade all leds
    for(int i = 0; i < NUM_LEDS; i++) {
      if (leds[i].g != 255) leds[i].nscale8(192); // only fade trail
    }

    // check for empty screen to ensure code spawn
    bool emptyScreen = true;
    for(int i = 0; i < NUM_LEDS; i++) {
      if (leds[i]) {
        emptyScreen = false;
        break;
      }
    }

    // spawn new falling code
    if (random8(3) == 0 || emptyScreen) { // lower number == more frequent spawns
      int8_t spawnX = random8(MATRIX_WIDTH);
      leds[XY(spawnX, 0)] = CRGB(175, 255, 175);
    }

    FastLED.show();
  }
}

void programRainbow(bool first) {
  static unsigned long updateTimer = millis();

  if (first || millis() - updateTimer > ANIMATION_UPDATE_MS) {
    updateTimer = millis();

    int32_t yHueDelta32 = ((int32_t) cos16(updateTimer * (27 / 3)) * (350 / MATRIX_WIDTH));
    int32_t xHueDelta32 = ((int32_t) cos16(updateTimer * (39 / 3)) * (310 / MATRIX_HEIGHT));

    byte startHue8 = updateTimer / 65536;
    int8_t yHueDelta8 = yHueDelta32 / 32768;
    int8_t xHueDelta8 = xHueDelta32 / 32768;

    byte lineStartHue = startHue8;
    for (byte y = 0; y < MATRIX_HEIGHT; y++) {
      lineStartHue += yHueDelta8;
      byte pixelHue = lineStartHue;
      for (byte x = 0; x < MATRIX_WIDTH; x++) {
        pixelHue += xHueDelta8;
        leds[XY(x, y)] = CHSV(pixelHue, 255, 255);
      }
    }

    FastLED.show();
  }
}

void programFire(bool first) {
  static unsigned long updateTimer = millis();
  if (first) currentPalette = HeatColors_p;

  if (first || millis() - updateTimer > ANIMATION_UPDATE_MS) {
    updateTimer = millis();

    for (int i = 0; i < MATRIX_WIDTH; i++) {
      for (int j = 0; j < MATRIX_HEIGHT; j++) {
        leds[XY(i, j)] = ColorFromPalette(currentPalette, qsub8(inoise8(i * 60, j * 60 + updateTimer, updateTimer / 3),
        abs8(j - (MATRIX_HEIGHT - 1)) * 255 / (MATRIX_HEIGHT - 1)), 255);
      }
    }
    FastLED.show();
  }
}

uint16_t _plasmaShift = (random8(0, 5) * 32) + 64;
uint16_t _plasmaTime = 0;
const uint8_t _plasmaXfactor = 8;
const uint8_t _plasmaYfactor = 8;
void programPlasma(bool first) {
  static unsigned long updateTimer = millis();

  if (first || millis() - updateTimer > ANIMATION_UPDATE_MS) {
    updateTimer = millis();

    for (int16_t x = 0; x < MATRIX_WIDTH; x++) {
      for (int16_t y = 0; y < MATRIX_HEIGHT; y++) {
        int16_t r = sin16(_plasmaTime) / 256;
        int16_t h = sin16(x * r * _plasmaXfactor + _plasmaTime) + cos16(y * (-r) * _plasmaYfactor + _plasmaTime) + sin16(y * x * (cos16(-_plasmaTime) / 256) / 2);
        leds[XY(x, y)] = CHSV((uint8_t)((h / 256) + 128), 255, 255);
      }
    }
    uint16_t oldPlasmaTime = _plasmaTime;
    _plasmaTime += _plasmaShift;
    if (oldPlasmaTime > _plasmaTime)
    _plasmaShift = (random8(0, 5) * 32) + 64;

    FastLED.show();
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

void setupRandom() {
  uint32_t seed;

  // Random works best with a seed that can use 31 bits analogRead on a unconnected pin tends
  // toward less than four bits
  seed = analogRead(0);
  delay(1);

  for (int shifts = 3; shifts < 31; shifts += 3) {
    seed ^= analogRead(0) << shifts;
    delay(1);
  }

  randomSeed(seed);
}

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
  setupRandom();
}

void loop() {
  loopPortal();
  loopOTA();
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaInProgress) {
      surpriseAndDelight();
      loopDisplay();
    }
    loopClock();
  }
}
