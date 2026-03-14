#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <IRremote.h>
#include <Preferences.h>

// --- Configuration ---
#define IR_SEND_PIN 13

// Reverse-engineered codes of the IR remote controller
#define IR_ADDRESS    0xDEA8
#define CMD_ON        0x01
#define CMD_OFF       0xFF
#define CMD_CINEMA    0x0A
#define CMD_6500K     0x16
#define CMD_4000K     0x0E
#define CMD_3000K     0x14
#define CMD_1H        0x6
#define CMD_60s       0x8
#define CMD_DimmUp    0x0
#define CMD_DimmDown  0xD
#define CMD_WarmLight 0x9
#define CMD_ColdLight 0x15


// WiFi Access Point Credentials
const char* ssid = "SlaveOne";
String password;
Preferences preferences;  

WebServer server(80);

// --- HTML PAGE ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 IR Remote</title>
<style>
body {font-family: Arial; text-align:center; margin-top:50px; background:#FFFFFF;}
.button{
display:block;
width:250px;
margin:20px auto;
padding:20px;
font-size:24px;
color:white;
background:#4CAF50;
border:none;
border-radius:10px;
}.button-off{background:#f44336;}
.button-cinemaMode{background:#303e93;}
.button-1H{background:#1abc9c;}
.button-60s{background:#9b59b6;}
.button-6500K{background:#ecf0f1; border: 2px solid black;}
.button-4000K{background:#f1c40f;}
.button-3000K{background:#e67e22;}
.button-DimmUp{background:#2980b9;}
.button-DimmDown{background:#1A202C;}
.button-coldLight{background:#CCFBF1;}
.button-warmLight{background:#FF5E57;}
</style>
</head>
<body>
<h1>IR Controller</h1>
<a href="/on"><button class="button">Turn ON</button></a>
<a href="/off"><button class="button button-off">Turn OFF</button></a>
<a href="/cinemaMode"><button class="button button-cinemaMode">Cinema Mode</button></a>
<a href="/1H"><button class="button button-1H">1H Timer</button></a>
<a href="/60s"><button class="button button-60s">60s Timer</button></a>
<a href="/6500K"><button class="button button-6500K">65000K</button></a>
<a href="/4000K"><button class="button button-4000K">4000K</button></a>
<a href="/3000K"><button class="button button-3000K">3000K</button></a>
<a href="/dimmUp"><button class="button button-DimmUp">Dimm Up</button></a>
<a href="/dimmDown"><button class="button button-DimmDown">Dimm Down</button></a>
<a href="/coldLight"><button class="button button-coldLight">Cold Light</button></a>
<a href="/warmLight"><button class="button button-warmLight">Warm Light</button></a>
</body>
</html>
)rawliteral";

// --- Web Server Functions ---

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleOn() {
  Serial.println("Sending ON command");
  IrSender.sendNEC(IR_ADDRESS, CMD_ON, 0);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleOff() {
  Serial.println("Sending OFF command");
  IrSender.sendNEC(IR_ADDRESS, CMD_OFF, 0);
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleCinemaMode() {
  Serial.println("Sending cinemaMode command");
  IrSender.sendNEC(IR_ADDRESS, CMD_CINEMA, 0);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handle1HTimer() {
  Serial.println("Sending 1H Timer command");
  IrSender.sendNEC(IR_ADDRESS, CMD_1H, 0);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handle60sTimer() {
  Serial.println("Sending 60s Timer command");
  IrSender.sendNEC(IR_ADDRESS, CMD_60s, 0);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handlelightTemp(int lightCode) {
  switch (lightCode) {
    case 1:
      Serial.println("Sending 6500K command");
      IrSender.sendNEC(IR_ADDRESS, CMD_6500K, 0);
      break;
    case 2:
      Serial.println("Sending 4000K command");
      IrSender.sendNEC(IR_ADDRESS, CMD_4000K, 0);
      break;
    case 3:
      Serial.println("Sending 3000K command");
      IrSender.sendNEC(IR_ADDRESS, CMD_3000K, 0);
      break;
    default:
      Serial.println("LightCode is not Correct!");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDimmUpAndDown(int dimmNum) {
  switch (dimmNum) {
    case 1:
      Serial.println("Sending Dimm Up command");
      IrSender.sendNEC(IR_ADDRESS, CMD_DimmUp, 0);
      break;
    case 2:
      Serial.println("Sending Dimm Down command");
      IrSender.sendNEC(IR_ADDRESS, CMD_DimmDown, 0);
      break;
    default:
      Serial.println("dimmNum is not Correct!");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWarmColdLight(int tempLight) {
  switch (tempLight) {
    case 1:
      Serial.println("Sending Cold Light command");
      IrSender.sendNEC(IR_ADDRESS, CMD_ColdLight, 0);
      break;
    case 2:
      Serial.println("Sending warm Light command");
      IrSender.sendNEC(IR_ADDRESS, CMD_WarmLight, 0);
      break;
    default:
      Serial.println("tempLight is not Correct!");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}



String generateRandomPassword(int length) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String newPassword = "";
  
  for (int i = 0; i < length; i++) {
    // esp_random() is the hardware random number generator of the ESP32
    int index = esp_random() % (sizeof(charset) - 1); 
    newPassword += charset[index];
  }
  return newPassword;
}



// --- Setup ---

void setup() {
  Serial.begin(115200);

  IrSender.begin(IR_SEND_PIN);
  Serial.println("IR Sender initialized");

  Serial.println("Starting WiFi Access Point...");

  WiFi.mode(WIFI_AP);
  
  preferences.begin("wifi_settings", false);

  String currBuildTime = String(__DATE__) + " " + String(__TIME__);

  String savedBuildTime = preferences.getString("build_time", "");
  
  password = preferences.getString("ap_pass", "");

  if (password == "" || savedBuildTime != currBuildTime) {
    Serial.println("New flash process detected! Generating a new password...");
    
    password = generateRandomPassword(16);

    preferences.putString("ap_pass", password);
    preferences.putString("build_time", currBuildTime);
  } else {
    Serial.println("Normal reboot detected. Password remains unchanged.");
  }


  
  WiFi.softAP(ssid, password.c_str());

  IPAddress IP = WiFi.softAPIP();

  Serial.println("================================");
  Serial.print("SSID: ");
  Serial.println(ssid);

  Serial.print("Password: ");
  Serial.println(password);

  Serial.print("IP Address: ");
  Serial.println(IP);
  Serial.println("================================");

  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/cinemaMode", handleCinemaMode);
  server.on("/1H",handle1HTimer);
  server.on("/60s",handle60sTimer);
  server.on("/6500K",[]() { handlelightTemp(1);});
  server.on("/4000K",[]() { handlelightTemp(2);});
  server.on("/3000K",[]() { handlelightTemp(3);});
  server.on("/dimmUp",[]() { handleDimmUpAndDown(1);});
  server.on("/dimmDown",[]() { handleDimmUpAndDown(2);});
  server.on("/coldLight",[]() { handleWarmColdLight(1);});
  server.on("/warmLight",[]() { handleWarmColdLight(2);});
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
}