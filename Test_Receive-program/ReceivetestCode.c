#include <Arduino.h>
#include <IRremote.h>

#define IR_RECEIVE_PIN 13

void setup() {
  Serial.begin(115200);
  
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  
  Serial.println("IR Receiver started...");
  Serial.println("Point the remote at the sensor and press a button!");
  Serial.println("-----------------------------------------------------------");
}

void loop() {
  // Check for incoming IR signal
  if (IrReceiver.decode()) {
    
    // Prints the protocol used (e.g., NEC, Sony, RC5)
    Serial.print("Protocol: ");
    Serial.println(IrReceiver.getProtocolString());

    // Prints the remote control address in hexadecimal (e.g., 0xDEA8)
    Serial.print("Address: 0x");
    Serial.println(IrReceiver.decodedIRData.address, HEX);
    
    // Prints the button command in hexadecimal (e.g., 0xFF or 0x01)
    Serial.print("Command: 0x");
    Serial.println(IrReceiver.decodedIRData.command, HEX);
    
    Serial.println("-----------------------------------------------------------");

    // Resume the receiver to listen for the next signal
    IrReceiver.resume(); 
  }
}