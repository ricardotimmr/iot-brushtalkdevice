#include <Arduino.h>

// Pin definitions
const int recordRedLEDPin = 33;        // Record LED pin
const int recordRedButtonPin = 32;     // Record button pin

const int playBlueLEDPin = 18;         // Play LED pin
const int playBlueButtonPin = 19;      // Play button pin

// Record LED variables
int recordLedState = LOW;           // Current state of record LED
int recordButtonState;              // Current state of record button
int lastRecordButtonState = HIGH;   // Previous state of record button

// Play LED variables
int playLedState = LOW;             // Current state of play LED
int playButtonState;                // Current state of play button
int lastPlayButtonState = HIGH;     // Previous state of play button

unsigned long lastDebounceTime = 0;  // Last debounce time
const unsigned long debounceDelay = 30;  // Debounce delay

unsigned long recordLedOnTime = 0;          // Time when record LED was last turned on
const unsigned long recordLedTimeout = 10000; // Record LED timeout (10 seconds)

unsigned long playLedOnTime = 0;            // Time when play LED was last turned on
const unsigned long playLedTimeout = 10000; // Play LED timeout (10 seconds)

bool recordModeActive = false; // Track if record mode is active
bool playModeActive = false;   // Track if play mode is active

void setup() {
  pinMode(recordRedButtonPin, INPUT_PULLUP);  // Record button pin as input with pull-up resistor
  pinMode(recordRedLEDPin, OUTPUT);           // Record LED pin as output
  digitalWrite(recordRedLEDPin, recordLedState);    // Initialize record LED state

  pinMode(playBlueButtonPin, INPUT_PULLUP);   // Play button pin as input with pull-up resistor
  pinMode(playBlueLEDPin, OUTPUT);            // Play LED pin as output
  digitalWrite(playBlueLEDPin, playLedState); // Initialize play LED state

  Serial.begin(115200);              // Initialize serial communication
}

void loop() {
  // Handle record button
  int recordReading = digitalRead(recordRedButtonPin);
  if (recordReading != lastRecordButtonState) {
    lastDebounceTime = millis();  // Reset debounce timer
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (recordReading != recordButtonState) {
      recordButtonState = recordReading;
      if (recordButtonState == LOW && !playModeActive) {  // Record button pressed and play mode is not active
        if (recordLedState == LOW) {  // Only toggle if record LED is off
          recordLedState = HIGH;   // Turn on the record LED
          recordLedOnTime = millis();  // Record time when record LED was turned on
          Serial.print("Record LED turned ON at ");
          Serial.print((millis() - recordLedOnTime) / 1000); // Log elapsed seconds
          Serial.println(" seconds");
          digitalWrite(recordRedLEDPin, recordLedState);

          // Set modes
          recordModeActive = true;
          playModeActive = false; // Ensure play mode is not active
          digitalWrite(playBlueLEDPin, LOW); // Turn off play LED
        }
      }
    }
  }

  // Automatically turn off record LED after timeout
  if (recordLedState == HIGH && (millis() - recordLedOnTime >= recordLedTimeout)) {
    recordLedState = LOW;
    digitalWrite(recordRedLEDPin, recordLedState);
    Serial.print("Record LED turned OFF after ");
    Serial.print((millis() - recordLedOnTime) / 1000); // Log elapsed seconds
    Serial.println(" seconds");

    // Reset record mode
    recordModeActive = false;
  }

  lastRecordButtonState = recordReading;  // Save current record button state

  // Handle play button
  int playReading = digitalRead(playBlueButtonPin);
  if (playReading != lastPlayButtonState) {
    lastDebounceTime = millis();  // Reset debounce timer
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (playReading != playButtonState) {
      playButtonState = playReading;
      if (playButtonState == LOW && !recordModeActive) {  // Play button pressed and record mode is not active
        if (playLedState == LOW) {  // Only toggle if play LED is off
          playLedState = HIGH;   // Turn on the play LED
          playLedOnTime = millis();  // Record time when play LED was turned on
          Serial.print("Play LED turned ON at ");
          Serial.print((millis() - playLedOnTime) / 1000); // Log elapsed seconds
          Serial.println(" seconds");
          digitalWrite(playBlueLEDPin, playLedState);

          // Set modes
          playModeActive = true;
          recordModeActive = false; // Ensure record mode is not active
          digitalWrite(recordRedLEDPin, LOW); // Turn off record LED
        }
      }
    }
  }

  // Automatically turn off play LED after timeout
  if (playLedState == HIGH && (millis() - playLedOnTime >= playLedTimeout)) {
    playLedState = LOW;
    digitalWrite(playBlueLEDPin, playLedState);
    Serial.print("Play LED turned OFF after ");
    Serial.print((millis() - playLedOnTime) / 1000); // Log elapsed seconds
    Serial.println(" seconds");

    // Reset play mode
    playModeActive = false;
  }

  lastPlayButtonState = playReading;  // Save current play button state
}