#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <driver/i2s.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_adc_cal.h>

// Pin definitions
const int recordRedLEDPin = 33;        // Record LED pin
const int recordRedButtonPin = 32;     // Record button pin

const int playBlueLEDPin = 22;         // Play LED pin
const int playBlueButtonPin = 21;      // Play button pin

const int CSPin = 5;

const int sampleRate = 40000;
const int bufferSize = 1024;
bool recording = false;
bool playing = false; // New flag for playback

// Wi-Fi credentials
const char* ssid = "ErstHausaufgabenMachen";
const char* password = "baf.09021c0129";

// Server setup
AsyncWebServer server(80);

// Debounce and timer variables
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 30;

unsigned long recordLedOnTime = 0;
const unsigned long recordLedTimeout = 5000; // 10 seconds

unsigned long playLedOnTime = 0;
const unsigned long playLedTimeout = 5000; // 10 seconds

// Record LED variables
int recordLedState = LOW;
int recordButtonState;
int lastRecordButtonState = HIGH;
bool recordModeActive = false;

// Play LED variables
int playLedState = LOW;
int playButtonState;
int lastPlayButtonState = HIGH;
bool playModeActive = false;

// Function declarations
void readerTask(void *param);
void playerTask(void *param);
void setupServer();

// ADC and I2S configuration for recording
i2s_config_t i2s_config_record = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
  .sample_rate = sampleRate,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S_LSB,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 4,
  .dma_buf_len = bufferSize,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

// I2S configuration for playback
i2s_config_t i2s_config_playback = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = sampleRate,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S_LSB,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 4,
  .dma_buf_len = bufferSize,
  .use_apll = true, // Use APLL (Automatic Precision Level Lock) for clock generation
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

// I2S pin configuration for playback
i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_PIN_NO_CHANGE,  // No separate clock pin needed
  .ws_io_num = I2S_PIN_NO_CHANGE,   // No separate word select pin needed
  .data_out_num = 16,               // Data output pin (SIG)
  .data_in_num = I2S_PIN_NO_CHANGE  // Not used in playback
};

void setup() {
  Serial.begin(115200);
  delay(5000);  // Delay to allow serial connection to stabilize

  Serial.println("Starting setup...");

  // Initialize SD card
  if (!SD.begin(CSPin)) {
    Serial.println("SD card initialization failed!");
    while (true) {
      // Stay here forever as the SD card initialization is critical
      Serial.println("Please check the SD card and wiring.");
      delay(5000);
    }
  }
  Serial.println("SD card initialized successfully");

  // Wi-Fi connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected to Wi-Fi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Start the server
  setupServer();

  // Install and start I2S driver for recording
  esp_err_t i2s_err = i2s_driver_install(I2S_NUM_0, &i2s_config_record, 0, NULL);
  if (i2s_err != ESP_OK) {
    Serial.printf("I2S driver install failed for recording: %d\n", i2s_err);
    return;
  }

  // Set I2S ADC mode for recording
  i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_7);
  i2s_adc_enable(I2S_NUM_0);

  pinMode(recordRedButtonPin, INPUT_PULLUP);
  pinMode(recordRedLEDPin, OUTPUT);
  digitalWrite(recordRedLEDPin, recordLedState);

  pinMode(playBlueButtonPin, INPUT_PULLUP);
  pinMode(playBlueLEDPin, OUTPUT);
  digitalWrite(playBlueLEDPin, playLedState);

  Serial.println("Setup completed.");
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
          
          recording = !recording;
          if (recording) {
            Serial.println("Recording started...");
            if (xTaskCreatePinnedToCore(readerTask, "Reader Task", 12288, NULL, 1, NULL, 0) != pdPASS) {
              Serial.println("Failed to create reader task");
              recording = false; // Ensure it doesn't get stuck
              recordLedState = LOW;
              digitalWrite(recordRedLEDPin, recordLedState);
            }
          } else {
            Serial.println("Recording stopped.");
            delay(1000);
            recordLedState = LOW;
            digitalWrite(recordRedLEDPin, recordLedState);
          }
        }
      }
    }
  }

  // Automatically turn off record LED after timeout
  if (recordLedState == HIGH && (millis() - recordLedOnTime >= recordLedTimeout)) {
    recordLedState = LOW;
    digitalWrite(recordRedLEDPin, recordLedState);
    Serial.print("Record LED turned OFF after ");
    Serial.print((millis() - recordLedOnTime) / 1000);
    Serial.println(" seconds");

    // Reset record mode
    recordModeActive = false;
    recording = false;
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
          
          playing = !playing;
          if (playing) {
            Serial.println("Playback started...");
            if (xTaskCreatePinnedToCore(playerTask, "Player Task", 12288, NULL, 1, NULL, 0) != pdPASS) {
              Serial.println("Failed to create player task");
              playing = false; // Ensure it doesn't get stuck
              playLedState = LOW;
              digitalWrite(playBlueLEDPin, playLedState);
            }
          } else {
            Serial.println("Playback stopped.");
            delay(1000);
            playLedState = LOW;
            digitalWrite(playBlueLEDPin, playLedState);
          }
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
    playing = false;
  }

  lastPlayButtonState = playReading;  // Save current play button state
}

