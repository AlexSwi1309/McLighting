//#define USE_NEOANIMATIONFX  // Uses NeoAnimationFX, PIN is ignored & set to RX/GPIO3, see: https://github.com/debsahu/NeoAnimationFX
#define USE_WS2812FX          // Uses WS2812FX, see: https://github.com/kitesurfer1404/WS2812FX

// Neopixel
#define PIN 15             // PIN (15 / D8) where neopixel / WS2811 strip is attached 
#define NUMLEDS 194        // Number of leds in the strip 
#define BUILTIN_LED 2      // ESP-12F has the built in LED on GPIO2, see https://github.com/esp8266/Arduino/issues/2192
#define BUTTON  14         // Input pin (14 / D5) for switching the LED strip on / off, connect this PIN to ground to trigger button.
#define BUTTON_GY33 12         // Input pin (12 / D6) for read color data with RGB sensor, connect this PIN to ground to trigger button.
#define RGBW

const char HOSTNAME[] = "ESPLightRGBW02";   // Friedly hostname

#define HTTP_OTA           // If defined, enable Added ESP8266HTTPUpdateServer
//#define ENABLE_OTA           // If defined, enable Arduino OTA code.
#define ENABLE_AMQTT         // If defined, enable Async MQTT code, see: https://github.com/marvinroger/async-mqtt-client
//#define ENABLE_MQTT          // If defined, enable MQTT client code, see: https://github.com/toblum/McLighting/wiki/MQTT-API
#define ENABLE_HOMEASSISTANT // If defined, enable Homeassistant integration, ENABLE_MQTT must be active
#define ENABLE_BUTTON        // If defined, enable button handling code, see: https://github.com/toblum/McLighting/wiki/Button-control
#define ENABLE_BUTTON_GY33       //
//#define MQTT_HOME_ASSISTANT_SUPPORT // If defined, use AMQTT and select Tools -> IwIP Variant -> Higher Bandwidth


#if defined(USE_NEOANIMATIONFX) and defined(USE_WS2812FX)
#error "Cant have both NeoAnimationFX and WS2812FX enabled. Choose either one."
#endif
#if !defined(USE_NEOANIMATIONFX) and !defined(USE_WS2812FX)
#error "Need to either use NeoAnimationFX and WS2812FX mode."
#endif
#if defined(ENABLE_MQTT) and defined(ENABLE_AMQTT)
#error "Cant have both PubSubClient and AsyncMQTT enabled. Choose either one."
#endif
#if ( (defined(ENABLE_HOMEASSISTANT) and !defined(ENABLE_MQTT)) and (defined(ENABLE_HOMEASSISTANT) and !defined(ENABLE_AMQTT)) )
#error "To use HA, you have to either enable PubCubClient or AsyncMQTT"
#endif

// parameters for automatically cycling favorite patterns
uint32_t autoParams[][4] = {   // color, speed, mode, duration (seconds)
  {0x00ff0000, 250,  1,  5.0}, // blink red for 5 seconds
  {0x0000ff00, 200,  3, 10.0}, // wipe green for 10 seconds
  {0x000000ff, 200, 14,  5.0}, // dual scan blue for 5 seconds
  {0x000000ff, 200, 46, 15.0}  // fireworks for 15 seconds
};

#if defined(ENABLE_MQTT) or defined(ENABLE_AMQTT)
  #ifdef ENABLE_MQTT
    #define MQTT_MAX_PACKET_SIZE 512
    #define MQTT_MAX_RECONNECT_TRIES 4

    int mqtt_reconnect_retries = 0;
    char mqtt_intopic[strlen(HOSTNAME) + 4 + 5];      // Topic in will be: <HOSTNAME>/in
    char mqtt_outtopic[strlen(HOSTNAME) + 5 + 5];     // Topic out will be: <HOSTNAME>/out
    uint8_t qossub = 0; // PubSubClient can sub qos 0 or 1
  #endif

  #ifdef ENABLE_AMQTT
    String mqtt_intopic = String(HOSTNAME) + "/in";
    String mqtt_outtopic = String(HOSTNAME) + "/out";
    uint8_t qossub = 0; // AMQTT can sub qos 0 or 1 or 2
    uint8_t qospub = 0; // AMQTT can pub qos 0 or 1 or 2
  #endif

  #ifdef ENABLE_HOMEASSISTANT
    String mqtt_ha = "home/" + String(HOSTNAME) + "_ha/";
    String mqtt_ha_state_in = mqtt_ha + "state/in";
    String mqtt_ha_state_out = mqtt_ha + "state/out";

    const char* on_cmd = "ON";
    const char* off_cmd = "OFF";
    bool stateOn = false;
    bool animation_on = false;
    bool new_ha_mqtt_msg = false;
    uint16_t color_temp = 327; // min is 154 and max is 500
  #endif

  const char mqtt_clientid[] = "NeoPixelsStrip"; // MQTT ClientID

  char mqtt_host[64] = "";
  char mqtt_port[6] = "";
  char mqtt_user[32] = "";
  char mqtt_pass[32] = "";
