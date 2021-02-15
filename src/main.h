// =--------------------------------------------------------------------------------= Constants =--=

#define CONFIG_FILE                               "/settings.json"

#define LED_STRIP_PIN                             15
#define NUM_LEDS                                  170  // sum of descriptors + colons

#define CHAR_DASH                                 16

#define LUMINANCE                                 32

#define CAPTIVE_PORTAL_BLINK_MS                   1000
#define CLOCK_UPDATE_MS                           1000

#define NTP_UPDATE_MS                             10 * 60 * 1000 // interval between NTP checks
#define NTP_RETRY_MS                              5000 // Retry connection to NTP


// =-------------------------------------------------------------------------------= Time Zones =--=

void loadConfig();
void saveConfig(String timezone);


// =-------------------------------------------------------------------------------= Time Zones =--=

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
  // { "Europe/London", 0 },
  // { "Europe/Berlin", 1 },
  // { "Europe/Helsinki", 2 },
  // { "Europe/Moscow", 3 },
  // { "Asia/Dubai", 4 },
  // { "Asia/Karachi", 5 },
  // { "Asia/Dhaka", 6 },
  // { "Asia/Jakarta", 7 },
  // { "Asia/Manila", 8 },
  // { "Asia/Tokyo", 9 },
  // { "Australia/Brisbane", 10 },
  // { "Pacific/Noumea", 11 },
  // { "Pacific/Auckland", 12 },
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

static const char PORTAL_TIMEZONE_PAGE[] PROGMEM = R"(
{
  "title": "TimeZone",
  "uri": "/timezone",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "Select local time zone for clock display",
      "style": "font-family:Arial;font-weight:bold;text-align:center;margin-bottom:10px;color:DarkSlateBlue"
    },
    {
      "name": "timezone",
      "type": "ACSelect",
      "label": "Time Zone",
      "option": [],
      "selected": 10
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

uint32_t getPixelColorHsv(uint16_t h, uint8_t s, uint8_t v);
void writeDigit(uint8_t character, uint16_t place, uint32_t color);
void writeAllDigits(uint8_t character, uint32_t color);
void writeSegment(uint16_t startingLed, uint16_t quantity, uint32_t color); 

/**
* A 7-segment LED descriptor used to identify its starting LED address and the number of LEDs in
* the digit.
*
* @warning Change the data types to uint16_t if you have more than 256 LEDs.
*/
struct ledSescriptor_t
{
  uint8_t    startingLedNumber;    // the address of the first LED in a digit
  uint8_t    numStrips;            // number of strips per segment
  uint8_t    ledsPerStrip;         // number of LEDs per strip
};

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
