#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <FS.h>
#include <driver/i2s.h>
#include <driver/adc.h>

// Pin definitions
const int recordRedLEDPin = 33;     // Record LED pin
const int recordRedButtonPin = 32;  // Record button pin
const int playBlueLEDPin = 22;      // Play LED pin
const int playBlueButtonPin = 21;   // Play button pin
const int CSPin = 5;                // Chip select pin for the SD card module

// Wi-Fi credentials
const char *ssid = "FRITZ!Box 7590 YH"; // Replace with your Wi-Fi SSID
const char *password = "85050155753107747314"; // Replace with your Wi-Fi password

// ngrok static domain
const String serverURL = "https://8ea3035bbf6b75532a241ec23480c4ee.serveo.net";  // Replace with your static Serveo URL

// Audio settings
const int sampleRate = 44100;
const int bufferSize = 1024;
const int bitsPerSample = 16;
const int channels = 1; // Mono

// File management
const String recordedFilePath = "/uploaded_audio_device1.wav";
const String downloadedFilePath = "/uploaded_audio_device2.wav";
const String checkFileURL = "/check/device2"; // Endpoint to check for new audio files from device2

// I2S configurations for recording
i2s_config_t i2s_config_record = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
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

i2s_pin_config_t pin_config_record = {
    .bck_io_num = I2S_PIN_NO_CHANGE,    // Bit clock pin for microphone (input)
    .ws_io_num = I2S_PIN_NO_CHANGE,     // Word select pin for microphone (input)
    .data_out_num = I2S_PIN_NO_CHANGE, // No data output pin needed for recording
    .data_in_num = 34};  // Data input pin for microphone

// I2S configurations for playback
i2s_config_t i2s_config_playback = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_LSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = bufferSize,
    .use_apll = true,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

i2s_pin_config_t pin_config_playback = {
    .bck_io_num = 17,    // Bit clock pin for speaker (output)
    .ws_io_num = I2S_PIN_NO_CHANGE,     // Word select pin for speaker (output)
    .data_out_num = 16,  // Data output pin for speaker
    .data_in_num = I2S_PIN_NO_CHANGE}; // No data input pin needed for playback

// Timers
unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 60000; // Check for new audio every 60 seconds

bool newAudioAvailable = false;

// Function declarations
void checkForNewAudio();
void recordAudio();
void uploadAudio();
void downloadAudio();
void playAudio();
void handleRecordButton();
void handlePlayButton();
void configureI2S(i2s_config_t& config, i2s_pin_config_t& pinConfig);
void writeWAVHeader(File &file, size_t dataSize);
size_t getFileSize(const String& filePath);
void blinkPlayButton();

void setup() {
    Serial.begin(115200);
    delay(5000);

    Serial.println("Setup starting...");

    // Initialize SD card
    Serial.print("Initializing SD card on pin ");
    Serial.println(CSPin);
    if (!SD.begin(CSPin)) {
        Serial.println("Failed to initialize SD card. Check connections or try a different SD card.");
        return;
    }
    Serial.println("SD card initialized successfully.");

    // Connect to Wi-Fi
    Serial.print("Connecting to Wi-Fi network ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
        attempts++;
        if (attempts > 30) {
            Serial.println("Failed to connect to Wi-Fi. Please check SSID and password.");
            return;
        }
    }
    Serial.println("\nConnected to Wi-Fi successfully.");

    // Install I2S driver for recording
    Serial.print("Installing I2S driver for recording...");
    configureI2S(i2s_config_record, pin_config_record);

    // Pin setup
    pinMode(recordRedButtonPin, INPUT_PULLUP);
    pinMode(recordRedLEDPin, OUTPUT);
    pinMode(playBlueButtonPin, INPUT_PULLUP);
    pinMode(playBlueLEDPin, OUTPUT);

    Serial.println("Setup completed successfully.");
}

void loop() {
    // Handle record button press
    if (digitalRead(recordRedButtonPin) == LOW) {
        Serial.println("Record button pressed.");
        digitalWrite(recordRedLEDPin, HIGH); // Turn on LED

        // Check if file exists and delete it before recording
        if (SD.exists(recordedFilePath)) {
            Serial.println("Deleting existing file before recording...");
            if (!SD.remove(recordedFilePath)) {
                Serial.println("Error: Failed to delete existing file.");
                digitalWrite(recordRedLEDPin, LOW); // Turn off LED
                return;
            }
        }

        recordAudio();
        digitalWrite(recordRedLEDPin, LOW); // Turn off LED
    }

    // Handle play button press
    if (digitalRead(playBlueButtonPin) == LOW && newAudioAvailable) {
        Serial.println("Play button pressed.");
        digitalWrite(playBlueLEDPin, HIGH); // Turn on LED
        playAudio();
        digitalWrite(playBlueLEDPin, LOW); // Turn off LED
        newAudioAvailable = false; // Reset after playing
        if (SD.exists(downloadedFilePath)) {
            if (SD.remove(downloadedFilePath)) {
                Serial.println("Downloaded audio file deleted after playback.");
            } else {
                Serial.println("Error: Failed to delete downloaded audio file after playback.");
            }
        }
    }

    // Periodically check for new audio files
    if (millis() - lastCheckTime > checkInterval) {
        lastCheckTime = millis();
        checkForNewAudio();
    }

    if (newAudioAvailable) {
        blinkPlayButton(); // Blink play button if a new audio file is available
    }

    delay(1000); // Loop delay
}