void writeWAVHeader(File &file, uint32_t dataSize) {
  typedef struct {
    char chunkID[4];
    uint32_t chunkSize;
    char format[4];
    char subchunk1ID[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char subchunk2ID[4];
    uint32_t subchunk2Size;
  } WAVHeader;

  WAVHeader header = {
    {'R', 'I', 'F', 'F'},
    36 + dataSize,
    {'W', 'A', 'V', 'E'},
    {'f', 'm', 't', ' '},
    16,
    1,
    1,
    sampleRate,
    sampleRate * 2,
    2,
    16,
    {'d', 'a', 't', 'a'},
    dataSize
  };

  file.write((uint8_t *)&header, sizeof(header));
}

void readerTask(void *param) {
  Serial.println("Reader task started");

  if (SD.exists("/recording.wav")) {
    SD.remove("/recording.wav");
  }

  File audioFile = SD.open("/recording.wav", FILE_WRITE);
  if (!audioFile) {
    Serial.println("Failed to open file for writing on SD card");
    vTaskDelete(NULL);
    return;
  }

  Serial.println("File opened successfully for writing");

  size_t totalDataSize = 0;

  // Write a placeholder WAV header with zero data size
  writeWAVHeader(audioFile, 0);

  while (recording) {
    int16_t buffer[bufferSize];
    size_t bytesRead;
    esp_err_t i2s_err = i2s_read(I2S_NUM_0, (char *)buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
    if (i2s_err != ESP_OK) {
      Serial.printf("I2S read failed: %d\n", i2s_err);
      break;
    }

    audioFile.write((uint8_t *)buffer, bytesRead);
    totalDataSize += bytesRead;

    // Debugging information
    Serial.printf("Bytes read: %zu, Total data size: %zu\n", bytesRead, totalDataSize);

    // Check if SD card is almost full
    if (SD.cardSize() - SD.usedBytes() < bufferSize) {
      Serial.println("Warning: SD card is almost full!");
      break;
    }
  }

  // Ensure the totalDataSize is valid before writing it to header
  if (totalDataSize > 0) {
    audioFile.seek(4); // Move to chunk size position
    uint32_t fileSize = totalDataSize + 36;
    audioFile.write((uint8_t *)&fileSize, sizeof(fileSize));

    audioFile.seek(40); // Move to data chunk size position
    audioFile.write((uint8_t *)&totalDataSize, sizeof(totalDataSize));
  } else {
    Serial.println("No data recorded, WAV file will not be updated.");
  }

  audioFile.close();
  Serial.println("Recording stopped, file closed");
  vTaskDelete(NULL);
}

void playerTask(void *param) {
  Serial.println("Player task started");

  // Uninstall I2S driver if it is already installed
  i2s_driver_uninstall(I2S_NUM_0);

  // Install and start I2S driver for playback
  esp_err_t i2s_err = i2s_driver_install(I2S_NUM_0, &i2s_config_playback, 0, NULL);
  if (i2s_err != ESP_OK) {
    Serial.printf("I2S driver install failed for playback: %d\n", i2s_err);
    vTaskDelete(NULL);
    return;
  }

  // Set I2S pin configuration
  i2s_err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (i2s_err != ESP_OK) {
    Serial.printf("I2S set pin failed: %d\n", i2s_err);
    vTaskDelete(NULL);
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);

  File audioFile = SD.open("/recording.wav", FILE_READ);
  if (!audioFile) {
    Serial.println("Failed to open file for reading on SD card");
    vTaskDelete(NULL);
    return;
  }

  Serial.println("File opened successfully for reading");

  audioFile.seek(44); // Skip WAV header

  while (playing) {
    int16_t buffer[bufferSize];
    size_t bytesRead = audioFile.read((uint8_t *)buffer, sizeof(buffer));
    if (bytesRead == 0) {
      Serial.println("End of file reached");
      break;
    }

    size_t bytesWritten;
    esp_err_t i2s_err = i2s_write(I2S_NUM_0, (char *)buffer, bytesRead, &bytesWritten, portMAX_DELAY);
    if (i2s_err != ESP_OK) {
      Serial.printf("I2S write failed: %d\n", i2s_err);
      break;
    }
  }

  audioFile.close();
  Serial.println("Playback stopped, file closed");
  vTaskDelete(NULL);
}

void setupServer() {
  // Route to serve the file
  server.on("/recording", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/recording.wav", "audio/wav");
  });

  // Start server
  server.begin();
}