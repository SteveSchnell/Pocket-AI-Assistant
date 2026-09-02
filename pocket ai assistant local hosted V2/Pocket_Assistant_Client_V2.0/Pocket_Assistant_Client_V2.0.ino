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

const char* WIFI_SSID = "------";
const char* WIFI_PASS = "----------";

//
// ==================================================
// WEBSOCKET SERVER IP
// ==================================================
//

const char* WS_SERVER = "ws://---.---.---.---:9000";  //Server IP

//
// ==================================================
// ASSISTANT CONFIG TO LOAD
// ==================================================
//

const char* ASSISTANT_CONFIG = "";

const int MAX_CONFIG = 10;       // Maximum spots in the array
String configArray[MAX_CONFIG];  // The fixed-size array
int configCount = 0;             // Tracks how many words we actually found

//
// ==================================================
// I2S MIC PINS
// ==================================================
//

#define BUTTON_A 48  //A
#define BUTTON_B 45  //B
#define BUTTON_C 21  //←
#define BUTTON_D 20  //↑
#define BUTTON_E 47  //→
#define BUTTON_F 40  //↓

int BUTTON_A_STAT = 0;
int BUTTON_B_STAT = 0;
int BUTTON_C_STAT = 0;
int BUTTON_D_STAT = 0;
int BUTTON_E_STAT = 0;
int BUTTON_F_STAT = 0;

//
// ==================================================
// I2S MIC PINS
// ==================================================
//

#define I2S_MIC_WS 3
#define I2S_MIC_SD 8
#define I2S_MIC_SCK 9

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

//https://github.com/lovyan03/LovyanGFX/tree/master
//https://notisrac.github.io/FileToCArray/
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9486 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void) {

    auto cfg_bus = _bus_instance.config();

    cfg_bus.spi_host = SPI2_HOST;
    cfg_bus.spi_mode = 0;
    cfg_bus.freq_write = 40000000;

    cfg_bus.pin_sclk = 12;
    cfg_bus.pin_mosi = 11;
    cfg_bus.pin_miso = -1;
    cfg_bus.pin_dc = 5;

    _bus_instance.config(cfg_bus);
    _panel_instance.bus(&_bus_instance);


    auto cfg_panel = _panel_instance.config();

    cfg_panel.pin_cs = 6;
    cfg_panel.pin_rst = 4;
    cfg_panel.pin_busy = -1;

    cfg_panel.memory_width = 320;
    cfg_panel.memory_height = 480;

    cfg_panel.panel_width = 320;
    cfg_panel.panel_height = 480;

    cfg_panel.offset_x = 0;
    cfg_panel.offset_y = 0;

    cfg_panel.invert = false;
    cfg_panel.rgb_order = false;
    cfg_panel.bus_shared = true;

    _panel_instance.config(cfg_panel);

    setPanel(&_panel_instance);
  }
};

LGFX lcd;

#define IMAGE_BUFFER_SIZE 400000  // Maximum PNG size in bytes
uint8_t* imageBuffer = NULL;
size_t imageSize = 0;

//
// ==================================================
// MENU/UI INDEX
// ==================================================
//
bool SendingAudio = false;
bool SendingImaeg = false;
bool confirmRequest = false;
bool autoRun = false;
String wifiString;
String wifiIpString;
String WebSocketString;
int activeConfigID = 0;
int menuID = 0;
int menuActiveID_A = 0;
int menuActiveID_B = 0;

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
bool serverReady = false;

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

  //Set button mode
  pinMode(BUTTON_A, INPUT_PULLUP);  //20
  pinMode(BUTTON_B, INPUT_PULLUP);  //21
  pinMode(BUTTON_C, INPUT_PULLUP);  //47
  pinMode(BUTTON_D, INPUT_PULLUP);  //46
  pinMode(BUTTON_E, INPUT_PULLUP);  //45
  pinMode(BUTTON_F, INPUT_PULLUP);  //0

  // Create audio buffer
  audioBuffer = xRingbufferCreate(128 * 1024, RINGBUF_TYPE_BYTEBUF);

  if (audioBuffer == NULL) {
    Serial.println("ERROR: Failed to create audio buffer!");
  } else {
    Serial.println("Audio buffer created");
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

  //LCD setup
  lcd.init();
  lcd.setRotation(6);
  lcd.setBrightness(128);
  lcd.setColorDepth(16);

  showLogo();
  delay(1000);

  connectWiFi();
  connectWebSocket();
  delay(2000);
}

//
// ==================================================
// LOOP
// ==================================================
//

