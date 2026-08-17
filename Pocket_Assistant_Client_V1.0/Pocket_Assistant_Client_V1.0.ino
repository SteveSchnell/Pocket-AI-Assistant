/*

ESP32-S3-N16R8 Dev Module
INMP441 microphone
MAX98357A amplifier
SSD1306 Screen
*/

#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <driver/i2s.h>
#include <Adafruit_SSD1306.h>

using namespace websockets;

//
// ==================================================
// WIFI
// ==================================================
//

const char* WIFI_SSID = "-----";//WiFi Name
const char* WIFI_PASS = "-----";//WiFi Pass

//
// ==================================================
// WEBSOCKET SERVER IP
// ==================================================
//

const char* WS_SERVER = "ws://-.-.-.-:9000";//Server IP

//
// ==================================================
// ASSISTANT CONFIG TO LOAD
// ==================================================
//

const char* ASSISTANT_CONFIG = "AssistantA.txt";


//
// ==================================================
// I2S MIC PINS
// ==================================================
//

#define I2S_MIC_WS 6
#define I2S_MIC_SD 5
#define I2S_MIC_SCK 4

//
// ==================================================
// I2S SPEAKER PINS
// ==================================================
//

#define I2S_SPK_BCLK 15
#define I2S_SPK_LRC 16
#define I2S_SPK_DOUT 17

//
// ==================================================
// AUDIO SETTINGS
// ==================================================
//

#define SAMPLE_RATE 16000
#define MIC_AUDIO_CHUNK_SIZE 1024
#define I2S_PORT_MIC I2S_NUM_0
#define I2S_PORT_SPK I2S_NUM_1

//
// ==================================================
// OLED SETTINGS
// ==================================================
//

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(4);

//
// ==================================================
// VOICE ACTIVATION
// ==================================================
//

#define VAD_THRESHOLD 40
#define SILENCE_TIMEOUT 900
#define MAX_RECORD_TIME 15000

//
// ==================================================
// STATE
// ==================================================
//

WebsocketsClient ws;
bool isPlaying = false;
bool isRecording = false;
bool waitingForServer = false;
bool audioStreamFinished = true;
RingbufHandle_t audioBuffer = NULL;
unsigned long lastSpeech = 0;
unsigned long lastConnectionCheck = 0;
#define CONNECTION_CHECK_INTERVAL 5000
volatile size_t playbackBytesPending = 0;



//
// ==================================================
// SETUP
// ==================================================
//

void setup() {
  Serial.begin(9600);
  setupDisplay();

  // Create audio buffer
  audioBuffer = xRingbufferCreate(128 * 1024, RINGBUF_TYPE_BYTEBUF);

  if (audioBuffer == NULL) {
    Serial.println("ERROR: Failed to create audio buffer!");
  } else {
    Serial.println("Audio buffer created");
    displayText("Audio buffer created", 0, 0);
  }


  setupSpeaker();
  setupMicrophone();


  // Start playback task
  xTaskCreatePinnedToCore(
    audioPlaybackTask,
    "AudioPlayback",
    8192,
    NULL,
    5,
    NULL,
    1);

  connectWiFi();
  connectWebSocket();

  delay(2000);

  Serial.println("AI Assistant Ready");
  Serial.println("Voice activation enabled");
  displayText("AI Assistant Ready\nVoice activation\nenabled", 0, 0);

  displayText("Ready", 0, 0);
}

//
// ==================================================
// LOOP
// ==================================================
//

void loop() {

  checkConnections();

  if (ws.available()) {
    ws.poll();
  }

  // Voice activation
  if (!isPlaying && !isRecording && !waitingForServer) {
    detectSpeechTrigger();
  }

  delay(5);
}

//
// ==================================================
// WIFI
// ==================================================
//

