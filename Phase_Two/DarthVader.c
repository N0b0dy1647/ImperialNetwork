#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>

// --- Configuration ---
// IR Commands (sent via ESP-NOW to ESP2)
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

// ESP-NOW Broadcast Address (Sends to all ESPs in range)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

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
display:block; width:250px; margin:20px auto; padding:20px;
font-size:24px; color:white; background:#4CAF50; border:none; border-radius:10px;
}
.button-off{background:#f44336;}
.button-cinemaMode{background:#303e93;}
.button-1H{background:#1abc9c;}
.button-60s{background:#9b59b6;}
.button-6500K{background:#ecf0f1; border: 2px solid black; color:black;}
.button-4000K{background:#f1c40f;}
.button-3000K{background:#e67e22;}
.button-DimmUp{background:#2980b9;}
.button-DimmDown{background:#1A202C;}
.button-coldLight{background:#CCFBF1; color:black;}
.button-warmLight{background:#FF5E57;}
</style>
</head>
<body>
<h1>IR Controller</h1>
<a href="/cmd?id=1"><button class="button">Turn ON</button></a>
<a href="/cmd?id=255"><button class="button button-off">Turn OFF</button></a>
<a href="/cmd?id=10"><button class="button button-cinemaMode">Cinema Mode</button></a>
<a href="/cmd?id=6"><button class="button button-1H">1H Timer</button></a>
<a href="/cmd?id=8"><button class="button button-60s">60s Timer</button></a>
<a href="/cmd?id=22"><button class="button button-6500K">6500K</button></a>
<a href="/cmd?id=14"><button class="button button-4000K">4000K</button></a>
<a href="/cmd?id=20"><button class="button button-3000K">3000K</button></a>
<a href="/cmd?id=0"><button class="button button-DimmUp">Dimm Up</button></a>
<a href="/cmd?id=13"><button class="button button-DimmDown">Dimm Down</button></a>
<a href="/cmd?id=21"><button class="button button-coldLight">Cold Light</button></a>
<a href="/cmd?id=9"><button class="button button-warmLight">Warm Light</button></a>
</body>
</html>
)rawliteral";

// --- Helper Functions ---
String generateRandomPassword(int length) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String newPassword = "";
  for (int i = 0; i < length; i++) {
    int index = esp_random() % (sizeof(charset) - 1); 
    newPassword += charset[index];
  }
  return newPassword;
}

// Centralized function to send ESP-NOW commands
void sendCommandToESP2(uint8_t cmd) {
  esp_err_t result = esp_now_send(broadcastAddress, &cmd, sizeof(cmd));
  if (result == ESP_OK) {
    Serial.printf("Command sent: 0x%02X\n", cmd);
  } else {
    Serial.println("Error sending command!");
  }
}

// --- Web Server Endpoints ---
void handleRoot() {
  server.send(200, "text/html", index_html);
}

// Processes all button clicks via a single route (/cmd?id=XXX)
void handleCommand() {
  if (server.hasArg("id")) {
    uint8_t cmd = server.arg("id").toInt();
    sendCommandToESP2(cmd);
  }
  // Redirect back to the main page
  server.sendHeader("Location", "/");
  server.send(303);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  // WiFi as Access Point AND Station (Station mode is recommended for ESP-NOW)
  WiFi.mode(WIFI_AP_STA);
  
  // Password Management
  preferences.begin("wifi_settings", false);
  String currBuildTime = String(__DATE__) + " " + String(__TIME__);
  String savedBuildTime = preferences.getString("build_time", "");
  password = preferences.getString("ap_pass", "");

  if (password == "" || savedBuildTime != currBuildTime) {
    Serial.println("New flash process detected! Generating a new password...");
    password = generateRandomPassword(16);
    preferences.putString("ap_pass", password);
    preferences.putString("build_time", currBuildTime);
  }

  // Start AP (IMPORTANT: Fixed to channel 1 so ESP-NOW finds it easily)
  WiFi.softAP(ssid, password.c_str(), 1);
  IPAddress IP = WiFi.softAPIP();

  Serial.println("================================");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("IP Address: ");
  Serial.println(IP);
  Serial.println("================================");

  // ESP-NOW Initialization
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register Peer (ESP2)
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  // Web server routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCommand); // All buttons now route through here!
  
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
}