void checkForNewAudio() {
    Serial.println("Checking for new audio files...");

    WiFiClientSecure client;
    client.setInsecure(); // Disable SSL certificate verification for simplicity

    HTTPClient http;
    http.begin(client, serverURL + checkFileURL); // Use the correct endpoint for checking

    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        Serial.println("New audio file available, downloading...");
        downloadAudio();
        newAudioAvailable = true; // Set flag to indicate new audio is available
    } else {
        Serial.printf("Check failed with HTTP response code: %d\n", httpResponseCode);
    }

    http.end();
}

void downloadAudio() {
    Serial.println("Downloading audio...");

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    HTTPClient http;
    String downloadURL = serverURL + "/download/device2/uploaded_audio_device2.wav";
    http.begin(client, downloadURL);

    File audioFile = SD.open(downloadedFilePath, FILE_WRITE);
    if (!audioFile) {
        Serial.println("Failed to open file for writing. Check SD card and try again.");
        return;
    }

    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        WiFiClient *stream = http.getStreamPtr();
        size_t totalBytesDownloaded = 0;
        uint8_t buffer[512]; // Adjust buffer size as needed
        while (stream->available()) {
            int bytesRead = stream->readBytes(buffer, sizeof(buffer));
            if (bytesRead > 0) {
                audioFile.write(buffer, bytesRead);
                totalBytesDownloaded += bytesRead;
            }
        }
        Serial.printf("Download completed. Total bytes downloaded: %d\n", totalBytesDownloaded);

        // Delete the file from the server
        http.end();
        http.begin(client, downloadURL);
        int deleteResponseCode = http.sendRequest("DELETE");
        if (deleteResponseCode == 200) {
            Serial.println("Audio file deleted from server after download.");
        } else {
            Serial.printf("Failed to delete file from server, HTTP response code: %d\n", deleteResponseCode);
        }
    } else {
        Serial.printf("Download failed with HTTP response code: %d\n", httpResponseCode);
    }

    audioFile.close();
    http.end();
}

void blinkPlayButton() {
    static bool ledState = false;
    ledState = !ledState;
    digitalWrite(playBlueLEDPin, ledState ? HIGH : LOW);
}

void playAudio() {
    Serial.println("Playing audio...");

    File audioFile = SD.open(downloadedFilePath, FILE_READ);
    if (!audioFile) {
        Serial.println("Failed to open file for reading. Check SD card and file path.");
        return;
    }

    Serial.println("File opened successfully for reading.");


    i2s_driver_uninstall(I2S_NUM_0);
    // Install I2S driver for playback
    configureI2S(i2s_config_playback, pin_config_playback);

    int16_t buffer[bufferSize];
    size_t bytesRead;
    size_t totalBytesPlayed = 0;

    // Skip WAV header
    audioFile.seek(44); // WAV header is 44 bytes

    while (audioFile.available()) {
        size_t bytesToRead = audioFile.read((uint8_t *)buffer, sizeof(buffer));
        esp_err_t i2s_err = i2s_write(I2S_NUM_0, (char *)buffer, bytesToRead, &bytesRead, portMAX_DELAY);
        if (i2s_err != ESP_OK) {
            Serial.printf("I2S write failed with error code: %d\n", i2s_err);
            break;
        }
        totalBytesPlayed += bytesRead;
        Serial.printf("Played %d bytes\n", bytesRead);
    }

    audioFile.close();
    Serial.printf("Playback finished. Total bytes played: %d\n", totalBytesPlayed);

    // Uninstall I2S driver after playback
    i2s_driver_uninstall(I2S_NUM_0);
}

