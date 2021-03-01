// =--------------------------------------------------------------------------------= Libraries =--=

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <AutoConnect.h>


// =--------------------------------------------------------------------------------= Constants =--=

#define CONFIG_FILE                               "/settings.json"

#define LED_PIN                                   15
#define MATRIX_WIDTH                              29
#define MATRIX_HEIGHT                             12
#define NUM_LEDS                                  (MATRIX_WIDTH * MATRIX_HEIGHT)
#define LAST_VISIBLE_LED                          347
#define CLOCK_UPDATE_MS                           1000
#define ANIMATION_UPDATE_MS                       66 // 15fps

#define CHAR_DASH                                 16

#define LUMINANCE                                 28

#define MDNS_HOSTNAME                             "big-clock"
#define CAPTIVE_PORTAL_BLINK_MS                   1000

#define NTP_UPDATE_MS                             10 * 60 * 1000 // interval between NTP checks
#define NTP_RETRY_MS                              5000 // Retry connection to NTP

#define OTA_PUBKEY "-----BEGIN PUBLIC KEY-----\nMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAtaQtsdcGeKc9FlHsOnYh\nv1g6Hdsu2+t3/m5AJeT9ZHRJXcrxBKE8SL3WFpAXW28PiW1aHvG7ZNLEgoWlF48G\nwuzoigyiKxB0le937FgV7jvkVDlRjyXN0CZyBNftLqn95LKIaUWmxrWx/a8IUj8l\nY3n7OpqK/17ip0S0UrX8CY3jCE5zf57t6fdB7OkQItJtBO6pcgwWjpwWL3Paur+X\nPn92cRaJaA6ZSheqpk01e9mRVxRUQ8G1zUCDHKyUXpMH5EwctL0ugegQKWLerxFr\nZSDvMA1x18UyrUQgu9Yirf/b3CbQfRyuY4wW5alrSDs0AYr1osegV2OsA+lJOWxJ\n2QIDAQAB\n-----END PUBLIC KEY-----"
#define OTA_PORT                                  8266


// =----------------------------------------------------------------------------------= Statics =--=

/**
* A 7-segment LED descriptor used to identify its starting LED address and the number of LEDs in
* the digit.
*/
struct ledDescriptor_t {
  uint16_t startingLedNumber;   // the address of the first LED in a digit
  uint8_t  numStrips;            // number of strips per segment
  uint8_t  ledsPerStrip;         // number of LEDs per strip
};

// Display Layout and Pixels
static struct ledDescriptor_t descriptors[] = {
  {  0, 2, 3}, // __:_X
  { 42, 2, 3}, // __:X_
  { 86, 2, 3}, // _X:__ -- skip colon pixels
  {128, 2, 3}  // X_:__
};

static const uint16_t colon1 = 84;  // the address of the first colon LED
static const uint16_t colon2 = 85;  // the address of the second colon LED

// Matrix mapping
/**
 * @brief X-Y Matrix to LED strip mapping table
 *
 * This crazy thing let's us map a full X-Y matrix to the actual clock pixels. We use a full matrix
 * instead of the strip array + safety pixel so we can use animations that rely on surrounding pixel
 * data and copying from previous frames.
 */
static const uint16_t XYTable[] = {
  170, 171, 133, 132, 131, 172, 173, 174, 175,  91,  90,  89, 176, 177, 178, 179, 180,  47,  46,  45, 181, 182, 183, 184,   5,   4,   3, 185, 186,
  187, 188, 164, 165, 166, 189, 190, 191, 192, 122, 123, 124, 193, 194, 195, 196, 197,  78,  79,  80, 198, 199, 200, 201,  36,  37,  38, 202, 203,
  134, 163, 204, 205, 206, 167, 130,  92, 121, 207, 208, 209, 125,  88, 210,  48,  77, 211, 212, 213,  81,  44,   6,  35, 214, 215, 216,  39,   2,
  135, 162, 217, 218, 219, 168, 129,  93, 120, 220, 221, 222, 126,  87,  84,  49,  76, 223, 224, 225,  82,  43,   7,  34, 226, 227, 228,  40,   1,
  136, 161, 229, 230, 231, 169, 128,  94, 119, 232, 233, 234, 127,  86, 235,  50,  75, 236, 237, 238,  83,  42,   8,  33, 239, 240, 241,  41,   0,
  242, 243, 160, 159, 158, 244, 245, 246, 247, 118, 117, 116, 248, 249, 250, 251, 252,  74,  73,  72, 253, 254, 255, 256,  32,  31,  30, 257, 258,
  259, 260, 137, 138, 139, 261, 262, 263, 264,  95,  96,  97, 265, 266, 267, 268, 269,  51,  52,  53, 270, 271, 272, 273,   9,  10,  11, 274, 275,
  149, 148, 276, 277, 278, 140, 157, 107, 106, 179, 280, 281,  98, 115, 282,  63,  62, 283, 284, 285,  54,  71,  21,  20, 286, 287, 288,  12,  29,
  150, 147, 289, 290, 291, 141, 156, 108, 105, 292, 293, 294,  99, 114,  85,  64,  61, 295, 296, 297,  55,  70,  22,  19, 298, 299, 300,  13,  28,
  151, 146, 301, 302, 303, 142, 155, 109, 104, 304, 305, 306, 100, 113, 307,  65,  60, 308, 309, 310,  56,  69,  23,  18, 311, 312, 313,  14,  27,
  314, 315, 145, 144, 143, 316, 317, 318, 319, 103, 102, 101, 320, 321, 322, 323, 324,  59,  58,  57, 325, 326, 327, 328,  17,  16,  15, 329, 330,
  331, 332, 152, 153, 154, 333, 334, 335, 336, 110, 111, 112, 337, 338, 339, 340, 341,  66,  67,  68, 342, 343, 344, 345,  24,  25,  26, 346, 347
};