void connectWiFi() {

  WiFi.disconnect();
  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  displayText("Connecting WiFi", 0, 0);
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts >= 40) {
      Serial.println();
      Serial.println("WiFi failed, restarting attempt");
      displayText("WiFi failed,\nrestarting attempt", 0, 0);
      WiFi.disconnect();
      delay(1000);

      WiFi.begin(WIFI_SSID, WIFI_PASS);
      attempts = 0;
    }
  }

  Serial.println();
  Serial.println("WiFi connected");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  displayText("WiFi connected ", 0, 0);
  delay(1000);
}

//
// ==================================================
// WEBSOCKET
// ==================================================
//

void connectWebSocket() {

  ws.onMessage(onWebSocketMessage);
  ws.onEvent(onWebSocketEvent);

  Serial.print("Connecting WebSocket");
  displayText("Connecting WebSocket", 0, 0);

  while (!ws.connect(WS_SERVER)) {
    Serial.print(".");
    delay(2000);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println();
      Serial.println("WiFi lost during WS reconnect");
      connectWiFi();
    }
  }

  Serial.println();
  Serial.println("WebSocket connected");
  displayText("WebSocket connected", 0, 0);
  delay(1000);

  // Send assistant configuration
  ws.send(ASSISTANT_CONFIG);

  Serial.print("Loaded assistant config: ");
  Serial.println(ASSISTANT_CONFIG);
  displayText("Loaded assistant\nconfig", 0, 0);
  delay(1000);
}

//
// ==================================================
// WEBSOCKET EVENTS
// ==================================================
//

void onWebSocketEvent(WebsocketsEvent event, String data) {

  switch (event) {
    case WebsocketsEvent::ConnectionOpened:
      Serial.println("WS Connected");
      displayText("WS Connected", 0, 0);
      delay(1000);
      break;

    case WebsocketsEvent::ConnectionClosed:
      Serial.println("WS Closed");
      displayText("WS Closed", 0, 0);
      delay(1000);
      waitingForServer = false;
      isRecording = false;
      isPlaying = false;
      break;

    default:
      break;
  }
}

//
// ==================================================
// RECEIVE SERVER DATA
// ==================================================
//

void onWebSocketMessage(WebsocketsMessage message) {
  // TEXT MESSAGE
  if (message.isText()) {
    String msg = message.data();

    if (msg == "CONFIG_ERROR") {

      Serial.println();
      Serial.println("Assistant config failed!");
      displayText("Assistant config failed!", 0, 0);

      waitingForServer = false;

      return;
    }

    // No speech detected
    if (msg == "NO_SPEECH") {

      Serial.println();
      Serial.println("No speech detected");
      displayText("No speech detected\nReady", 0, 0);

      waitingForServer = false;
      isPlaying = false;

      return;
    }

    // AUDIO STARTED
    if (msg == "AUDIO_START") {
      Serial.println();
      Serial.println("AUDIO STREAM START");
      displayText("Audio stream started", 0, 0);
      Serial.println();

      clearAudioBuffer();

      waitingForServer = false;
      audioStreamFinished = false;
      isPlaying = true;

      return;
    }

    // AUDIO FINISHED
    if (msg == "AUDIO_END") {
      Serial.println();
      Serial.println("AUDIO STREAM END - waiting for playback to finish");
      displayText("Finishing audio...", 0, 0);
      Serial.println();
      audioStreamFinished = true;

      return;
    }

    Serial.print(msg);
    displayText(msg, 0, 0);
    return;
  }

  if (message.isBinary()) {

    if (audioBuffer == NULL) {
      Serial.println("ERROR: audioBuffer is NULL");
      return;
    }

    const uint8_t* data = (const uint8_t*)message.c_str();
    size_t len = message.length();
    BaseType_t result = xRingbufferSend(audioBuffer, data, len, pdMS_TO_TICKS(100));

    if (result != pdTRUE) {
      Serial.println("ERROR: Failed to put audio into buffer");
    }

    return;
  }
}

//
// ==================================================
// CONNECTION CHECK
// ==================================================
//

