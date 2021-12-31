#include <Arduino.h>
// On Air Display control
// Light up the On Air sign using a strip of 10 WS2812s
// Button controls if sign is on or off
// Webpage also available to control sign

#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiManager.h>              // For managing the Wifi Connection
#include <ESPmDNS.h>                  // For running OTA and Web Server
#include <WiFiUdp.h>                  // For running OTA
#include <ArduinoOTA.h>               // For running OTA
#include <ESPAsyncWebServer.h>        // For running Web Server
#include <ArduinoJson.h>              // For running Web Services
#include <Wire.h>                     // For Servos
#include <SPI.h>                      // For display
#include <Adafruit_GFX.h>             // For display
#include <Adafruit_ILI9341.h>         // For display
//#include <XPT2046_Touchscreen.h>      // For display
#include <Adafruit_PWMServoDriver.h>  // For Servos
#include "settings.h"                 // Secret values
using namespace std;



//for using LED as a startup status indicator
#include <Ticker.h>
Ticker ticker;
boolean ledState = LOW;   // Used for blinking LEDs when WifiManager in Connecting and Configuring

// On board LED used to show status
#ifndef LED_BUILTIN
#define LED_BUILTIN 13 // ESP32 DOES NOT DEFINE LED_BUILTIN
#endif
const int ledPin =  LED_BUILTIN;  // the number of the LED pin

//
// TFT definitions
//
//#define TFT_CS D0  //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
//#define TFT_DC D8  //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
//#define TFT_RST -1 //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
//#define TS_CS D3   //for D1 mini or TFT I2C Connector Shield (V1.1.0 or later)
//#define TFT_LED D2  //for D1 mini to adjust brightness

#define TFT_CS 14  //for D32 Pro
#define TFT_DC 27  //for D32 Pro
#define TFT_RST 33 //for D32 Pro
#define TS_CS  12 //for D32 Pro

// Visual part of the Display
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Screen Display positions of interest
#define BACKGROUND_COLOR ILI9341_BLACK
#define DIVIDER_HEIGHT 3
#define DIVIDER_COLOR ILI9341_CYAN
#define SETUP_FONT_SIZE 1
#define SETUP_FONT_COLOR ILI9341_WHITE
#define SERVO_STATUS_FONT_SIZE 2
#define SERVO_STATUS_FONT_COLOR ILI9341_YELLOW
uint16_t screenHeight = 0;
uint16_t screenWidth = 0;
uint16_t statusTopLeftX = 0;
uint16_t statusTopLeftY = 0;
uint16_t statusHeight = 0;

bool drawScreen = false;


//
// Touchscreen definitions
//
// Touch response part of the display
//XPT2046_Touchscreen ts(TS_CS);

// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 700
#define TS_MINY 500
#define TS_MAXX 3500
#define TS_MAXY 3800



//
// PWM definitions
//
// called this way, it uses the default address 0x40
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// Depending on your servo make, the pulse width min and max may vary, you 
// want these to be as small/large as possible without hitting the hard stop
// for max range. You'll have to tweak them as necessary to match the servos you
// have!
//#define SERVOMIN  0    // This is the 'minimum' pulse length count (out of 4096)
//#define SERVOMAX  4095 // This is the 'maximum' pulse length count (out of 4096)
#define SERVOMIN  150    // This is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX  600 // This is the 'maximum' pulse length count (out of 4096)
#define SERVO_FREQ 50  // Analog servos run at ~60 Hz updates


//
// Web Server definitions
//
AsyncWebServer server(80);


//
// Head definitions
//
typedef struct headServo {  // Definition for Servo being used by the head
  uint8_t servoNum;         // Position number of the servo attached to the PWM
  String  name;             // Name for the servo
  uint8_t limitMinAngle;    // This is the minimum angle allowed for this servo
  uint8_t limitMaxAngle;    // This is the maximum angle allowed for this servo
  uint8_t angle;            // Expected current angle of the servo
  String  direction;        // Direction the servos move UD, DU, LR, RL
} headServo;

// The widest range for all the servos is 30 to 150
headServo left_eyebrow        = {0, "LEB",    58, 102,  80, "UD"};
headServo right_eyebrow       = {1, "REB",    64, 114,  89, "DU"};
headServo left_eye_updown     = {2, "LE_UD",  70, 100,  85, "UD"};
headServo left_eye_leftright  = {3, "LE_LR",  50, 76,   63, "LR"};
headServo right_eye_updown    = {4, "RE_UD",  73, 123,  98, "UD"};
headServo right_eye_leftright = {5, "RE_LR",  59,  94,  76, "RL"};
headServo mouth               = {6, "MOUTH",  64,  88,  65, "UD"};
headServo neck_updown         = {7, "N_UD",   67,  77,  70, "UD"};
headServo neck_leftright      = {8, "N_LR",   30, 100,  65, "LR"};