// Colors
static const CHSV colorOrange           = CHSV( 35, 255, 255);
static const CHSV colorBeige            = CHSV( 35, 200, 255);
static const CHSV colorYellow           = CHSV( 43, 255, 255);
static const CHSV colorOcean            = CHSV(141, 255, 255);
static const CHSV colorCyan             = CHSV(131, 255, 255);

static const CHSV colorHour             = colorBeige;
static const CHSV colorColon            = colorOcean;
static const CHSV colorMinute           = colorBeige;


// =---------------------------------------------------------------------------------= Programs =--=

void setProgram(uint8_t program);
void programClock(bool first);
void programMatrix(bool first);
void programRainbow(bool first);
void programFire(bool first);
void programPlasma(bool first);

void (*renderFunc[])(bool first) {
  programClock,
  programMatrix,
  programRainbow,
  programFire,
  programPlasma
};
#define PROGRAM_COUNT (sizeof(renderFunc) / sizeof(renderFunc[0]))

const char *programNames[] = {
  "clock",
  "matrix",
  "rainbow",
  "fire",
  "plasma"
};

// =-------------------------------------------------------------------------------= Filesystem =--=

void loadConfig();
void saveConfig(String timezone);


// =-------------------------------------------------------------------------------= Time Zones =--=

// New Zealand Time Zone
TimeChangeRule tzNewZealandSTD = {"NZST", First, Sun, Apr, 3, 720};   // UTC + 12 hours
TimeChangeRule tzNewZealandDST = {"NZDT", Last, Sun, Sep, 2, 780};    // UTC + 13 hours
Timezone tzNewZealand(tzNewZealandDST, tzNewZealandSTD);

// Australia Eastern Time Zone (Sydney, Melbourne)
TimeChangeRule tzAustraliaEDT = {"AEDT", First, Sun, Oct, 2, 660};
TimeChangeRule tzAustraliaEST = {"AEST", First, Sun, Apr, 3, 600};
Timezone tzAustraliaET(tzAustraliaEDT, tzAustraliaEST);

// Moscow Standard Time (MSK, does not observe DST)
TimeChangeRule tzEuropeMoscow = {"MSK", Last, Sun, Mar, 1, 180};
Timezone tzEuropeMSK(tzEuropeMoscow);

// United Kingdom (London, Belfast)
TimeChangeRule tzEuropeBST = {"BST", Last, Sun, Mar, 1, 60};
TimeChangeRule tzEuropeGMT = {"GMT", Last, Sun, Oct, 2, 0};
Timezone tzEuropeUK(tzEuropeBST, tzEuropeGMT);

// UTC
TimeChangeRule utcRule = {"UTC", Last, Sun, Mar, 1, 0};
Timezone tzUTC(utcRule);

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule tzAmericaEDT = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule tzAmericaEST = {"EST", First, Sun, Nov, 2, -300};
Timezone tzAmericaET(tzAmericaEDT, tzAmericaEST);

// US Central Time Zone (Chicago, Houston)
TimeChangeRule tzAmericaCDT = {"CDT", Second, Sun, Mar, 2, -300};
TimeChangeRule tzAmericaCST = {"CST", First, Sun, Nov, 2, -360};
Timezone tzAmericaCT(tzAmericaCDT, tzAmericaCST);

// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule tzAmericaMDT = {"MDT", Second, Sun, Mar, 2, -360};
TimeChangeRule tzAmericaMST = {"MST", First, Sun, Nov, 2, -420};
Timezone tzAmericaMT(tzAmericaMDT, tzAmericaMST);

// Arizona is US Mountain Time Zone but does not use DST
Timezone tzAmericaAZ(tzAmericaMST);

// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule tzAmericaPDT = {"PDT", Second, Sun, Mar, 2, -420};
TimeChangeRule tzAmericaPST = {"PST", First, Sun, Nov, 2, -480};
Timezone tzAmericaPT(tzAmericaPDT, tzAmericaPST);

