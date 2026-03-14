#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 
#include <IRremote.h>

// --- Configuration ---
#define IR_SEND_PIN 13
#define IR_ADDRESS  0xDEA8

// Callback function: Executed when ESP1 sends a command
// Updated the parameters to be compatible with newer ESP32 Core versions
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len == 1) { // We expect exactly 1 byte (the command)
    uint8_t cmd = incomingData[0];
    Serial.printf("Command received: 0x%02X -> Sending IR signal...\n", cmd);
    
    // Execute IR command
    IrSender.sendNEC(IR_ADDRESS, cmd, 0);
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize IR Sender
  IrSender.begin(IR_SEND_PIN);
  Serial.println("IR Sender initialized");

  // Switch WiFi to Station mode (required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  
  // Fix WiFi channel to 1 so it finds ESP1 (AP) on the same channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // ESP-NOW Initialization
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("ESP2 is ready and waiting for commands from ESP1...");
}

void loop() {
  // Nothing to do here, everything is event-driven in the OnDataRecv callback!
}