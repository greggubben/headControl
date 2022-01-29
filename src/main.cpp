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
#include <XPT2046_Touchscreen.h>      // For display
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

#define TFT_CS  14  //for D32 Pro
#define TFT_DC  27  //for D32 Pro
#define TFT_RST 33  //for D32 Pro
#define TFT_LED 32  //for D32 Pro
#define TS_CS   12  //for D32 Pro

// Visual part of the Display
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Screen Display positions of interest
#define BACKGROUND_COLOR ILI9341_BLACK
#define DIVIDER_HEIGHT 3
#define DIVIDER_COLOR ILI9341_CYAN
#define SETUP_FONT_SIZE 1
#define SETUP_FONT_COLOR ILI9341_WHITE
#define SERVO_STATUS_FONT_SIZE 2
#define SERVO_STATUS_FONT_COLOR ILI9341_GREENYELLOW
#define SERVO_STATUS_DISABLED_FONT_COLOR ILI9341_LIGHTGREY
#define SERVO_STATUS_OUTRANGE_FONT_COLOR ILI9341_RED
#define SERVO_HEADER_FONT_COLOR ILI9341_GREEN
#define FACE_STATUS_FONT_COLOR ILI9341_CYAN
// Time to wait to dim or turn off screen
#define SCREEN_OFF_DELAY 600000   // 10 minutes
#define SCREEN_BRIGHTNESS_FULL 255
#define SCREEN_BRIGHTNESS_OFF 0

uint16_t screenHeight = 0;
uint16_t screenWidth = 0;
uint16_t statusTopLeftX = 0;
uint16_t statusTopLeftY = 0;
uint16_t statusHeight = 0;
// variables used for returning face to a resting state
int16_t  restStatusX = 0;
int16_t  restStatusY = 0;
uint16_t restStatusHeight = 0;
unsigned long restSeconds = 0;
// variables used for handling the websocket
int16_t  faceWebSocketStatusX = 0;
int16_t  faceWebSocketStatusY = 0;
uint16_t faceWebSocketStatusHeight = 0;
// variables used for showing screen only during activity
unsigned long lastActivity = 0;   // Timestamp of when the last activity happened in millis

bool screenOn = true;
bool drawScreen = false;


//
// Touchscreen definitions
//
// Touch response part of the display
XPT2046_Touchscreen ts(TS_CS);


//
// PWM definitions
//
// called this way, it uses the default address 0x40
#define I2C_SDA 21  // For D32 Pro
#define I2C_SCL 22  // For D32 Pro
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
AsyncWebServer webServer(80);

//
// Web Socket definitions
//
AsyncWebSocket faceWebSocketServer("/facews");
void notifyFaceClients();

//
// Head definitions
//
typedef struct headServo {  // Definition for Servo being used by the head
  uint8_t servoNum;         // Position number of the servo attached to the PWM
  bool    enabled;           // Indicated if this servo should be set
  String  name;             // Name for the servo
  uint8_t limitMinAngle;    // This is the minimum angle allowed for this servo
  uint8_t limitMaxAngle;    // This is the maximum angle allowed for this servo
  uint8_t angle;            // Expected current angle of the servo
  String  direction;        // Direction the servos move UD, DU, LR, RL
} headServo;

// The widest range for all the servos is 30 to 150
headServo left_eyebrow        = {0,  true, "LEFT  EYE BROW",  58, 102,  80, "DU"};
headServo right_eyebrow       = {1,  true, "RIGHT EYE BROW",  64, 114,  89, "UD"};
headServo left_eye_updown     = {2,  true, "LEFT  EYE U/D ",  70, 100,  85, "UD"};
headServo left_eye_leftright  = {3,  true, "LEFT  EYE SIDE",  50, 90,   63, "LR"};
headServo right_eye_updown    = {4,  true, "RIGHT EYE U/D ",  73, 123,  98, "UD"};
headServo right_eye_leftright = {5,  true, "RIGHT EYE SIDE",  59,  94,  76, "LR"};
headServo mouth               = {6,  true,     "MOUTH OPEN",  66,  88,  67, "DU"};
headServo neck_updown         = {7, false,      "NECK NOD ",  67,  77,  70, "UD"};
headServo neck_leftright      = {8, false,      "NECK TURN",  30, 100,  65, "LR"};

