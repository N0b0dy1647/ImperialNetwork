// VERY IMPORTANT: This must be placed BEFORE the #includes to prevent the ESP32 crash!
#define S3KM1110_SKIP_READ_CONFIG_ON_BEGIN 

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <IRremote.h>
#include <s3km1110.h>
#include <time.h>
#include <sys/time.h>

// --- Configuration IR & ESP-NOW ---
#define IR_SEND_PIN 13
#define IR_ADDRESS  0xDEA8

#define CMD_ON  0x01
#define CMD_OFF 0xFF

// Set this to 2 for NodeTrooper 47, and to 3 for NodeTrooper 16!
const uint8_t MY_ID = 2; 

// --- Configuration Radar from s3km1110 Libary ---
#if defined(ESP32)
  #ifdef ESP_IDF_VERSION_MAJOR // IDF 4+
    #if CONFIG_IDF_TARGET_ESP32 // ESP32/PICO-D4
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial2
      #define RADAR_RX_PIN 16
      #define RADAR_TX_PIN 17
    #elif CONFIG_IDF_TARGET_ESP32S2
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial1
      #define RADAR_RX_PIN 9
      #define RADAR_TX_PIN 8
    #elif CONFIG_IDF_TARGET_ESP32C3
      #define MONITOR_SERIAL Serial
      #define RADAR_SERIAL Serial1
      #define RADAR_RX_PIN 4
      #define RADAR_TX_PIN 5
    #else
      #error Target CONFIG_IDF_TARGET is not supported
    #endif
  #else // ESP32 Before IDF 4.0
    #define MONITOR_SERIAL Serial
    #define RADAR_SERIAL Serial1
    #define RADAR_RX_PIN 32
    #define RADAR_TX_PIN 33
  #endif
#elif defined(__AVR_ATmega32U4__)
  #define MONITOR_SERIAL Serial
  #define RADAR_SERIAL Serial1
  #define RADAR_RX_PIN 0
  #define RADAR_TX_PIN 1
#endif

s3km1110 radar;

// --- Radar Tracking Variables ---
uint32_t lastReading = 0;
uint16_t lastDistance = 0;
bool isDetected = false;

// --- Automation & Time Variables ---
bool isTimeSet = false;
bool isLightOn = false;
bool autoTurnOnEnabled = true; 

// --- Async Command Variables to prevent ESP Crash ---
volatile bool hasNewCommand = false;
volatile uint8_t pendingCmd = 0;

// Packet Types
#define MSG_TYPE_CMD  1
#define MSG_TYPE_TIME 2

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 5000;

// ESP-NOW Data Structures
typedef struct base_message {
  uint8_t msgType; 
} base_message;

typedef struct cmd_message {
  uint8_t msgType; 
  uint8_t target;
  uint8_t cmd;
} cmd_message;

typedef struct time_message {
  uint8_t msgType; 
  uint32_t epochTime; 
} time_message;

typedef struct heartbeat_msg {
  uint8_t id;
  uint8_t humanPresent; // 1 = Human detected, 0 = Clear
} heartbeat_msg;

heartbeat_msg myHeartbeat;

// --- Time Check Function for the NodeTrooper ---
bool isNight() {
  if (!isTimeSet) return false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  
  int hour = timeinfo.tm_hour;
  if (hour >= 19 || hour < 6) return true;
  return false;
}

