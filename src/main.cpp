// Adapted from:
// https://randomnerdtutorials.com/esp32-websocket-server-arduino/
// https://m1cr0lab-esp32.github.io/remote-control-with-websocket/
// https://lastminuteengineers.com/handling-esp32-gpio-interrupts-tutorial/

// TODO: refactor TFT display code as optional, i.e. using define statement
// TODO: add project description


#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "pin_config.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <secrets.h>

#define DEBOUNCE_DELAY  250 // in milliseconds
#define KEY_BTN_PIN     14 // Key button is attached to GPIO pin 14

bool ledState = 0;
bool stateChanged = false;  // whether state of LED has changed or not

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
  html {
    font-family: Arial, Helvetica, sans-serif;
    text-align: center;
  }
  h1 {
    font-size: 1.8rem;
    color: white;
  }
  h2{
    font-size: 1.5rem;
    font-weight: bold;
    color: #143642;
  }
  .topnav {
    overflow: hidden;
    background-color: #143642;
  }
  body {
    margin: 0;
  }
  .content {
    padding: 30px;
    max-width: 600px;
    margin: 0 auto;
  }
  .card {
    background-color: #F8F7F9;;
    box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
    padding-top:10px;
    padding-bottom:20px;
  }
  .button {
    padding: 15px 50px;
    font-size: 24px;
    text-align: center;
    outline: none;
    color: #fff;
    background-color: #0f8b8d;
    border: none;
    border-radius: 5px;
    -webkit-touch-callout: none;
    -webkit-user-select: none;
    -khtml-user-select: none;
    -moz-user-select: none;
    -ms-user-select: none;
    user-select: none;
    -webkit-tap-highlight-color: rgba(0,0,0,0);
   }
   /*.button:hover {background-color: #0f8b8d}*/
   .button:active {
     /*background-color: #F8F7F9;*/
     box-shadow: 2 2px #CDCDCD;
     transform: translateY(2px);
   }
   .state {
     font-size: 1.5rem;
     color:#8c8c8c;
     font-weight: bold;
   }
  </style>
<title>ESP Web Server</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
</head>
<body>
  <div class="topnav">
    <h1>ESP WebSocket Server</h1>
  </div>
  <div class="content">
    <div class="card">
      <h2>Output - TFT Display</h2>
      <p class="state">state: <span id="state">%STATE%</span></p>
      <p><button id="button" class="button">Toggle</button></p>
    </div>
  </div>
<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  window.addEventListener('load', onLoad);
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
  }
  function onOpen(event) {
    console.log('Connection opened');
  }
  function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
  }
  function onMessage(event) {
    var state;
    if (event.data == "1"){
      state = "ON";
      document.getElementById('button').style.backgroundColor = "#2ece00";
    }
    else{
      state = "OFF";
      document.getElementById('button').style.backgroundColor = "#0f8b8d";
    }
    document.getElementById('state').innerHTML = state;
  }
  function onLoad(event) {
    initWebSocket();
    initButton();
  }
  function initButton() {
    document.getElementById('button').addEventListener('click', toggle);
  }
  function toggle(){
    websocket.send('toggle');
  }
</script>
</body>
</html>
)rawliteral";

void notifyClients() {
  ws.textAll(String(ledState));
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char*)data, "toggle") == 0) {
      ledState = !ledState;
      stateChanged = true;
      Serial.println("LED state is changed");
      notifyClients();
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    if (ledState){
      return "ON";
    }
    else{
      return "OFF";
    }
  }
  return String();
}

TFT_eSPI tft = TFT_eSPI();

// ----------------------------------------------------------------------------
// Definition of the Button component
// ----------------------------------------------------------------------------
struct Button {
    uint8_t  pin;
    uint32_t currentTime;
    uint32_t lastDebounceTime;
    bool     pressed;
    uint32_t numberKeyPresses;  // for DEBUG
};

Button key_btn = { KEY_BTN_PIN, 0, 0, false, 0};

void IRAM_ATTR key_btn_isr() {
  key_btn.currentTime = millis();
  if (millis() - key_btn.lastDebounceTime > DEBOUNCE_DELAY) {
      key_btn.pressed = true;
      key_btn.numberKeyPresses++;
      key_btn.lastDebounceTime = key_btn.currentTime;
  }
}

void setup() {
  // GPIO
  pinMode(key_btn.pin, INPUT);  

  // Interrupt setup
  attachInterrupt(KEY_BTN_PIN, key_btn_isr, FALLING);

  // Start TFT display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Serial port for debugging purposes
  delay(1000);
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi");
  tft.setCursor(0, 0, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("Connecting to WiFi ");
  tft.println(SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tft.print(".");
  }
  // Print ESP Local IP Address
  Serial.println(WiFi.localIP());
  tft.println("Connected");
  tft.print("Open ");
  tft.print(WiFi.localIP());
  delay(2000);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Start server
  server.begin();

  tft.setCursor(0, 0, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("LED: ");

  // initially, LED is OFF
  tft.setCursor(60, 0, 4);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.print("OFF");
}

void loop() {
  ws.cleanupClients();

  if (key_btn.pressed){
    // DEBUG: Serial.printf("Key button has been pressed %u times\n", key_btn.numberKeyPresses);
    ledState = !ledState;
    stateChanged = true;
    key_btn.pressed = false;
    // notify clients about this change
    Serial.println("LED state is changed by physical button");
    notifyClients();
  }

  // only update the state when it is actually changed
  if(stateChanged) {
    tft.setCursor(60, 0, 4);
    if (ledState){
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.print("ON   ");  // spaces to avoid overlapped texts, not a clean solution, try looking up padding
    }
    else{
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.print("OFF");
    }
    stateChanged = false;
  }
}