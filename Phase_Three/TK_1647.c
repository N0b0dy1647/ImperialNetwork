#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <IRremote.h>

// --- Configuration ---
#define IR_SEND_PIN 13
#define IR_ADDRESS  0xDEA8

// IMPORTANT: Set this to 2 for ESP2, and to 3 for ESP3!
const uint8_t MY_ID = 2; 

// --- ESP-NOW Variables ---
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// Heartbeat timing
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 5000; // Send heartbeat every 5 seconds

// Incoming Command Structure
typedef struct struct_message {
  uint8_t target;
  uint8_t cmd;
} struct_message;

// Outgoing Heartbeat Structure
typedef struct heartbeat_msg {
  uint8_t id;
} heartbeat_msg;

struct_message incomingData;
heartbeat_msg myHeartbeat;

// Callback Function when receiving commands
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  // Check if it's a command message (2 bytes)
  if (len == sizeof(struct_message)) {
    memcpy(&incomingData, data, sizeof(incomingData));
    
    // Check if meant for me or all
    if (incomingData.target == MY_ID || incomingData.target == 0) {
      Serial.printf("Command received for Target %d: 0x%02X -> Sending IR...\n", incomingData.target, incomingData.cmd);
      IrSender.sendNEC(IR_ADDRESS, incomingData.cmd, 0);
    }
  }
}

void setup() {
  Serial.begin(115200);

  IrSender.begin(IR_SEND_PIN);
  Serial.printf("IR Sender initialized. My ID is: %d\n", MY_ID);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register peer so we can send the heartbeat to the Master
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);
  
  // Prepare heartbeat data
  myHeartbeat.id = MY_ID;

  Serial.println("Ready and waiting for commands...");
}

void loop() {
  // Non-blocking timer to send a heartbeat every 5 seconds
  if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = millis();
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myHeartbeat, sizeof(myHeartbeat));
    
    if (result == ESP_OK) {
      Serial.println("Heartbeat sent to Master");
    } else {
      Serial.println("Failed to send Heartbeat");
    }
  }
}