// --- Callback Function when receiving ESP-NOW data ---
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  base_message baseMsg;
  memcpy(&baseMsg, data, sizeof(baseMsg));

  // 1. If it is a TIME SYNC message
  if (baseMsg.msgType == MSG_TYPE_TIME && len == sizeof(time_message)) {
    time_message timeMsg;
    memcpy(&timeMsg, data, sizeof(timeMsg));
    
    struct timeval tv = { .tv_sec = timeMsg.epochTime };
    settimeofday(&tv, NULL);
    isTimeSet = true;
    
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    MONITOR_SERIAL.printf(">>> NodeTrooper Time Synced: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
  
  // 2. If it is a COMMAND message
  else if (baseMsg.msgType == MSG_TYPE_CMD && len == sizeof(cmd_message)) {
    cmd_message cmdMsg;
    memcpy(&cmdMsg, data, sizeof(cmdMsg));
    
    if (cmdMsg.target == MY_ID || cmdMsg.target == 0) {
      pendingCmd = cmdMsg.cmd;
      hasNewCommand = true; 
    }
  }
}

// Function to broadcast the heartbeat
void sendHeartbeatUpdate() {
  myHeartbeat.humanPresent = isDetected ? 1 : 0;
  esp_now_send(broadcastAddress, (uint8_t *) &myHeartbeat, sizeof(myHeartbeat));
  lastHeartbeatTime = millis(); 
}

void setup(void) {
  MONITOR_SERIAL.begin(115200);

  IrSender.begin(IR_SEND_PIN);
  MONITOR_SERIAL.printf("IR Sender initialized. My ID is: %d\n", MY_ID);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() == ESP_OK) {
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1; 
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    esp_now_register_recv_cb(OnDataRecv);
  } else {
    MONITOR_SERIAL.println("ESP-NOW Init failed!");
  }
  
  myHeartbeat.id = MY_ID;

  MONITOR_SERIAL.println("Waiting for radar to boot...");
  delay(2000);

  #if defined(ESP32)
  RADAR_SERIAL.begin(115200, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN); 
  #elif defined(__AVR_ATmega32U4__)
  RADAR_SERIAL.begin(115200); 
  #endif
  
  bool isRadarEnabled = false;
  for(int i=0; i<3; i++) {
      if(radar.begin(RADAR_SERIAL, MONITOR_SERIAL)) {
          isRadarEnabled = true;
          break;
      }
      MONITOR_SERIAL.println("Retrying connection...");
      delay(1000);
  }

  MONITOR_SERIAL.printf("Radar status: %s\n", isRadarEnabled ? "Ok" : "Failed");

  if (isRadarEnabled) {
      delay(500);
      if(radar.setRadarConfigurationMinimumGates(0)) {
         MONITOR_SERIAL.println("[Info] Minimum Gate set to 0.");
      }
      if(radar.setRadarConfigurationMaximumGates(4)) {
         MONITOR_SERIAL.println("[Info] Maximum Gate set to 4.");
      }
      radar.readAllRadarConfigs();
  }
}

void loop(void) {
  // --- 0. SAFELY PROCESS PENDING COMMANDS ---
  // This prevents the crash because the infrared signal is now safely sent inside the main loop!
  if (hasNewCommand) {
    uint8_t cmdToExecute = pendingCmd;
    hasNewCommand = false; // Reset the flag

    MONITOR_SERIAL.printf("Command received: 0x%02X -> Sending IR...\n", cmdToExecute);
    IrSender.sendNEC(IR_ADDRESS, cmdToExecute, 0);

    // --- SMART MANUAL OVERRIDE ---
    if (cmdToExecute == CMD_OFF) {
      isLightOn = false;
      autoTurnOnEnabled = false; 
      MONITOR_SERIAL.println("[MODE] Manual OFF received. Auto-ON paused.");
    } else {
      isLightOn = true;
      autoTurnOnEnabled = true;  
      MONITOR_SERIAL.println("[MODE] Manual ON/Action received. Auto-ON active.");
    }
  }

  // --- 1. Radar Read & Fast Automation Routine ---
  if (radar.read()) {
    bool newIsDetected = radar.isTargetDetected;
    uint16_t newDistance = radar.distanceToTarget;

    // Detect state changes
    if (isDetected && !newIsDetected) {
        MONITOR_SERIAL.printf("[INFO] Target lost (Last known: %ucm)\n", lastDistance);
        isDetected = newIsDetected;
        sendHeartbeatUpdate(); 
    } else if (!isDetected && newIsDetected) {
         MONITOR_SERIAL.println("[INFO] Target FOUND!");
         isDetected = newIsDetected;
         sendHeartbeatUpdate(); 
    }

    if (isDetected && (lastDistance != newDistance)) {
        MONITOR_SERIAL.printf("[INFO] Distance: %ucm\n", newDistance);
    }

    lastDistance = newDistance;

    // --- AUTOMATION LOGIC ---
    if (isDetected && (lastDistance <= 200) && isNight() && !isLightOn && autoTurnOnEnabled) {
      MONITOR_SERIAL.println("[AUTO] Human < 2m (Night) -> Turning Light ON");
      IrSender.sendNEC(IR_ADDRESS, CMD_ON, 0);
      isLightOn = true;
    }
    
    else if (!isDetected && isLightOn) {
      MONITOR_SERIAL.println("[AUTO] Human left -> Turning Light OFF");
      IrSender.sendNEC(IR_ADDRESS, CMD_OFF, 0);
      isLightOn = false;
    }
  }

  // --- 2. Radar Warning Check  ---
  static uint32_t nextWarning = 0;
  if (!radar.isActive() && millis() > nextWarning) {
    nextWarning = millis() + 5000;
    MONITOR_SERIAL.println("[WARN] Radar not sending data (Check wiring or power)");
  }

  // --- 3. Regular Heartbeat Loop ---
  if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    sendHeartbeatUpdate();
  }
}