void checkConnections() {

  if (millis() - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) {
    return;
  }

  lastConnectionCheck = millis();

  // WIFI CHECK
  if (WiFi.status() != WL_CONNECTED) {

    Serial.println();
    Serial.println("WiFi disconnected!");
    displayText("WiFi disconnected!", 0, 0);

    connectWiFi();

    Serial.println("WiFi restored");
    displayText("WiFi restored", 0, 0);
  }

  // WEBSOCKET CHECK
  if (!ws.available()) {

    Serial.println();
    Serial.println("WebSocket disconnected!");
    displayText("WebSocket disconnected!", 0, 0);

    connectWebSocket();

    Serial.println("WebSocket restored");
    displayText("WebSocket restored", 0, 0);
    isPlaying = false;
    waitingForServer = false;
  }
}

//
// ==================================================
// DISPLAY SETUP
// ==================================================
//

void setupDisplay() {

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.display();
  delay(2000);
  display.clearDisplay();
  display.display();
}

void displayText(String text, int X, int Y) {
  display.clearDisplay();

  display.setTextColor(WHITE);
  display.setCursor(X, Y);
  display.println(text);

  display.display();
}


//
// ==================================================
// MICROPHONE SETUP
// ==================================================
//

void setupMicrophone() {
  i2s_config_t config = {

    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),

    .sample_rate = SAMPLE_RATE,

    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,

    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,

    .communication_format = I2S_COMM_FORMAT_I2S,

    .intr_alloc_flags = 0,

    .dma_buf_count = 8,

    .dma_buf_len = 512,

    .use_apll = false,

    .tx_desc_auto_clear = false,

    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {

    .bck_io_num = I2S_MIC_SCK,

    .ws_io_num = I2S_MIC_WS,

    .data_out_num = I2S_PIN_NO_CHANGE,

    .data_in_num = I2S_MIC_SD
  };

  i2s_driver_install(I2S_PORT_MIC, &config, 0, NULL);
  i2s_set_pin(I2S_PORT_MIC, &pins);
  i2s_zero_dma_buffer(I2S_PORT_MIC);

  displayText("Microphone I2S ready", 0, 0);
  delay(1000);
}

//
// ==================================================
// SPEAKER SETUP
// ==================================================
//

void setupSpeaker() {

  i2s_config_t config = {

    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),

    .sample_rate = SAMPLE_RATE,

    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,

    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,

    .communication_format = I2S_COMM_FORMAT_I2S,

    .intr_alloc_flags = 0,

    .dma_buf_count = 8,

    .dma_buf_len = 512,

    .use_apll = false,

    .tx_desc_auto_clear = true,

    .fixed_mclk = 0
  };


  i2s_pin_config_t pins = {

    .bck_io_num = I2S_SPK_BCLK,

    .ws_io_num = I2S_SPK_LRC,

    .data_out_num = I2S_SPK_DOUT,

    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT_SPK, &config, 0, NULL);
  i2s_set_pin(I2S_PORT_SPK, &pins);
  i2s_zero_dma_buffer(I2S_PORT_SPK);

  Serial.println("Speaker I2S ready");
  delay(1000);
}

//
// ==================================================
// VOICE ACTIVATION
// ==================================================
//

void detectSpeechTrigger() {
  uint8_t buffer[MIC_AUDIO_CHUNK_SIZE];
  size_t bytesRead;

  i2s_read(I2S_PORT_MIC, buffer, MIC_AUDIO_CHUNK_SIZE, &bytesRead, portMAX_DELAY);

  int16_t* samples = (int16_t*)buffer;
  int sampleCount = bytesRead / 2;
  long sum = 0;

  for (int i = 0; i < sampleCount; i++) {
    sum += abs(samples[i]);
  }

  int level = sum / sampleCount;

  Serial.print("Listening: ");
  Serial.println(level);

  // Voice detected
  if (level > VAD_THRESHOLD) {
    Serial.println();
    Serial.println("Voice detected");
    displayText("Voice detected", 0, 0);
    delay(150);

    streamAudioToServer();
  }
}

