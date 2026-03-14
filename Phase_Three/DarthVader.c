#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>

// --- Configuration ---
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

const char* ssid = "SlaveOne";
String password;
Preferences preferences;  

WebServer server(80);

// ESP-NOW Variables
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// Heartbeat Tracking Variables
unsigned long lastSeenESP2 = 0;
unsigned long lastSeenESP3 = 0;
const unsigned long TIMEOUT_MS = 15000; // 15 seconds without heartbeat = offline

// Structure to send commands
typedef struct struct_message {
  uint8_t target;
  uint8_t cmd;
} struct_message;

// Structure to receive heartbeat
typedef struct heartbeat_msg {
  uint8_t id;
} heartbeat_msg;

struct_message commandData;


// --- HTML PAGE ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>IR Remote</title>

<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="IR Remote">

<link rel="apple-touch-icon" href="data:image/svg+xml;charset=utf-8,%3Csvg xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22 viewBox%3D%220 0 100 100%22%3E%3Crect width%3D%22100%22 height%3D%22100%22 rx%3D%2220%22 fill%3D%22%234CAF50%22%2F%3E%3Ctext y%3D%2250%25%22 x%3D%2250%25%22 dy%3D%22.35em%22 text-anchor%3D%22middle%22 font-family%3D%22Arial%22 font-size%3D%2240%22 font-weight%3D%22bold%22 fill%3D%22%23fff%22%3EIR%3C%2Ftext%3E%3C%2Fsvg%3E">

<style>
/* Added code to prevent text selection and highlight effects on mobile */
body {
  font-family: -apple-system, BlinkMacSystemFont, Arial, sans-serif; 
  text-align:center; 
  margin-top:50px; 
  background:#FFFFFF;
  -webkit-touch-callout: none; /* iOS Safari */
  -webkit-user-select: none;   /* Safari */
  user-select: none;           /* Standard syntax */
}
select {font-size: 20px; padding: 10px; margin-bottom: 20px; border-radius: 5px;}
.status-box { margin-bottom: 20px; padding: 10px; background: #f8f9fa; border-radius: 10px; display: inline-block; text-align: left;}
.status-box p { margin: 5px 0; font-weight: bold; font-size: 18px; }
.button{
display:block; width:250px; margin:15px auto; padding:20px; cursor: pointer;
font-size:24px; color:white; background:#4CAF50; border:none; border-radius:10px;
/* Smooth active state for better app feel */
transition: transform 0.1s;
}
.button:active {transform: scale(0.95);}
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
<script>
function sendCmd(cmdId) {
  var targetId = document.getElementById("targetSel").value;
  fetch('/cmd?target=' + targetId + '&id=' + cmdId);
  // Add a small haptic feedback if supported (iOS doesn't support this via web yet, but good practice)
  if (navigator.vibrate) navigator.vibrate(50);
}

setInterval(function() {
  fetch('/status')
    .then(response => response.json())
    .then(data => {
      document.getElementById('status2').innerText = data.esp2 ? "Online" : "Offline";
      document.getElementById('status2').style.color = data.esp2 ? "green" : "red";
      document.getElementById('status3').innerText = data.esp3 ? "Online" : "Offline";
      document.getElementById('status3').style.color = data.esp3 ? "green" : "red";
    });
}, 3000);
</script>
</head>
<body>
<h1>IR Controller</h1>

<div class="status-box">
  <p>Light 1 (ESP2): <span id="status2" style="color:red;">Checking...</span></p>
  <p>Light 2 (ESP3): <span id="status3" style="color:red;">Checking...</span></p>
</div>
<br>

<select id="targetSel">
  <option value="0">All Lights (ESP2 & ESP3)</option>
  <option value="2">Only Light 1 (ESP 2)</option>
  <option value="3">Only Light 2 (ESP 3)</option>
</select>

<button class="button" onclick="sendCmd(1)">Turn ON</button>
<button class="button button-off" onclick="sendCmd(255)">Turn OFF</button>
<button class="button button-cinemaMode" onclick="sendCmd(10)">Cinema Mode</button>
<button class="button button-1H" onclick="sendCmd(6)">1H Timer</button>
<button class="button button-60s" onclick="sendCmd(8)">60s Timer</button>
<button class="button button-6500K" onclick="sendCmd(22)">6500K</button>
<button class="button button-4000K" onclick="sendCmd(14)">4000K</button>
<button class="button button-3000K" onclick="sendCmd(20)">3000K</button>
<button class="button button-DimmUp" onclick="sendCmd(0)">Dimm Up</button>
<button class="button button-DimmDown" onclick="sendCmd(13)">Dimm Down</button>
<button class="button button-coldLight" onclick="sendCmd(21)">Cold Light</button>
<button class="button button-warmLight" onclick="sendCmd(9)">Warm Light</button>
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

// Callback when data is received from Slaves (Heartbeat)
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  // If the packet size matches our heartbeat structure (1 byte)
  if (len == sizeof(heartbeat_msg)) {
    heartbeat_msg hb;
    memcpy(&hb, incomingData, sizeof(hb));
    
    // Update the last seen timestamp
    if (hb.id == 2) lastSeenESP2 = millis();
    if (hb.id == 3) lastSeenESP3 = millis();
  }
}

// --- Web Server Endpoints ---
void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleCommand() {
  if (server.hasArg("id") && server.hasArg("target")) {
    commandData.target = server.arg("target").toInt();
    commandData.cmd = server.arg("id").toInt();
    esp_now_send(broadcastAddress, (uint8_t *) &commandData, sizeof(commandData));
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// Endpoint returning JSON status for the JS fetch routine
void handleStatus() {
  unsigned long currentMillis = millis();
  bool isEsp2Online = (lastSeenESP2 != 0) && (currentMillis - lastSeenESP2 < TIMEOUT_MS);
  bool isEsp3Online = (lastSeenESP3 != 0) && (currentMillis - lastSeenESP3 < TIMEOUT_MS);
  
  String json = "{";
  json += "\"esp2\": " + String(isEsp2Online ? "true" : "false") + ",";
  json += "\"esp3\": " + String(isEsp3Online ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);
  
  preferences.begin("wifi_settings", false);
  String currBuildTime = String(__DATE__) + " " + String(__TIME__);
  String savedBuildTime = preferences.getString("build_time", "");
  password = preferences.getString("ap_pass", "");

  if (password == "" || savedBuildTime != currBuildTime) {
    password = generateRandomPassword(16);
    preferences.putString("ap_pass", password);
    preferences.putString("build_time", currBuildTime);
  }

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

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register peer for sending commands
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  // Register callback for receiving heartbeats
  esp_now_register_recv_cb(OnDataRecv);

  server.on("/", handleRoot);
  server.on("/cmd", handleCommand);
  server.on("/status", handleStatus); // New endpoint for JS checking
  
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
}