void loop() {


  buttonRead();
  checkConnections();
  manu();

  if (ws.available()) {
    ws.poll();
  }

  // Voice activation
  if (serverReady) {
    if (!isPlaying && !isRecording && !waitingForServer) {
      recordAudioToServer();
    }
  }

  delay(5);
}

//
// ==================================================
// READ BUTTON INPUT
// ==================================================
//

void buttonRead() {
  if (digitalRead(BUTTON_A) == LOW && BUTTON_A_STAT == 0)
    BUTTON_A_STAT = 1;
  else if (digitalRead(BUTTON_A) != LOW && (BUTTON_A_STAT == -1 || BUTTON_A_STAT == 1))
    BUTTON_A_STAT = 0;

  if (digitalRead(BUTTON_B) == LOW && BUTTON_B_STAT == 0)
    BUTTON_B_STAT = 1;
  else if (digitalRead(BUTTON_B) != LOW && (BUTTON_B_STAT == -1 || BUTTON_B_STAT == 1))
    BUTTON_B_STAT = 0;

  if (digitalRead(BUTTON_C) == LOW && BUTTON_C_STAT == 0)
    BUTTON_C_STAT = 1;
  else if (digitalRead(BUTTON_C) != LOW && (BUTTON_C_STAT == -1 || BUTTON_C_STAT == 1))
    BUTTON_C_STAT = 0;

  if (digitalRead(BUTTON_D) == LOW && BUTTON_D_STAT == 0)
    BUTTON_D_STAT = 1;
  else if (digitalRead(BUTTON_D) != LOW && (BUTTON_D_STAT == -1 || BUTTON_D_STAT == 1))
    BUTTON_D_STAT = 0;

  if (digitalRead(BUTTON_E) == LOW && BUTTON_E_STAT == 0)
    BUTTON_E_STAT = 1;
  else if (digitalRead(BUTTON_E) != LOW && (BUTTON_E_STAT == -1 || BUTTON_E_STAT == 1))
    BUTTON_E_STAT = 0;

  if (digitalRead(BUTTON_F) == LOW && BUTTON_F_STAT == 0)
    BUTTON_F_STAT = 1;
  else if (digitalRead(BUTTON_F) != LOW && (BUTTON_F_STAT == -1 || BUTTON_F_STAT == 1))
    BUTTON_F_STAT = 0;
}

//
// ==================================================
// UI
// ==================================================
//

void showLogo() {
  lcd.setTextSize(4);  //4=30
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString("Pocket", 160, 50);
  lcd.drawString("Assistant", 160, 95);
}

void showWiFi() {
  lcd.setTextSize(2);
  lcd.setTextColor(0x00aef0);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.fillRect(0, 270, 320, 40, TFT_BLACK);
  lcd.drawString(wifiString, 160, 280);
  lcd.drawString(wifiIpString, 160, 300);
}

void showWebSocket() {
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.fillRect(0, 330, 320, 20, TFT_BLACK);
  lcd.drawString(WebSocketString, 160, 340);
}

void showConfig() {
  setBackgraound();
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE );
  lcd.setTextDatum(textdatum_t::middle_center);

  int activeConfigID_A = activeConfigID + 1;
  int activeConfigID_B = activeConfigID - 1;

  if (activeConfigID_B == -1)
    activeConfigID_B = configCount - 1;

  if (activeConfigID_A == configCount)
    activeConfigID_A = 0;

  lcd.drawString("Select config to load.", 160, 270);

  lcd.setTextSize(1.5);
  lcd.setTextColor(TFT_GREEN );
  lcd.drawString(configArray[activeConfigID], 160, 300);
  lcd.setTextSize(1.5);
  lcd.setTextColor(TFT_WHITE );
  lcd.drawString("<<<          >>>", 160, 320);
  lcd.setTextColor(TFT_DARKGREEN);

  lcd.drawString(configArray[activeConfigID_B], 80, 340);
  lcd.drawString(configArray[activeConfigID_A], 240, 340);


  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_YELLOW);
  lcd.drawString("Left/Right      A Confirm", 160, 460);
}

void showMassege(String massege, uint16_t color, int size) {
  lcd.setTextSize(size);
  lcd.setTextColor(color);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.fillRectAlpha(0, 0, 320, size * 8, 128, TFT_BLACK);
  lcd.drawString(massege, 160, (size * 4));
}

void showMainManu() {
  setBackgraound();
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString("B image    A record", 160, 450);
  lcd.drawString("UP clear    Downe restart", 160, 470);
}

void showUserText(String text) {
  setBackgraound();
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.setTextWrap(true, false);
  lcd.setCursor(10, 20);
  lcd.print(text);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.drawString("B cancel      A submit", 160, 460);
}