void recordAudio() {
    Serial.println("Starting recording...");

    File audioFile = SD.open(recordedFilePath, FILE_WRITE);
    if (!audioFile) {
        Serial.println("Failed to open file for writing. Check SD card and try again.");
        return;
    }

    Serial.println("File opened successfully for writing.");
    configureI2S(i2s_config_record, pin_config_record);

    int16_t buffer[bufferSize];
    size_t bytesRead;
    size_t totalBytesWritten = 0;

    unsigned long startTime = millis(); // Record start time
    unsigned long currentTime;

    while (true) {
        currentTime = millis();
        if (currentTime - startTime >= 5000) { // Check if 3 seconds have passed
            Serial.println("3 seconds elapsed, stopping recording.");
            break; // Exit loop after 3 seconds
        }

        esp_err_t i2s_err = i2s_read(I2S_NUM_0, (char *)buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
        if (i2s_err == ESP_OK) {
            Serial.printf("Read %d bytes from I2S\n", bytesRead);
            audioFile.write((uint8_t *)buffer, bytesRead);
            totalBytesWritten += bytesRead;
            Serial.printf("Wrote %d bytes to SD card\n", bytesRead);
        } else {
            Serial.printf("Error: Failed to read data from I2S, error code: %d\n", i2s_err);
        }
    }

    // Update file with correct WAV header
    size_t dataSize = totalBytesWritten + 36; // Adding 36 bytes for the header
    audioFile.seek(0); // Move to the beginning of the file
    writeWAVHeader(audioFile, dataSize);

    audioFile.close();
    Serial.printf("Recording stopped, file closed. Total recorded bytes: %d\n", totalBytesWritten);

    // Upload the audio file to a server or do further processing
    uploadAudio();
}

void uploadAudio() {
    Serial.println("Uploading audio...");

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);

    HTTPClient http;
    String uploadURL = serverURL + "/upload";
    http.begin(client, uploadURL);

    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("User-Agent", "ESP32/1.0");
    http.addHeader("X-Filename", "uploaded_audio_device1.wav");
    http.addHeader("X-Device-Type", "device1"); // Specify that this is device1

    File audioFile = SD.open(recordedFilePath, FILE_READ);
    if (!audioFile) {
        Serial.println("Failed to open file for reading. Check SD card and file path.");
        return;
    }

    size_t fileSize = audioFile.size();
    if (fileSize == 0) {
        Serial.println("File is empty, upload aborted.");
        audioFile.close();
        return;
    }

    int httpResponseCode = http.sendRequest("POST", &audioFile, fileSize);
    Serial.printf("HTTP Response Code: %d\n", httpResponseCode);

    audioFile.close();
    http.end();

    Serial.println("Upload process finished.");
}

void configureI2S(i2s_config_t& config, i2s_pin_config_t& pinConfig) {
    Serial.println("Configuring I2S...");

    // Uninstall I2S driver if already installed
    if (i2s_driver_install(I2S_NUM_0, &config, 0, NULL) != ESP_ERR_INVALID_STATE) {
        Serial.println("Uninstalling existing I2S driver...");
        i2s_driver_uninstall(I2S_NUM_0);
        delay(500); // Wait for a while to ensure uninstallation
    }
    
    // Try to install I2S driver
    esp_err_t install_status = i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    if (install_status != ESP_OK) {
        Serial.printf("I2S driver installation failed with error code: %d\n", install_status);
        return;
    }

    // Configure I2S pins
    esp_err_t pin_status = i2s_set_pin(I2S_NUM_0, &pinConfig);
    if (pin_status != ESP_OK) {
        Serial.printf("I2S pin configuration failed with error code: %d\n", pin_status);
    } else {
        Serial.println("I2S configured successfully.");
    }
}

void writeWAVHeader(File &file, uint32_t dataSize) {
    Serial.println("Writing WAV header...");

    typedef struct
    {
        char chunkID[4];        // "RIFF"
        uint32_t chunkSize;     // 36 + dataSize
        char format[4];         // "WAVE"
        char subchunk1ID[4];    // "fmt "
        uint32_t subchunk1Size; // 16 for PCM
        uint16_t audioFormat;   // PCM = 1
        uint16_t numChannels;   // 1 for mono, 2 for stereo
        uint32_t sampleRate;    // 44100 or other rate
        uint32_t byteRate;      // sampleRate * numChannels * bitsPerSample / 8
        uint16_t blockAlign;    // numChannels * bitsPerSample / 8
        uint16_t bitsPerSample; // 16 for PCM
        char subchunk2ID[4];    // "data"
        uint32_t subchunk2Size; // dataSize
    } WAVHeader;

    WAVHeader header = {
        {'R', 'I', 'F', 'F'},       // Chunk ID
        36 + dataSize,              // Chunk Size
        {'W', 'A', 'V', 'E'},       // Format
        {'f', 'm', 't', ' '},       // Subchunk 1 ID
        16,                         // Subchunk 1 Size (16 for PCM)
        1,                          // Audio Format (1 for PCM)
        1,                          // Number of Channels (1 for mono, 2 for stereo)
        sampleRate,                 // Sample Rate
        sampleRate * 2,             // Byte Rate (SampleRate * NumChannels * BitsPerSample / 8)
        2,                          // Block Align (NumChannels * BitsPerSample / 8)
        16,                         // Bits Per Sample
        {'d', 'a', 't', 'a'},       // Subchunk 2 ID
        dataSize                    // Subchunk 2 Size
    };

    file.write((uint8_t *)&header, sizeof(header));

    Serial.println("WAV header written successfully.");
}

size_t getFileSize(const String& filePath) {
    File file = SD.open(filePath, FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for size check. Check SD card and file path.");
        return 0;
    }
    size_t size = file.size();
    file.close();
    return size;
}