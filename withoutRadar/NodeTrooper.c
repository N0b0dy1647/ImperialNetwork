#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <IRremote.h>

// --- Configuration ---
#define IR_SEND_PIN 13
#define IR_ADDRESS  0xDEA8

// IMPORTANT: Set this to 2 for NodeTrooper 47, and to 3 for NodeTrooper 16!
const uint8_t MY_ID = 2; 

// --- SECURITY & ENCRYPTION ---
uint8_t masterAddress[]; // Readout the Adress from ImperatorNode 

// 2. Paste the keys that were printed in the Master's Serial Monitor here!
const char* PMK_KEY = "<PMK FROM ImperatorNode>";
const char* LMK_KEY = "<LMK FROM ImperatorNode >"; 

esp_now_peer_info_t peerInfo;

// Heartbeat timing
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 5000; 

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

// Variables to safely pass data from callback to main loop
volatile bool newCommandReceived = false;
volatile uint8_t commandToSend = 0;

// Callback Function when receiving commands
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len == sizeof(struct_message)) {
    memcpy(&incomingData, data, sizeof(incomingData));
    
    if (incomingData.target == MY_ID || incomingData.target == 0) {
      commandToSend = incomingData.cmd;
      newCommandReceived = true;
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

  // --- NEW: Apply Security Settings ---
  // Set the PMK
  esp_now_set_pmk((uint8_t *)PMK_KEY);

  // Register the Master as a Secure Peer
  memcpy(peerInfo.peer_addr, masterAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = true; // Turn ON Encryption!
  memcpy(peerInfo.lmk, LMK_KEY, 16); // Apply the LMK
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add Master as secure peer");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  
  myHeartbeat.id = MY_ID;

  Serial.println("Ready and securely waiting for encrypted commands...");
}

void loop() {
  // 1. Send IR Command safely
  if (newCommandReceived) {
    Serial.printf("Command received -> Sending IR: 0x%02X\n", commandToSend);
    IrSender.sendNEC(IR_ADDRESS, commandToSend, 0);
    newCommandReceived = false; 
  }

  // 2. Send Encrypted Heartbeat to the Master directly (No more Broadcast!)
  if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = millis();
    // Send specifically to the Master's MAC
    esp_err_t result = esp_now_send(masterAddress, (uint8_t *) &myHeartbeat, sizeof(myHeartbeat));
    
    if (result != ESP_OK) {
      Serial.println("Failed to send Encrypted Heartbeat");
    }
  }
}