void setBackgraound() {

  if (imageBuffer != NULL && menuID != 1) {
    lcd.drawPng(imageBuffer, imageSize, 0, 0);
    lcd.fillRect(0, 439, 320, 50, TFT_BLACK);
  }
  else{
    lcd.fillScreen(TFT_BLACK);
  }
}

//
// ==================================================
// MANU LOGIC
// ==================================================
//

void manu() {
  //config menu

  if (BUTTON_F_STAT == 1) {
       setBackgraound();
       Serial.println("Restarting");
       showMassege("Restarting", TFT_RED , 3);
       BUTTON_F_STAT = -1;
       serverReady = false;
       delay(1000);

       ESP.restart();
    }

  if (menuID == 1) {
    if (BUTTON_E_STAT == 1) {  //right
      if (activeConfigID == configCount - 1)
        activeConfigID = 0;
      else activeConfigID++;
      showConfig();
      BUTTON_E_STAT = -1;
    } else if (BUTTON_C_STAT == 1) {  //left
      if (activeConfigID == 0)
        activeConfigID = configCount - 1;
      else activeConfigID--;
      showConfig();
      BUTTON_C_STAT = -1;
    }

    if (BUTTON_A_STAT == 1) {  //submit

      // Send assistant configuration
      ws.send(configArray[activeConfigID]);

      Serial.println("Loaded assistant config: ");
      Serial.println(configArray[activeConfigID]);
      delay(1000);

      showMassege("Loading: " + configArray[activeConfigID], TFT_ORANGE, 2);
      delay(1000);
      BUTTON_A_STAT = -1;
    }
  }

  if (menuID == 4) {
    if (BUTTON_B_STAT == 1) {
      Serial.println("Get Image");
      ws.send("FORCE_IMAGE");
      setBackgraound();
      showMassege("Prossesing", TFT_GREEN , 2);
      BUTTON_B_STAT = -1;
      menuID = 0;
    }  else if (BUTTON_D_STAT == 1) {
       Serial.println("Clearing history");
       ws.send("CLEAR_HISTORY");
       setBackgraound();
       showMassege("Clearing history", TFT_GREEN , 2);
       BUTTON_D_STAT = -1;
       delay(1000);
    }

    if(autoRun){
      Serial.println("SUBMIT");
      ws.send("SUBMIT");
      setBackgraound();
      showMassege("Prossesing", TFT_GREEN , 2);
      menuID = 0;
    }
  }

  if (menuID == 7) {
    if (BUTTON_A_STAT == 1) {
      Serial.println("SUBMIT");
      ws.send("SUBMIT");
      setBackgraound();
      showMassege("Prossesing", TFT_GREEN , 2);
      menuID = 0;
      BUTTON_A_STAT = -1;
    } else if (BUTTON_B_STAT == 1) {
      Serial.println("CANCEL");
      ws.send("CANCEL");
      menuID = 4;
      showMainManu();
      serverReady = true;
      BUTTON_B_STAT = -1;
    }
  }
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
  Serial.println("Connecting to WiFi");
  wifiString = "Connecting to WiFi";
  showWiFi();
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts >= 40) {
      Serial.println();
      Serial.println("WiFi failed, restarting attempt");
      wifiString = "WiFi failed, restarting attempt";
      showWiFi();
      WiFi.disconnect();
      delay(1000);

      WiFi.begin(WIFI_SSID, WIFI_PASS);
      attempts = 0;
    }
  }

  Serial.println();
  Serial.println("WiFi Connected");
  wifiString = "WiFi Connected";
  Serial.println("IP: ");
  Serial.println(WiFi.localIP());
  wifiIpString = "IP: " + WiFi.localIP().toString();
  showWiFi();
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

  Serial.println("Connecting to Server");
  WebSocketString = "Connecting to Server";
  showWebSocket();

  while (!ws.connect(WS_SERVER)) {
    Serial.print(".");
    delay(2000);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println();
      Serial.println("WiFi lost during WS reconnect");
      WebSocketString = "WiFi lost";
      showWebSocket();
      connectWiFi();
    }
  }

  Serial.println();
  Serial.println("WebSocket connected");
  WebSocketString = "Server Connected";
  showWebSocket();
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
      showMassege("WS Connected", TFT_ORANGE, 2);
      delay(1000);
      break;

    case WebsocketsEvent::ConnectionClosed:
      Serial.println("WS Closed");
      showMassege("WS Closed", TFT_RED, 2);
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

  if (message.isBinary()) {

    //Reciving Audio
    if (SendingAudio) {

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

    //Reciving Image
    if (SendingImaeg) {

      const uint8_t* data = (const uint8_t*)message.c_str();
      size_t len = message.length();

      Serial.printf("Received image packet: %u bytes\n", len);

      // Debug the actual bytes arriving from WebSocket
      Serial.print("Packet first bytes: ");

      size_t debugLen = min((size_t)16, len);

      for (size_t i = 0; i < debugLen; i++) {
        Serial.printf("%02X ", data[i]);
      }

      Serial.println();


      // Check for overflow
      if (imageSize + len > IMAGE_BUFFER_SIZE) {

        Serial.println("ERROR: Image is too large!");

        if (imageBuffer != NULL) {
          free(imageBuffer);
          imageBuffer = NULL;
        }

        imageSize = 0;
        SendingImaeg = false;

        return;
      }


      // Allocate buffer once
      if (imageBuffer == NULL) {

        imageBuffer = (uint8_t*)ps_malloc(IMAGE_BUFFER_SIZE);

        if (imageBuffer == NULL) {
          Serial.println("ERROR: Failed to allocate image buffer in PSRAM!");
          imageSize = 0;
          SendingImaeg = false;
          return;
        }

        imageSize = 0;
      }


      // IMPORTANT:
      // Copy at the CURRENT imageSize offset.
      memcpy(imageBuffer + imageSize, data, len);

      imageSize += len;


      Serial.printf(
        "Image buffer: %u / %u bytes\n",
        imageSize,
        IMAGE_BUFFER_SIZE);


      // Show the first 16 bytes of the COMPLETE image
      if (imageSize >= 16) {

        Serial.print("Image first bytes: ");

        for (int i = 0; i < 16; i++) {
          Serial.printf("%02X ", imageBuffer[i]);
        }

        Serial.println();
      }

      return;
    }
  }

  // TEXT MESSAGE
  if (message.isText()) {
    String msg = message.data();
    int colonIndex = msg.indexOf(':');

    //is string data
    if (colonIndex != -1) {
      String code = msg.substring(0, colonIndex);
      String data = msg.substring(colonIndex + 1);

      Serial.println("Action Code Received: ");
      Serial.println(code);

      //get config list
      if (code == "ConfigList") {
        int commaIndex = 0;
        int startIndex = 0;

        data.replace(" ", "");
        // Loop through the string searching for commas
        while ((commaIndex = data.indexOf(',', startIndex)) != -1) {
          // Stop if the array is completely full
          if (configCount >= MAX_CONFIG) break;

          configArray[configCount] = data.substring(startIndex, commaIndex);
          configCount++;

          startIndex = commaIndex + 1;  // Move past the comma
        }

        // Add the last word if there is room left
        if (startIndex < data.length() && configCount < MAX_CONFIG) {
          configArray[configCount] = data.substring(startIndex);
          configCount++;
        }

        // 3. Print the array contents to verify
        Serial.println("Total words saved: ");
        Serial.println(configCount);

        for (int i = 0; i < configCount; i++) {
          Serial.print("Array Index [");
          Serial.print(i);
          Serial.print("]: ");
          Serial.println(configArray[i]);
        }

        menuID = 1;
        int menuActiveID_A = 0;
        int menuActiveID_B = 0;
        showConfig();
        return;
      }

      //get user input as text
      if (code == "userText") {
        if (confirmRequest) {
          menuID = 7;
          showUserText(data);
        } else {
          Serial.println("SUBMIT");
          ws.send("SUBMIT");
          showMassege("Prossesing", TFT_GREEN, 2);
          menuID = 0;
        }
        return;
      }

      Serial.println("ERROR: Unknown code.");
      return;
    }

    //config loaded server is ready
    if (msg == "Ready") {
      showMassege("System ready", TFT_GREEN, 3);
      delay(1000);
      serverReady = true;
      menuID = 4;
      showMainManu();
      return;
    }

    if (msg == "CONFIRM_REQUEST") {
      confirmRequest = true;
      return;
    }

    if (msg == "AUTO_RUN") {
      autoRun = true;
      return;
    }

    if (msg == "SendingAudio") {
      SendingAudio = true;
      serverReady = false;
      showMassege("Reciving audio.", TFT_GREEN, 2);
      return;
    }

    if (msg == "SendingImaeg") {
      SendingImaeg = true;
      serverReady = false;
      showMassege("Reciving image", TFT_GREEN, 2);
      return;
    }

    if (msg == "REQUEST_COMPLETE") {
      delay(500);
      serverReady = true;
      menuID = 4;
      showMainManu();
      return;
    }

    if (msg == "CONFIG_ERROR") {

      Serial.println();
      Serial.println("Assistant config failed!");
      showMassege("Assistant config failed!", TFT_RED , 2);
      waitingForServer = false;

      return;
    }

    // No speech detected
    if (msg == "NO_SPEECH") {

      Serial.println();
      Serial.println("No speech detected");

      waitingForServer = false;
      isPlaying = false;
      serverReady = true;
      menuID = 4;
      showMainManu();
      return;
    }

    // AUDIO STARTED
    if (msg == "AUDIO_START") {
      Serial.println();
      Serial.println("AUDIO STREAM START");
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
      Serial.println();
      audioStreamFinished = true;
      SendingAudio = false;
      return;
    }

    //IMAGE STARTED
    if (msg == "IMAGE_START") {
      Serial.println();
      Serial.println("Reciving image");
      Serial.println();

      if (imageBuffer != NULL) {
        free(imageBuffer);
        imageBuffer = NULL;
      }

      imageSize = 0;
      SendingImaeg = true;

      return;
    }

    // IMAGE FINISHED
    if (msg == "IMAGE_END") {
      Serial.printf("Final image size: %u bytes\n", imageSize);

      if (imageSize >= 8) {

        Serial.print("Final image header: ");

        for (int i = 0; i < 8; i++) {
          Serial.printf("%02X ", imageBuffer[i]);
        }

        Serial.println();
      }

      SendingImaeg = false;
      return;
    }

    //Debug massege
    Serial.println(msg);
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
    showMassege("WiFi disconnected!", TFT_RED , 2);

    connectWiFi();

    Serial.println("WiFi restored");
  }

  // WEBSOCKET CHECK
  if (!ws.available()) {

    Serial.println();
    Serial.println("WebSocket disconnected!");
    showMassege("WebSocket disconnected!", TFT_RED , 2);

    connectWebSocket();

    Serial.println("WebSocket restored");
    isPlaying = false;
    waitingForServer = false;
  }
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
// RECORD AUDIO TO SERVER
// ==================================================
//
void recordAudioToServer() {

  // Only start recording when BUTTON_A is pressed
  if (digitalRead(BUTTON_A) != LOW) {
    return;
  }

  isRecording = true;

  const size_t MAX_AUDIO_SIZE = (32000UL * MAX_RECORD_TIME) / 1000UL;

  uint8_t* audioBuffer = (uint8_t*)ps_malloc(MAX_AUDIO_SIZE);

  if (audioBuffer == nullptr) {
    Serial.println("ERROR: Failed to allocate audio buffer");
    isRecording = false;
    menuID = 4;
    showMainManu();
    return;
  }

  size_t totalBytes = 0;
  size_t bytesRead;

  unsigned long startTime = millis();

  Serial.println();
  Serial.println("=== PUSH TO TALK ===");
  Serial.println("Recording...");
  showMassege("Recording", TFT_BLUE , 2);

  // Record while button is held
  while (digitalRead(BUTTON_A) == LOW) {

    // Stop if playback starts
    if (isPlaying) {
      Serial.println("Playback started - stopping recording");
      break;
    }

    // Prevent buffer overflow
    if (totalBytes + MIC_AUDIO_CHUNK_SIZE > MAX_AUDIO_SIZE) {
      Serial.println("Maximum recording buffer reached");
      break;
    }

    // Read microphone
    i2s_read(
      I2S_PORT_MIC,
      audioBuffer + totalBytes,
      MIC_AUDIO_CHUNK_SIZE,
      &bytesRead,
      portMAX_DELAY);

    if (bytesRead > 0) {
      totalBytes += bytesRead;
    }

    // Maximum recording time
    if (millis() - startTime >= MAX_RECORD_TIME) {
      Serial.println("Maximum recording time reached");
      break;
    }
  }

  isRecording = false;

  Serial.println();
  Serial.println("Button released");
  Serial.println("Recorded ");
  Serial.print(totalBytes);
  Serial.print(" bytes");

  // Nothing recorded
  if (totalBytes == 0) {
    Serial.println("No audio recorded");
    free(audioBuffer);
    menuID = 4;
    showMainManu();
    return;
  }

  // Send the COMPLETE recording
  Serial.println("Sending recording...");

  ws.sendBinary(
    (const char*)audioBuffer,
    totalBytes);

  // Tell server the complete recording has been sent
  ws.send("AUDIO_END");

  Serial.println("Recording sent");
  showMassege("Prossesing", TFT_GREEN , 2);
  delay(1000);
  serverReady = false;
  free(audioBuffer);
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

      delay(3000);

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
