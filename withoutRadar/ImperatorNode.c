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


const char* ssid = "ImperatorNode";
String password;
Preferences preferences;  

WebServer server(80);

// ESP-NOW Variables
uint8_t mac_esp2[] = // Readout the Adress from First Node 
uint8_t mac_esp3[] = // Readout the Adress from Second Node 
String PMK_KEY;
String LMK_KEY;
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
      // Update ESP3 Status
      document.getElementById('status3').innerText = data.esp3 ? "Online" : "Offline";
      document.getElementById('status3').style.color = data.esp3 ? "green" : "red";
    });
}, 3000);
</script>
</head>
<body>
<h1>IR Controller</h1>
<div class="status-box">
  <p>NodeTrooper 47: <span id="status2" style="color:red;">Checking...</span></p>
  <p>NodeTrooper 16: <span id="status3" style="color:red;">Checking...</span></p>
</div>
<br>
<select id="targetSel">
  <option value="0">All NodeTroopers</option>
  <option value="2">Only NodeTrooper 47</option>
  <option value="3">Only NodeTrooper 16</option>
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
    if (commandData.target == 2) {
        esp_now_send(mac_esp2, (uint8_t *) &commandData, sizeof(commandData));
    } else if (commandData.target == 3) {
        esp_now_send(mac_esp3, (uint8_t *) &commandData, sizeof(commandData));
    } else if (commandData.target == 0) {
        esp_now_send(mac_esp2, (uint8_t *) &commandData, sizeof(commandData));
        esp_now_send(mac_esp3, (uint8_t *) &commandData, sizeof(commandData));
    }
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
  // Create the keys to encrypt the traffic.
  preferences.begin("esp_security", false);
  PMK_KEY = preferences.getString("pmk", "");
  LMK_KEY = preferences.getString("lmk", "");    
  // If keys don't exist yet, generate and save them!
  if ((PMK_KEY == "" || LMK_KEY == "") || savedBuildTime != currBuildTime) {
    PMK_KEY = generateRandomPassword(16);
    LMK_KEY = generateRandomPassword(16);

    preferences.putString("pmk", PMK_KEY);
    preferences.putString("lmk", LMK_KEY);     
    Serial.println("\n================================================");
    Serial.println("NEW ENCRYPTION KEYS GENERATED!");
    Serial.println("You MUST copy these into your NodeTrooper code:");
    Serial.println("PMK: " + PMK_KEY);
    Serial.println("LMK: " + LMK_KEY);
    Serial.println("================================================\n");
  } else {
    Serial.println("Loaded existing ESP-NOW keys from memory.");
    Serial.println("PMK: " + PMK_KEY);
    Serial.println("LMK: " + LMK_KEY);
  }
  esp_now_set_pmk((uint8_t *)PMK_KEY.c_str());
  peerInfo.channel = 1;  
  peerInfo.encrypt = true;
  memcpy(peerInfo.lmk, LMK_KEY.c_str(), 16);
  memcpy(peerInfo.peer_addr, mac_esp2, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add NodeTrooper 47");
    return;
  }
  memcpy(peerInfo.peer_addr, mac_esp3, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add NodeTrooper 16");
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