headServo *headServos[] = {&left_eyebrow, &right_eyebrow, &left_eye_updown, &left_eye_leftright, &right_eye_updown, &right_eye_leftright, &mouth, &neck_updown, &neck_leftright};
int headServosLen = sizeof(headServos) / sizeof(headServos[0]);


//
// Face Definitions
//
typedef struct face {     // Definition for Face to set head servos
  String name;            // Name for the face
  uint8_t angles[9];      // Angles for all 9 servos in the order of headServos[]
} face;

// List of Face definitions
face relaxed = {"Relaxed", { 80,  89,  85,  63,  98,  76,  67,  70,  65}};
face frown   = {"Frown",   { 60, 112,  85,  63,  98,  76,  67,  70,  65}};
face spock   = {"Spock",   { 80, 112,  85,  63,  98,  76,  67,  70,  65}};
face smile   = {"Smile",   { 75,  96,  96,  63, 106,  76,  78,  70,  65}};
face sad     = {"Sad",     { 93,  73,  75,  67,  89,  78,  67,  70,  65}};
face cross   = {"Cross",   { 66, 104,  78,  82,  88,  70,  67,  70,  65}};

face *faces[] = {&relaxed, &frown, &spock, &smile, &sad, &cross};
int facesLen = sizeof(faces) / sizeof(faces[0]);

const int defaultFace = 0;          // Default face to return to - start with Relaxed
int selectedFaceNum = defaultFace;  // Start with the Default face
const unsigned long showFaceDurationMillis = 2*60000; // How long to show a face before returning to default resting face
bool atRest = true;                             // Is the head at it's default resting face?
unsigned long lastFaceChangeMillis = 0;         // Last time the face changed

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
  tft.setRotation(1);
  tft.setTextSize(fontSize);
  tft.setTextColor(fontColor);
  tft.setCursor(0,0);
}


/*
 * Turn the screen on
 */
void turnScreenOn() {
  digitalWrite(TFT_LED, SCREEN_BRIGHTNESS_FULL);
  screenOn = true;
}

/*
 * Turn the screen off
 */
void turnScreenOff() {
  digitalWrite(TFT_LED, SCREEN_BRIGHTNESS_OFF);
  clearTftScreen(SERVO_STATUS_FONT_SIZE, SERVO_HEADER_FONT_COLOR);
  screenOn = false;
}


/*
 * Only draw the Rest countdown status
 * Must be called after drawStatus has been called at least once
 * so variables are set up properly.
 */
void drawRestStatus() {
  tft.setCursor(restStatusX, restStatusY);
  if (atRest) {
    tft.fillRect(restStatusX, restStatusY, screenWidth - restStatusX, restStatusHeight, BACKGROUND_COLOR);
    tft.print("Resting");
  }
  else {
    unsigned long timeElapsed = millis() - lastFaceChangeMillis;
    unsigned long timeLeftMillis = showFaceDurationMillis - timeElapsed;
    unsigned long timeLeftSeconds = timeLeftMillis/1000;
    if (restSeconds != timeLeftSeconds) {
      tft.fillRect(restStatusX, restStatusY, screenWidth - restStatusX, restStatusHeight, BACKGROUND_COLOR);
      tft.print(timeLeftSeconds);
      tft.print(" sec");
      restSeconds = timeLeftSeconds;
    }
  }

}

/*
 * Only draw the number of Face Web Socket Clients
 * Must be called after drawStatus has been called at least once
 * so variables are set up properly.
 */