headServo *headServos[] = {&left_eyebrow, &right_eyebrow, &left_eye_updown, &left_eye_leftright, &right_eye_updown, &right_eye_leftright, &mouth, &neck_updown, &neck_leftright};
int headServosLen = sizeof(headServos) / sizeof(headServos[0]);


/*************************************************
 * Callback Utilities during setup
 *************************************************/
 
/*
 * Blink the LED Strip.
 * If on  then turn off
 * If off then turn on
 */
void tick()
{
  //toggle state
  digitalWrite(ledPin, !digitalRead(ledPin));     // set pin to the opposite state
}

/*
 * gets called when WiFiManager enters configuration mode
 */
void configModeCallback (WiFiManager *myWiFiManager) {
  //Serial.println("Entered config mode");
  //Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  //Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


/*************************************************
 * TFT Functions
 *************************************************/

/*
 * Initialize the TFT for text
 */
void clearTftScreen(uint8_t fontSize, uint16_t fontColor) {
  tft.fillScreen(BACKGROUND_COLOR);
  // origin = left,top landscape (USB left upper)
  tft.setRotation(0);
  tft.setTextSize(fontSize);
  tft.setTextColor(fontColor);
  tft.setCursor(0,0);
}

void drawServoStatus() {
  //tft.setCursor(statusTopLeftX, statusTopLeftY);
  //tft.setTextSize(SERVO_STATUS_FONT_SIZE);
  //tft.setTextColor(SERVO_STATUS_FONT_COLOR);
  //tft.fillRect(statusTopLeftX, statusTopLeftY, screenWidth, statusHeight, BACKGROUND_COLOR);
  clearTftScreen(SERVO_STATUS_FONT_SIZE, SERVO_STATUS_FONT_COLOR);

  for (headServo *hs : headServos) {
    uint16_t currPwm = pwm.getPWM(hs->servoNum);
    tft.printf("%2d %5s %3d %4d\n", hs->servoNum, hs->name.c_str(), hs->angle, currPwm);
    //tft.ptintln();
  }
}

/*************************************************
 * Servo Functions
 *************************************************/

void setAngle (headServo *hs, int angle, bool limit = true) {
  if (angle < 0)   angle = 0;
  if (angle > 180) angle = 180;
  if (limit && angle < hs->limitMinAngle) angle = hs->limitMinAngle;
  if (limit && angle > hs->limitMaxAngle) angle = hs->limitMaxAngle;
  uint16_t pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(hs->servoNum, 0, pulse);
  hs->angle = angle;
  //tft.printf("%2d: %3d - %4d", hs->servoNum, angle, pulse);
}


void servoRangeTest() {
  uint8_t minRange = 255; // starting minimum range value across all servos
  uint8_t maxRange = 0;   // starting maximum range value across all servos
  uint16_t curX = 0;
  uint16_t curY = 0;

  // Find the min and max of the limits
  for (headServo *hs : headServos) {
    if (hs->limitMaxAngle > maxRange) maxRange = hs->limitMaxAngle;
    if (hs->limitMinAngle < minRange) minRange = hs->limitMinAngle;
  }
  tft.printf("minRange: %d\nmaxRange: %d\n", minRange, maxRange);

  if (minRange < maxRange) {
    int interval = (maxRange - minRange) / 20;

    // Move all servos through the range from min to max
    tft.println("min to max");
    curX = tft.getCursorX();
    curY = tft.getCursorY();
    for (int angle = minRange; angle < maxRange; angle += interval) {
      for (headServo *hs : headServos) {
        if (hs->limitMinAngle < angle && angle < hs->limitMaxAngle) {
          tft.fillRect(curX, curY, screenWidth, 8, ILI9341_BLUE);
          setAngle(hs, angle);
          tft.setCursor(curX, curY);
          delay(500);
        }
      }
      delay(1000);
    }
    tft.println();
    delay(250);

    // Move all servos through the range from max to min
    tft.println("max to min");
    curX = tft.getCursorX();
    curY = tft.getCursorY();
    for (int angle = maxRange; angle > minRange; angle -= interval) {
      for (headServo *hs : headServos) {
        if (hs->limitMinAngle < angle && angle < hs->limitMaxAngle) {
          tft.fillRect(curX, curY, screenWidth, 8, ILI9341_BLUE);
          setAngle(hs, angle);
          tft.setCursor(curX, curY);
        }
      }
      delay(25);
    }
    tft.println();
    delay(250);

  }
}

void middleServos() {

  // Find the min and max of the limits
  for (headServo *hs : headServos) {
    uint8_t middle = ((hs->limitMaxAngle - hs->limitMinAngle)/2) + hs->limitMinAngle;
    setAngle(hs, middle);
  }

}


//
// set servos to initial values
//
void initServos() {
  // Find the min and max of the limits
  for (headServo *hs : headServos) {
    setAngle(hs, hs->angle);
  }

}
//
// Send the current status of the Servos
//
void sendStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument jsonDoc(2048);
  JsonObject jsonRoot = jsonDoc.to<JsonObject>();

  JsonArray jsonServoArray = jsonRoot.createNestedArray("servos");

  for (int s=0; s<headServosLen; s++) {
    JsonObject jsonServoStatus = jsonServoArray.createNestedObject();
    jsonServoStatus["servoNum"] = headServos[s]->servoNum;
    jsonServoStatus["name"] = headServos[s]->name;
    jsonServoStatus["angle"] = headServos[s]->angle;
    jsonServoStatus["minAngle"] = headServos[s]->limitMinAngle;
    jsonServoStatus["maxAngle"] = headServos[s]->limitMaxAngle;
    jsonServoStatus["direction"] = headServos[s]->direction;
  }

  String payload;
  serializeJson(jsonDoc, payload);
  request->send(200, "application/json", payload);

}