/**
 * Timezone struct to collect human readable name and the timezone object with daylight saving rules
 */
typedef struct {
  const char* name;
  Timezone    timezone;
} Timezone_t;

/**
 * Complete listing of timezones that can be selected in the portal UI
 */
static const Timezone_t TZ_LIST[] = {
  { "Pacific/New Zealand", tzNewZealand },
  // { "Pacific/Noumea", 11 },
  { "Australia/Sydney", tzAustraliaET },
  // { "Asia/Tokyo", 9 },
  // { "Asia/Manila", 8 },
  // { "Asia/Jakarta", 7 },
  // { "Asia/Dhaka", 6 },
  // { "Asia/Karachi", 5 },
  // { "Asia/Dubai", 4 },
  { "Europe/Moscow", tzEuropeMSK },
  // { "Europe/Helsinki", 2 },
  // { "Europe/Berlin", 1 },
  { "UTC", tzUTC },
  { "Europe/London", tzEuropeUK },
  // { "Atlantic/Azores", -1 },
  // { "America/Noronha", -2 },
  // { "America/Araguaina", -3 },
  { "America/Eastern", tzAmericaET },
  { "America/Central", tzAmericaCT },
  { "America/Mountain", tzAmericaMT },
  { "America/Arizone", tzAmericaAZ },
  { "America/Pacific", tzAmericaPT }
  // { "Pacific/Samoa", -11 },
};


// =---------------------------------------------------------------------------= Captive Portal =--=

static const char PORTAL_CONFIGURE_PAGE[] PROGMEM = R"(
{
  "title": "Configure",
  "uri": "/config",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "Set options for the big clock",
      "style": "font-family:Arial;font-weight:bold;text-align:center;margin-bottom:10px;color:DarkSlateBlue"
    },
    {
      "name": "timezone",
      "type": "ACSelect",
      "label": "Time Zone",
      "option": []
    },
    {
      "name": "newline",
      "type": "ACElement",
      "value": "<br>"
    },
    {
      "name": "program",
      "type": "ACSelect",
      "label": "Program",
      "option": []
    },
    {
      "name": "newline",
      "type": "ACElement",
      "value": "<br>"
    },
    {
      "name": "start",
      "type": "ACSubmit",
      "value": "OK",
      "uri": "/start"
    }
  ]
}
)";


// =----------------------------------------------------------------------------= NTP and Clock =--=

void syncLocalClock();


// =--------------------------------------------------------------------------= WiFi and Portal =--=

void portalRootPage();
void portalStartPage();
bool loopCaptivePortal(void);
bool startCaptivePortal(IPAddress& ip);
void onWifiConnect(IPAddress& ipaddr);


// =--------------------------------------------------------------------= Seven Segment Display =--=

void writeDigit(uint8_t character, uint16_t place, CRGB color);
void writeAllDigits(uint8_t character, CRGB color);
void writeSegment(uint16_t place, uint8_t segment, CRGB color);
void writeSegmentStrip(uint16_t startingLed, uint16_t quantity, CRGB color); 

/**
 * @brief A 7-Segment display 'font'.
 *
 * This is the font used to convert the values from 0-F into a displayable character on a 7-segment
 * display. There is one value for each character.
 *
 * The relationship of the font bit positions to the LED display segments:
 *
@verbatim
   --1--
  |     |
  2     0
  |     |
   --3--
  |     |
  6     4
  |     |
   --5--
@endverbatim
 *
 * For example: In order to display the number '1', the #0 and #4 segments need to be turned on and
 * the rest turned off.  Therefore the binary value 0010001 (hex 0x11) represents the segment values
 * to display a '1'.
 *
 * The most significant bit is not used.
 */
static const uint8_t font[] = {
  0b01110111,  // 0
  0b00010001,  // 1
  0b01101011,  // 2
  0b00111011,  // 3
  0b00011101,  // 4
  0b00111110,  // 5
  0b01111110,  // 6
  0b00010011,  // 7
  0b01111111,  // 8
  0b00111111,  // 9
  0b01011111,  // A
  0b01111100,  // b
  0b01100110,  // C
  0b01111001,  // d
  0b01101110,  // E
  0b01001110,  // F
  0b00001000,  // - (dash)
  0b00100000,  // _ (underscore)
  0b00001111   // º (degree)
};

/**
 * @brief An ordered map of display segment to segment for the progress bar
 *
 * Segment order is as follows:
 *
@verbatim
   --1--
  |     |
  2     0
  |     |
   --3--
  |     |
  6     4
  |     |
   --5--
@endverbatim
 *
 * Ordered pair: place, segment.
 */
static const uint8_t progressSegmentMap[] = {
  3, 1,
  2, 1,
  1, 1,
  0, 1,
  0, 0,
  0, 3,
  1, 3,
  2, 3,
  3, 3,
  3, 6,
  3, 5,
  2, 5,
  1, 5,
  0, 5
};
