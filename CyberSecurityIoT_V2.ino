#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// Replace with your Wi-Fi credentials
const char* ssid = "HotspotTest";  // Simplified SSID (avoid ' character)
const char* password = "12345678";  // Wi-Fi Password

// Telegram Bot config
String chatId = "6613306588";
String BOTtoken = "7858776863:AAETyI0zLBy0FHqnf8hSpSx_KHu7SbXzQC4";

bool sendPhoto = false;

WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);

// GPIO definitions
#define BUTTON 13
#define LOCK 12
#define FLASH_LED 4

// CAMERA_MODEL_AI_THINKER pin mapping
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

int lockState = 0;

const unsigned long BOT_MTBS = 1000;
unsigned long bot_lasttime;

void handleNewMessages(int numNewMessages);
String sendPhotoTelegram();

String unlockDoor() {
  if (lockState == 0) {
    digitalWrite(LOCK, HIGH);
    lockState = 1;
    delay(100);
    return "Door Unlocked. /lock";
  } else {
    return "Door Already Unlocked. /lock";
  }
}

String lockDoor() {
  if (lockState == 1) {
    digitalWrite(LOCK, LOW);
    lockState = 0;
    delay(100);
    return "Door Locked. /unlock";
  } else {
    return "Door Already Locked. /unlock";
  }
}

String sendPhotoTelegram() {
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }

  if (clientTCP.connect(myDomain, 443)) {
    Serial.println("Connected to Telegram server");

    String head = "--IotCircuitHub\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + chatId + "\r\n--IotCircuitHub\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--IotCircuitHub--\r\n";

    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;

    clientTCP.println("POST /bot" + BOTtoken + "/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=IotCircuitHub");
    clientTCP.println();
    clientTCP.print(head);

    uint8_t* fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n += 1024) {
      if (n + 1024 < fbLen) {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      } else {
        size_t remainder = fbLen % 1024;
        clientTCP.write(fbBuf, remainder);
      }
    }

    clientTCP.print(tail);
    esp_camera_fb_return(fb);

    long startTimer = millis();
    int waitTime = 10000;
    boolean state = false;

    while ((startTimer + waitTime) > millis()) {
      delay(100);
      while (clientTCP.available()) {
        char c = clientTCP.read();
        if (c == '\n') {
          if (getAll.length() == 0) state = true;
          getAll = "";
        } else if (c != '\r') {
          getAll += String(c);
        }
        if (state == true) {
          getBody += String(c);
        }
        startTimer = millis();
      }
      if (getBody.length() > 0) break;
    }

    clientTCP.stop();
    Serial.println(getBody);
  } else {
    getBody = "Connection to Telegram failed.";
    Serial.println(getBody);
  }
  return getBody;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != chatId) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    String text = bot.messages[i].text;
    if (text == "/photo") {
      sendPhoto = true;
    }
    if (text == "/lock") {
      bot.sendMessage(chatId, lockDoor(), "");
    }
    if (text == "/unlock") {
      bot.sendMessage(chatId, unlockDoor(), "");
    }
    if (text == "/start") {
      String welcome = "Welcome to the ESP32-CAM Telegram Smart Lock.\n";
      welcome += "/photo : Take a new photo\n";
      welcome += "/unlock : Unlock the Door\n";
      welcome += "/lock : Lock the Door\n";
      bot.sendMessage(chatId, welcome, "Markdown");
    }
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(1000);

  pinMode(LOCK, OUTPUT);
  pinMode(FLASH_LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  digitalWrite(LOCK, LOW);

  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  delay(2000);
  WiFi.begin(ssid, password);

  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    Serial.print(".");
    delay(500);
    wifi_retry++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect to Wi-Fi. Please check SSID/Password.");
    while (true);
  }

  Serial.println();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());

  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_CIF);
}

void loop() {
  if (sendPhoto) {
    Serial.println("Capturing photo...");
    digitalWrite(FLASH_LED, HIGH);
    delay(200);
    sendPhotoTelegram();
    digitalWrite(FLASH_LED, LOW);
    sendPhoto = false;
  }

  if (digitalRead(BUTTON) == LOW) {
    Serial.println("Button pressed. Capturing photo...");
    digitalWrite(FLASH_LED, HIGH);
    delay(200);
    sendPhotoTelegram();
    digitalWrite(FLASH_LED, LOW);
    sendPhoto = false;
  }

  if (millis() - bot_lasttime > BOT_MTBS) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    bot_lasttime = millis();
  }
}