/*************************************************
 * Web Server Routines
 *************************************************/

/*
//
// Display the main page for selecting a face to display
//
void handleRoot () {
  server.send_P(200, "text/html", MAIN_PAGE, sizeof(MAIN_PAGE));
}

//
// Display the test page for individually moving servos
//
void handleTest () {
  server.send_P(200, "text/html", TEST_PAGE, sizeof(TEST_PAGE));
}
*/

//
// Handle service for Servos 
//
void handleServos (AsyncWebServerRequest *request) {
  uint8_t servoNum = 0;
  uint8_t angle = 0;
  bool limit = true;
  //left_eyebrow.name = String("HNDL");
  switch (request->method()) {
    case HTTP_PUT:
      //left_eyebrow.name = "PUT";
      if (request->hasArg("servoNum") && request->hasArg("angle")) {
        servoNum = request->arg("servoNum").toInt();
        angle = request->arg("angle").toInt();
        headServo *hs = headServos[servoNum];
        if (request->hasArg("override")) limit = false;
        setAngle(hs, angle, limit);
      }
      sendStatus(request);
      drawScreen = true;
      break;
    case HTTP_GET:
      //left_eyebrow.name = "GET";
      sendStatus(request);
      break;
    default:
      request->send(405, "text/plain", "Method Not Allowed");
      break;
  }
  //server.send_P(200, "text/html", TEST_PAGE, sizeof(TEST_PAGE));
}

//
// Display a Not Found page
//
void handleNotFound(AsyncWebServerRequest *request) {
  //digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += request->methodToString();
  message += "\nArguments: ";
  message += request->args();
  message += "\n";
  for (uint8_t i = 0; i < request->args(); i++) {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }
  request->send(404, "text/plain", message);
  //digitalWrite(led, 0);
}