#endif


// ***************************************************************************
// Global variables / definitions
// ***************************************************************************
#define DBG_OUTPUT_PORT Serial  // Set debug output port

// List of all color modes
enum MODE { SET_MODE, HOLD, OFF, ALL, SETCOLOR, SETSPEED, BRIGHTNESS, WIPE, RAINBOW, RAINBOWCYCLE, THEATERCHASE, TWINKLERANDOM, THEATERCHASERAINBOW, TV, CUSTOM, AUTO };

MODE mode = RAINBOW;        // Standard mode that is active when software starts

int ws2812fx_speed = 196;   // Global variable for storing the delay between color changes --> smaller == faster
int brightness = 196;       // Global variable for storing the brightness (255 == 100%)

int ws2812fx_mode = 0;      // Helper variable to set WS2812FX modes

bool exit_func = false;     // Global helper variable to get out of the color modes when mode changes

bool shouldSaveConfig = false;  // For WiFiManger custom config

struct ledstate             // Data structure to store a state of a single led
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
};

typedef struct ledstate LEDState;     // Define the datatype LEDState
LEDState ledstates[NUMLEDS];          // Get an array of led states to store the state of the whole strip
LEDState main_color = { 0, 255, 0, 0};  // Store the "main color" of the strip used in single color modes

#define ENABLE_STATE_SAVE_SPIFFS        // If defined, saves state on SPIFFS
//#define ENABLE_STATE_SAVE_EEPROM      // If defined, save state on reboot
#ifdef ENABLE_STATE_SAVE
  char current_state[36];               // Keeps the current state representation
  char last_state[36];                  // Save the last state as string representation
  unsigned long time_statechange = 0;   // Time when the state last changed
  int timeout_statechange_save = 5000;  // Timeout in ms to wait before state is saved
  bool state_save_requested = false;    // State has to be saved after timeout
#endif
#ifdef ENABLE_STATE_SAVE_SPIFFS
  bool updateStateFS = false;
#endif

// Button handling

#ifdef ENABLE_BUTTON || ENABLE_BUTTON_GY33
  boolean buttonState = false;
#endif

#ifdef ENABLE_BUTTON
  #define BTN_MODE_SHORT  "STA| 1|  0|245|196|255|  0|  0|  0"  // Static white
  #define BTN_MODE_MEDIUM "STA| 1| 48|245|196|  0|255|102|  0"  // Fire flicker
  #define BTN_MODE_LONG   "STA| 1| 46|253|196|  0|255|102|  0"  // Fireworks random
  
  unsigned long keyPrevMillis = 0;
  const unsigned long keySampleIntervalMs = 25;
  byte longKeyPressCountMax = 80;       // 80 * 25 = 2000 ms
  byte mediumKeyPressCountMin = 20;     // 20 * 25 = 500 ms
  byte KeyPressCount = 0;
  byte prevKeyState = HIGH;             // button is active low
#endif

#ifdef ENABLE_BUTTON_GY33
  #define BTN_MODE_SHORT  "STA| 1|  0|245|196|255|  0|  0|  0"  // Static white
  #define BTN_MODE_MEDIUM "STA| 1| 48|245|196|  0|255|102|  0"  // Fire flicker
  #define BTN_MODE_LONG   "STA| 1| 46|253|196|  0|255|102|  0"  // Fireworks random
  
  unsigned long keyPrevMillis2 = 0;
  const unsigned long keySampleIntervalMs2 = 25;
  byte longKeyPressCountMax2 = 80;       // 80 * 25 = 2000 ms
  byte mediumKeyPressCountMin2 = 20;     // 20 * 25 = 500 ms
  byte KeyPressCount2 = 0;
  byte prevKeyState2 = HIGH;             // button is active low
#endif