void drawFaceWebSocketStatus() {
  tft.setCursor(faceWebSocketStatusX, faceWebSocketStatusY);
  size_t faceClients = faceWebSocketServer.count();
  tft.fillRect(faceWebSocketStatusX, faceWebSocketStatusY, screenWidth - faceWebSocketStatusX, faceWebSocketStatusHeight, BACKGROUND_COLOR);
  tft.print(faceClients);
}

/*
 * Draw the full screen status
 */
void drawStatus() {
  //tft.setCursor(statusTopLeftX, statusTopLeftY);
  //tft.setTextSize(SERVO_STATUS_FONT_SIZE);
  //tft.setTextColor(SERVO_STATUS_FONT_COLOR);
  //tft.fillRect(statusTopLeftX, statusTopLeftY, screenWidth, statusHeight, BACKGROUND_COLOR);
  clearTftScreen(SERVO_STATUS_FONT_SIZE, SERVO_HEADER_FONT_COLOR);
  
  int16_t textX;
  int16_t textY;
  uint16_t textWidth;
  uint16_t textHeight;

  tft.printf("%2s %16s %5s\n", "#", "NAME", "Angle");
  int16_t cursorX = tft.getCursorX();
  int16_t cursorY = tft.getCursorY();

  //tft.setTextColor(SERVO_HEADER_FONT_COLOR);
  tft.getTextBounds(" ", cursorX, cursorY, &textX, &textY, &textWidth, &textHeight);
  cursorY+=2;
  int16_t length = textWidth*2;
  tft.drawLine(cursorX, cursorY, cursorX+length, cursorY, SERVO_HEADER_FONT_COLOR);
  cursorX += length;
  cursorX += textWidth;
  length = textWidth*16;
  tft.drawLine(cursorX, cursorY, cursorX+length, cursorY, SERVO_HEADER_FONT_COLOR);
  cursorX += length;
  cursorX += textWidth;
  length = textWidth*5;
  tft.drawLine(cursorX, cursorY, cursorX+length, cursorY, SERVO_HEADER_FONT_COLOR);
  cursorY+=3;
  tft.setCursor(0, cursorY);

  for (headServo *hs : headServos) {
    //uint16_t currPwm = pwm.getPWM(hs->servoNum);
    if (hs->angle < hs->limitMinAngle || hs->limitMaxAngle < hs->angle) {
      tft.setTextColor(SERVO_STATUS_OUTRANGE_FONT_COLOR);
    }
    else if (hs->enabled) {
      tft.setTextColor(SERVO_STATUS_FONT_COLOR);
    }
    else {
      tft.setTextColor(SERVO_STATUS_DISABLED_FONT_COLOR);
    }
    tft.printf("%2d %16s %5d\n", hs->servoNum, hs->name.c_str(), hs->angle);
    //tft.ptintln();
  }

  cursorY = tft.getCursorY();
  cursorY -= (textHeight/2);
  tft.setCursor(0, cursorY);

  tft.setTextColor(FACE_STATUS_FONT_COLOR);
  tft.println();
  face *selectedFace = faces[selectedFaceNum];
  tft.printf("Face: %2d - %s\n", selectedFaceNum, selectedFace->name.c_str());

  tft.print("Clients:   ");
  faceWebSocketStatusX = tft.getCursorX(); 
  faceWebSocketStatusY = tft.getCursorY();
  faceWebSocketStatusHeight = textHeight;
  tft.println();

  tft.print("Rest in:   ");
  restStatusX = tft.getCursorX(); 
  restStatusY = tft.getCursorY();
  restStatusHeight = textHeight;
  //tft.println();

  // Dynamic status values can only be updated after the boilerplate is drawn
  drawFaceWebSocketStatus();
  drawRestStatus();
}


/*************************************************
 * Servo Functions
 *************************************************/