/*************************************************
 * Setup
 *************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println(ESP.getSdkVersion());

  // set the digital pin as output:
  pinMode(ledPin, OUTPUT);

  tft.begin();
  clearTftScreen(SETUP_FONT_SIZE, SETUP_FONT_COLOR);
  screenWidth = tft.width();
  screenHeight = tft.height();

  tft.println(ESP.getSdkVersion());
  tft.println();

  /*
  if (!ts.begin()) { 
    Serial.println("Unable to start touchscreen.");
  } 
  else { 
    Serial.println("Touchscreen started."); 
  }
  ts.setRotation(0);
*/


  // start ticker to slow blink LED strip during Setup
  ticker.attach(0.6, tick);


  //
  // Start SPIFFS
  //
  tft.print("SPIFFS ... ");
  if (!SPIFFS.begin()) {
    tft.println("Error Starting");
  }
  else {
    tft.println("Started");
  }


  //
  // Set up the Wifi Connection
  //
  tft.print("WiFi ... ");
  WiFi.hostname(devicename);
  WiFi.mode(WIFI_STA);      // explicitly set mode, esp defaults to STA+AP
  WiFiManager wm;
  // wm.resetSettings();    // reset settings - for testing
  wm.setAPCallback(configModeCallback); //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  //if it does not connect it starts an access point with the specified name here  "AutoConnectAP"
  if (!wm.autoConnect(devicename,devicepassword)) {
    //Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(1000);
  }
  //Serial.println("connected");
  tft.println("Connected");
  tft.print("IP: ");
  tft.println(WiFi.localIP());
  tft.print("Hostname: ");
  tft.println(WiFi.getHostname());
  tft.println();


  //
  // Set up the Multicast DNS
  //
  tft.print("mDNS ");
  MDNS.begin(devicename);
  tft.println("Started");


  //
  // Set up OTA
  //
  tft.print("OTA  ");
  // ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(devicename);
  ArduinoOTA.setPassword(devicepassword);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    //Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      //Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      //Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      //Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      //Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      //Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  tft.println("Started");

  // Setup Telnet Serial
  //telnetSerial.begin(115200);
  //usbSerial = telnetSerial.getOriginalSerial();

  // Let USB/Hardware Serial know where to connect.
  //usbSerial->print("Ready! Use 'telnet ");
  //usbSerial->print(WiFi.localIP());
  //usbSerial->printf(" %d' to connect\n", TELNETSERIAL_DEFAULT_PORT);

  tft.print("PWM  ");
  pwm.begin();
  /*
   * In theory the internal oscillator (clock) is 25MHz but it really isn't
   * that precise. You can 'calibrate' this by tweaking this number until
   * you get the PWM update frequency you're expecting!
   * The int.osc. for the PCA9685 chip is a range between about 23-27MHz and
   * is used for calculating things like writeMicroseconds()
   * Analog servos run at ~50 Hz updates, It is importaint to use an
   * oscilloscope in setting the int.osc frequency for the I2C PCA9685 chip.
   * 1) Attach the oscilloscope to one of the PWM signal pins and ground on
   *    the I2C PCA9685 chip you are setting the value for.
   * 2) Adjust setOscillatorFrequency() until the PWM update frequency is the
   *    expected value (50Hz for most ESCs)
   * Setting the value here is specific to each individual I2C PCA9685 chip and
   * affects the calculations for the PWM update frequency. 
   * Failure to correctly set the int.osc value will cause unexpected PWM results
   */
  //pwm.setOscillatorFrequency(25000000);
  pwm.setPWMFreq(SERVO_FREQ);  // Analog servos run at ~50 Hz updates
  initServos();
  delay(500);
  //middleServos();
  tft.println("Started");

/*
  tft.println("SERVO Range Test ");
  servoRangeTest();
  middleServos();
  tft.println("Completed");
*/

  //
  // Setup WebServer
  //
  tft.print("Web Server ... ");
  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/mainpage.html", "text/html");
  });
  //server.on("/face", handleFace);
  server.on("/test", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/testpage.html", "text/html");
  });
  server.on("/servos", handleServos);
  server.onNotFound(handleNotFound);
  server.begin();
  tft.println("Started");

  //
  // Done with Setup
  //
  delay(2000);
  tft.println();
  ticker.detach();          // Stop blinking the LED
  digitalWrite(ledPin, false);     // turn off led
  tft.println("Setup Complete!");
  tft.println();

/*
  clearTftScreen()
  statusTopLeftX = tft.getCursorX();
  statusTopLeftY = tft.getCursorY();
  tft.fillRect(0,statusTopLeftY, screenWidth, DIVIDER_HEIGHT, DIVIDER_COLOR);
  statusTopLeftY += (DIVIDER_HEIGHT*2);
  statusHeight = screenHeight - statusTopLeftY;
*/
  drawScreen = true;
}


/*************************************************
 * Loop
 *************************************************/
void loop() {
  // Handle any requests
  ArduinoOTA.handle();
  //server.handleClient();  // Not needed with AsyncWebServer
  //MDNS.update();          // Not needed on ESP32
  //telnetSerial.handle();

  // Drive each servo one at a time using setPWM()
  //for (int angle = 0; angle < 181; angle+=10) {
  //  for(int s=0; s<9; s++) {
  //    pwm.setPWM(s, 0, map(angle, 0, 180, SERVOMIN, SERVOMAX));
  //  }
  //}
  delay(100);

  if (drawScreen) {
    drawServoStatus();
    drawScreen = false;
  }

}
