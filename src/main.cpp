#include <Arduino.h>

const int ledPin = 33;        // LED pin
const int buttonPin = 32;     // Button pin

int ledState = LOW;           // Current LED state
int buttonState;              // Current button state
int lastButtonState = HIGH;   // Previous button state

unsigned long lastDebounceTime = 0;  // Last debounce time
const unsigned long debounceDelay = 30;  // Debounce delay

unsigned long ledOnTime = 0;          // Time when LED was last turned on
const unsigned long ledTimeout = 10000; // LED on time (10 seconds for testing)

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);  // Button pin as input with pull-up resistor
  pinMode(ledPin, OUTPUT);           // LED pin as output
  digitalWrite(ledPin, ledState);    // Initialize LED state
  Serial.begin(115200);              // Initialize serial communication
}

void loop() {
  int reading = digitalRead(buttonPin);

  // Check for button state change
  if (reading != lastButtonState) {
    lastDebounceTime = millis();  // Reset debounce timer
  }

  // If stable state for debounce delay, update button and LED states
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {  // Button pressed
        if (ledState == LOW) {  // Only toggle if LED is off
          ledState = HIGH;   // Turn on the LED
          ledOnTime = millis();  // Record time when LED was turned on
          Serial.print("LED turned ON at ");
          Serial.print((millis() - ledOnTime) / 1000); // Log elapsed seconds
          Serial.println(" seconds");
          digitalWrite(ledPin, ledState);
        }
      }
    }
  }

  // Automatically turn off LED after timeout
  if (ledState == HIGH && (millis() - ledOnTime >= ledTimeout)) {
    ledState = LOW;
    digitalWrite(ledPin, ledState);
    Serial.print("LED turned OFF after ");
    Serial.print((millis() - ledOnTime) / 1000); // Log elapsed seconds
    Serial.println(" seconds");
  }

  lastButtonState = reading;  // Save current button state
}