void setAngle (headServo *hs, int angle, bool limit = true) {
  if (angle < 0)   angle = 0;
  if (angle > 180) angle = 180;
  if (limit && angle < hs->limitMinAngle) angle = hs->limitMinAngle;
  if (limit && angle > hs->limitMaxAngle) angle = hs->limitMaxAngle;
  if (hs->enabled) {
    uint16_t pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
    pwm.setPWM(hs->servoNum, 0, pulse);
    hs->angle = angle;
  }
  drawScreen = true;
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
// Send the current status of the Servos
//
void sendServos(AsyncWebServerRequest *request) {
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
    jsonServoStatus["enabled"] = headServos[s]->enabled;
  }

  String payload;
  serializeJson(jsonDoc, payload);
  request->send(200, "application/json", payload);

}


/*************************************************
 * Face Functions
 *************************************************/


//
// Set servos to a new face
//
void _setFace (face *newFace) {
  for (headServo *hs : headServos) {
    setAngle(hs, newFace->angles[hs->servoNum]);
  }
  atRest = false;
  lastFaceChangeMillis = millis();
  notifyFaceClients();
}

void setFace (int faceNum) {
  if (0 <= faceNum && faceNum < facesLen) {
    selectedFaceNum = faceNum;
    face *newFace = faces[selectedFaceNum];
    _setFace(newFace);
  }
}

void setDefaultFace() {
  setFace(defaultFace);
  atRest = true;        // This is the At Rest face
}

//
// Build JSON object of Face data
//
void buildFaceJson(DynamicJsonDocument *jsonDoc) {
  JsonObject jsonRoot = jsonDoc->to<JsonObject>();

  JsonArray jsonFaceArray = jsonRoot.createNestedArray("faces");

  for (int f=0; f<facesLen; f++) {
    JsonObject jsonFaceStatus = jsonFaceArray.createNestedObject();
    jsonFaceStatus["faceNum"] = f;
    jsonFaceStatus["name"] = faces[f]->name;
    jsonFaceStatus["selected"] = (f == selectedFaceNum);
  }

}


/*************************************************
 * Web Server Routines
 *************************************************/

//
// Send the list of Faces
//
void sendFaces(AsyncWebServerRequest *request) {
  DynamicJsonDocument jsonDoc(2048);
  buildFaceJson(&jsonDoc);

  String payload;
  serializeJson(jsonDoc, payload);
  request->send(200, "application/json", payload);
}

void handleFace (AsyncWebServerRequest *request) {
  uint8_t faceNum = 0;
  switch (request->method()) {
    case HTTP_PUT:
      if (request->hasArg("faceNum")) {
        faceNum = request->arg("faceNum").toInt();
        if (0 <= faceNum && faceNum < facesLen) {
          setFace(faceNum);
          sendFaces(request);
        }
        else {
          request->send(400, "text/plain", "Argument 'faceNum' out of range.");
        }
      }
      else {
        request->send(400, "text/plain", "Argument 'faceNum' missing.");
      }
      break;
    case HTTP_GET:
      sendFaces(request);
      break;
    default:
      request->send(405, "text/plain", "Method Not Allowed");
      break;
  }
  drawScreen = true;
}


//
// Handle service for Servos 
//
void handleServos (AsyncWebServerRequest *request) {
  uint8_t servoNum = 0;
  uint8_t angle = 0;
  bool limit = true;
  switch (request->method()) {
    case HTTP_PUT:
      if (request->hasArg("servoNum") && request->hasArg("angle")) {
        servoNum = request->arg("servoNum").toInt();
        angle = request->arg("angle").toInt();
        if (request->hasArg("override")) limit = false;
        if (0 <= servoNum && servoNum < headServosLen) {
          headServo *hs = headServos[servoNum];
          setAngle(hs, angle, limit);
        }
        else {
          request->send(400, "text/plain", "Argument 'servoNum' out of range.");
        }
      }
      else if (request->hasArg("middle")) {
        middleServos();
      }
      else if (request->hasArg("default")) {
        setDefaultFace();
      }
      else {
        request->send(400, "text/plain", "Argument 'servoNum' or 'angle' is missing.");
      }
      sendServos(request);
      break;
    case HTTP_GET:
      sendServos(request);
      break;
    default:
      request->send(405, "text/plain", "Method Not Allowed");
      break;
  }
  drawScreen = true;
}

