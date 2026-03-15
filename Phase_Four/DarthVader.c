#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <time.h>
#include <sys/time.h>

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

// Heartbeat & Radar Tracking Variables
unsigned long lastSeenESP2 = 0;
unsigned long lastSeenESP3 = 0;

// Tracks human presence for ESP
bool esp2HumanPresent = false; 
bool esp3HumanPresent = false; 

const unsigned long TIMEOUT_MS = 15000;

// Time Tracking
bool isTimeSet = false;

// Packet Types for ESP-NOW
#define MSG_TYPE_CMD  1
#define MSG_TYPE_TIME 2

// Structure that defines the type of message
typedef struct base_message {
  uint8_t msgType; 
} base_message;

// Structure to send commands
typedef struct cmd_message {
  uint8_t msgType; 
  uint8_t target;
  uint8_t cmd;
} cmd_message;

// Structure to sync time to Slaves
typedef struct time_message {
  uint8_t msgType; 
  uint32_t epochTime; 
} time_message;

// Structure to receive heartbeat AND radar status
typedef struct heartbeat_msg {
  uint8_t id;
  uint8_t humanPresent; // NEW: 1 = Human detected, 0 = No human
} heartbeat_msg;

cmd_message commandData;


// --- HTML PAGE ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>IR Remote</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, Arial, sans-serif; text-align:center; margin-top:50px; background:#FFFFFF; user-select: none; }
select {font-size: 20px; padding: 10px; margin-bottom: 20px; border-radius: 5px;}
.status-box { margin-bottom: 20px; padding: 10px; background: #f8f9fa; border-radius: 10px; display: inline-block; text-align: left; width: 90%; max-width: 400px;}
.status-box p { margin: 5px 0; font-weight: bold; font-size: 16px; }
.radar-badge { font-weight: normal; font-size: 14px; padding: 2px 6px; border-radius: 4px; background: #eee; }
.button{ display:block; width:250px; margin:15px auto; padding:20px; cursor: pointer; font-size:24px; color:white; background:#4CAF50; border:none; border-radius:10px; transition: transform 0.1s; }
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
window.onload = function() {
  var d = new Date();
  var url = '/settime?y=' + d.getFullYear() + '&mo=' + (d.getMonth()+1) + '&d=' + d.getDate() + '&h=' + d.getHours() + '&m=' + d.getMinutes() + '&s=' + d.getSeconds();
  fetch(url).then(response => console.log("Time synchronized with ESP32"));
};

function sendCmd(cmdId) {
  var targetId = document.getElementById("targetSel").value;
  fetch('/cmd?target=' + targetId + '&id=' + cmdId);
  if (navigator.vibrate) navigator.vibrate(50);
}

setInterval(function() {
  fetch('/status')
    .then(response => response.json())
    .then(data => {
      // Update ESP2 Status
      document.getElementById('status2').innerText = data.esp2 ? "Online" : "Offline";
      document.getElementById('status2').style.color = data.esp2 ? "green" : "red";
      document.getElementById('radar2').innerText = data.esp2 ? (data.esp2Human ? "Human Detected" : "Clear") : "Unknown";
      document.getElementById('radar2').style.color = data.esp2Human && data.esp2 ? "blue" : "gray";

      // Update ESP3 Status
      document.getElementById('status3').innerText = data.esp3 ? "Online" : "Offline";
      document.getElementById('status3').style.color = data.esp3 ? "green" : "red";
      document.getElementById('radar3').innerText = data.esp3 ? (data.esp3Human ? "Human Detected" : "Clear") : "Unknown";
      document.getElementById('radar3').style.color = data.esp3Human && data.esp3 ? "blue" : "gray";

      // Update Master Time
      document.getElementById('espTime').innerText = data.time;
      document.getElementById('espTime').style.color = data.time === "Not synced" ? "orange" : "blue";
    });
}, 1500); // Polling faster (1.5s) to see radar updates quickly
</script>
</head>
<body>
<h1>IR Controller</h1>
<div class="status-box">
  <p>Light 1 (ESP2): <span id="status2" style="color:red;">Checking...</span> | Radar: <span id="radar2" class="radar-badge">Unknown</span></p>
  <p>Light 2 (ESP3): <span id="status3" style="color:red;">Checking...</span> | Radar: <span id="radar3" class="radar-badge">Unknown</span></p>
  <hr style="border: 1px solid #ddd;">
  <p>Master Time: <span id="espTime" style="color:orange;">Checking...</span></p>
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


// Callback when data is received from Slaves (Heartbeat & Radar data)
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    // If the packet size matches our heartbeat structure (1 byte)
    if (len == sizeof(heartbeat_msg)) {
    heartbeat_msg hb;
    memcpy(&hb, incomingData, sizeof(hb));

    // to check which Humanpresent sensor are triggert.
    if (hb.id == 2) {
      lastSeenESP2 = millis();
      esp2HumanPresent = (hb.humanPresent == 1);
    }
    if (hb.id == 3) {
      lastSeenESP3 = millis();
      esp3HumanPresent = (hb.humanPresent == 1);
    }
  }
}

// Check if it is nighttime
bool isNight() {
  if (!isTimeSet) return false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  int hour = timeinfo.tm_hour;
  if (hour >= 19 || hour < 6) return true;
  return false;
}

// --- Web Server Endpoints ---
void handleRoot() { server.send(200, "text/html", index_html); }

void handleSetTime() {
  if (server.hasArg("h") && server.hasArg("m")) {
    struct tm t;
    t.tm_year = server.arg("y").toInt() - 1900;
    t.tm_mon = server.arg("mo").toInt() - 1;
    t.tm_mday = server.arg("d").toInt();
    t.tm_hour = server.arg("h").toInt();
    t.tm_min = server.arg("m").toInt();
    t.tm_sec = server.arg("s").toInt();
    t.tm_isdst = -1; 
    
    time_t timeSinceEpoch = mktime(&t);
    struct timeval tv = { .tv_sec = timeSinceEpoch };
    settimeofday(&tv, NULL);
    isTimeSet = true;
    
    Serial.printf(">>> Time synced via Smartphone: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);

    // Broadcast the exact time to all Slaves via ESP-NOW
    time_message timeMsg;
    timeMsg.msgType = MSG_TYPE_TIME;
    timeMsg.epochTime = (uint32_t)timeSinceEpoch;
    esp_now_send(broadcastAddress, (uint8_t *) &timeMsg, sizeof(timeMsg));
    Serial.println(">>> Time broadcasted to Slaves!");

    server.send(200, "text/plain", "Time synchronized");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleCommand() {
  if (server.hasArg("id") && server.hasArg("target")) {
    commandData.msgType = MSG_TYPE_CMD; 
    commandData.target = server.arg("target").toInt();
    uint8_t requestedCmd = server.arg("id").toInt();
    
    if (requestedCmd == CMD_ON && isNight()) {
      commandData.cmd = CMD_ON; 
    } else {
      commandData.cmd = requestedCmd; 
    }

    esp_now_send(broadcastAddress, (uint8_t *) &commandData, sizeof(commandData));
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleStatus() {
  unsigned long currentMillis = millis();
  bool isEsp2Online = (lastSeenESP2 != 0) && (currentMillis - lastSeenESP2 < TIMEOUT_MS);
  bool isEsp3Online = (lastSeenESP3 != 0) && (currentMillis - lastSeenESP3 < TIMEOUT_MS);
  
  String timeString = "Not synced";
  if (isTimeSet) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeStr[10];
      sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      timeString = String(timeStr);
    }
  }
  
  // Create JSON with Radar data included
  String json = "{";
  json += "\"esp2\": " + String(isEsp2Online ? "true" : "false") + ",";
  json += "\"esp3\": " + String(isEsp3Online ? "true" : "false") + ",";
  json += "\"esp2Human\": " + String(esp2HumanPresent ? "true" : "false") + ",";
  json += "\"esp3Human\": " + String(esp3HumanPresent ? "true" : "false") + ",";
  json += "\"time\": \"" + timeString + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

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
  server.on("/status", handleStatus); 
  server.on("/settime", handleSetTime); 
  server.begin();
  Serial.println("Web server started");
}

void loop() { server.handleClient(); }