//
// ==================================================
// STREAM AUDIO TO SERVER
// ==================================================
//

void streamAudioToServer() {

  isRecording = true;
  uint8_t buffer[MIC_AUDIO_CHUNK_SIZE];
  size_t bytesRead;
  bool speechDetected = false;
  unsigned long startTime = millis();
  lastSpeech = millis();

  while (true) {
    // Stop if playback starts
    if (isPlaying) {
      break;
    }

    // Read microphone
    i2s_read(I2S_PORT_MIC, buffer, MIC_AUDIO_CHUNK_SIZE, &bytesRead, portMAX_DELAY);

    // VAD
    int16_t* samples = (int16_t*)buffer;
    int sampleCount = bytesRead / 2;
    long sum = 0;

    for (int i = 0; i < sampleCount; i++) {
      sum += abs(samples[i]);
    }

    int level = sum / sampleCount;

    // Speech detected
    if (level > VAD_THRESHOLD) {
      speechDetected = true;
      lastSpeech = millis();

      Serial.print("VOICE: ");
      Serial.println(level);
    }

    // Send audio chunk
    ws.sendBinary((const char*)buffer, bytesRead);

    // Silence timeout
    if (speechDetected && millis() - lastSpeech > SILENCE_TIMEOUT) {
      Serial.println();
      Serial.println("End of speech");
      break;
    }

    // Max recording time
    if (millis() - startTime > MAX_RECORD_TIME) {
      Serial.println();
      Serial.println("Max recording reached");
      break;
    }
  }

  // End marker
  ws.send("END");
  Serial.println("Waiting for AI...");
  displayText("Waiting for AI...", 0, 0);
  isRecording = false;

  // Wait for AI response
  waitingForServer = true;
}

//
// ==================================================
// PLAY AUDIO
// ==================================================
//

void audioPlaybackTask(void* parameter) {

  Serial.println("Audio playback task STARTED");

  while (true) {

    size_t itemSize = 0;

    uint8_t* data = (uint8_t*)xRingbufferReceive(
      audioBuffer,
      &itemSize,
      pdMS_TO_TICKS(100));

    if (data != NULL) {

      size_t bytesWritten = 0;

      esp_err_t result = i2s_write(
        I2S_PORT_SPK,
        data,
        itemSize,
        &bytesWritten,
        portMAX_DELAY);

      if (result != ESP_OK) {

        Serial.printf(
          "I2S ERROR: %s\n",
          esp_err_to_name(result));
      }

      vRingbufferReturnItem(audioBuffer, (void*)data);
    }

    if (audioStreamFinished && isPlaying) {

      size_t remainingItemSize = 0;
      uint8_t* remainingData = (uint8_t*)xRingbufferReceive(audioBuffer, &remainingItemSize, 0);

      if (remainingData != NULL) {

        vRingbufferReturnItem(audioBuffer, (void*)remainingData);
        continue;
      }

      delay(500);

      size_t finalItemSize = 0;
      uint8_t* finalData = (uint8_t*)xRingbufferReceive(audioBuffer, &finalItemSize, 0);

      if (finalData != NULL) {

        vRingbufferReturnItem(audioBuffer, (void*)finalData);
        continue;
      }

      Serial.println("AUDIO PLAYBACK ACTUALLY FINISHED");

      audioStreamFinished = false;
      isPlaying = false;
      waitingForServer = false;

      displayText("Ready", 0, 0);
    }
  }
}

//
// ==================================================
// CLEAR AUDIO BUFFER
// ==================================================
//

void clearAudioBuffer() {

  if (audioBuffer == NULL) {
    return;
  }

  size_t itemSize;

  while (true) {

    uint8_t* data = (uint8_t*)xRingbufferReceive(audioBuffer, &itemSize, 0);

    if (data == NULL) {
      break;
    }

    vRingbufferReturnItem(audioBuffer, data);
  }

  Serial.println("Audio buffer cleared");
}