//
// Display a Not Found page
//
void handleNotFound(AsyncWebServerRequest *request) {
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
}


/*************************************************
 * Web Socket Routines
 *************************************************/

//
// Send the list of Faces
//
void notifyFaceClients() {
  DynamicJsonDocument jsonDoc(2048);
  buildFaceJson(&jsonDoc);

  String payload;
  serializeJson(jsonDoc, payload);
  faceWebSocketServer.textAll(payload);
}


//
// Handle events raised on Web Socket
//
void onFaceWebSocketEvent (AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      //Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      drawFaceWebSocketStatus();
      break;
    case WS_EVT_DISCONNECT:
      //Serial.printf("WebSocket client #%u disconnected\n", client->id());
      drawFaceWebSocketStatus();
      break;
    case WS_EVT_DATA:
      //handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}


/*************************************************
 * Setup
 *************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println(ESP.getSdkVersion());

  // start ticker to slow blink LED during Setup
  ticker.attach(0.6, tick);

  // set the digital pin as output:
  pinMode(ledPin, OUTPUT);
  pinMode(TFT_LED, OUTPUT);
  turnScreenOn();


  //
  // Start the screen so startup status can be displayed
  //
  tft.begin();
  clearTftScreen(SETUP_FONT_SIZE, SETUP_FONT_COLOR);
  screenWidth = tft.width();
  screenHeight = tft.height();

  tft.println(ESP.getSdkVersion());
  tft.println();

  
  //
  // Start Touch Screen
  //
  tft.print("Touch Screen ... ");
  if (!ts.begin()) { 
    tft.println("Error Starting");
  } 
  else { 
    tft.println("Started"); 
  }
  ts.setRotation(0);


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


  //
  // Set up the PWM board
  //
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
  setDefaultFace();
  delay(500);
  tft.println("Started");


  //
  // Setup WebServer
  //
  tft.print("Web Server ... ");
  // Main page to select a Face to show on the Head
  webServer.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
    drawScreen = true;
    request->send(SPIFFS, "/mainpage.html", "text/html");
  });
  // Web Service to display selected face on the head
  // and return face state
  webServer.on("/face", handleFace);

  // Test page to move the servos independently
  webServer.on("/test", HTTP_GET, [] (AsyncWebServerRequest *request) {
    drawScreen = true;
    request->send(SPIFFS, "/testpage.html", "text/html");
  });
  // Web Service to set a single servo to a specific angle
  // and return state of all servos
  webServer.on("/servos", handleServos);
  
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  tft.println("Started");


  //
  // Setup WebSocket
  //
  tft.print("Web Socket ... ");
  faceWebSocketServer.onEvent(onFaceWebSocketEvent);
  webServer.addHandler(&faceWebSocketServer);
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

  // Indicate the screen now needs to be drawn
  drawScreen = true;
}


/*************************************************
 * Loop
 *************************************************/
void loop() {
  // Handle any requests
  ArduinoOTA.handle();
  faceWebSocketServer.cleanupClients();
  //server.handleClient();  // Not needed with AsyncWebServer
  //MDNS.update();          // Not needed on ESP32

  bool isTouched = ts.touched();
  if (isTouched) {
    drawScreen = true;
  }

  // Is it time to turn off the screen?
  if ((screenOn) && ((millis() - lastActivity) > SCREEN_OFF_DELAY)) {
    turnScreenOff();
  }

  // See if it ia time to rest
  if (!atRest) {
    if((millis() - lastFaceChangeMillis) > showFaceDurationMillis) {
      setDefaultFace();
    }
    else {
      drawRestStatus();
    }
  }

  // Does the screen need to be redrawn
  if (drawScreen) {
    turnScreenOn();
    drawStatus();
    lastActivity = millis();
    drawScreen = false;
  }

  // slow down the loop a little since most activity is covered by event processing
  delay(50);
}
