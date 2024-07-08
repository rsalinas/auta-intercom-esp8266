#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoOTA.h>

#define GPIO_RING D1
#define GPIO_AUX D2
#define GPIO_OPENER LED_BUILTIN

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h file not found. cp secrets.h.templ secrets.h, and update it with your settings."
#endif

const unsigned long BOT_MTBS = 1000;  // mean time between scan messages

volatile int last_ring = 0;
volatile int last_aux = 0;
volatile int interruptFlagAux = 0;

bool autoOpen = false;

time_t lastOpen = 0;

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
unsigned long bot_lasttime;  // last time messages' scan has been done

void openDoor(String chat_id, int message_id = 0) {
  bot.sendMessage(chat_id, String("Opening door...") + message_id, "");
  bot.sendMessage(chat_id, "Opening door2...", "", message_id);
  digitalWrite(GPIO_OPENER, HIGH);
  delay(1000);
  digitalWrite(GPIO_OPENER, LOW);
  lastOpen = time(nullptr);
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    Serial.println(bot.messages[i].chat_id);
    const auto text = bot.messages[i].text;
    if (text == "/help" || text == "/start") {
      bot.sendMessage(bot.messages[i].chat_id, "Help:\n"
                                               "/open Opens door\n"
                                               "/enable_autoopen - Enables automatic opening",
                      "");
      continue;
    }

    if (text.startsWith("/open")) {
      openDoor(bot.messages[i].chat_id, bot.messages[i].message_id);
      continue;
    }

    bot.sendMessage(bot.messages[i].chat_id, "?? " + text, "");
  }
}




void IRAM_ATTR handleInterruptAuxLine() {
  interruptFlagAux = true;
}

void setup() {
  //WiFi.persistent(true);  // Enable persistent storage
  pinMode(GPIO_RING, INPUT_PULLUP);
  pinMode(GPIO_AUX, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GPIO_AUX), handleInterruptAuxLine, RISING);
  pinMode(GPIO_OPENER, OUTPUT);
  digitalWrite(GPIO_OPENER, LOW);

  Serial.begin(115200);
  Serial.println();

  Serial.println(String("Connecting to Wifi SSID: ") + WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  secured_client.setTrustAnchors(&cert);  // Add root certificate for api.telegram.org

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Error connecting wifi");
    delay(3000);
  }
  Serial.print("WiFi connected. IP address: ");
  Serial.println(WiFi.localIP());


  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_SPIFFS
      type = "filesystem";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();

  Serial.println(String("Wifi ready in ") + millis());

  Serial.print("Retrieving time: ");
  configTime(1, 1, "pool.ntp.org");
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(String("NTP Ready in ") + millis());

  Serial.println(now);

  struct tm timeinfo;
  char buffer[80];
  if (gmtime_r(&now, &timeinfo)) {

    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.println(buffer);
  } else {
    Serial.println("Failed to obtain time");
  }
  Serial.println("Sending startup message...");
  if (bot.sendMessage(OWNER_CHATID, String("UpOTA2 ") + String(buffer), "")) {
    Serial.println("msg sent ok...");

  } else {
    Serial.println("error sending msg...");
  }
  //bot.longPoll = 5;
  Serial.println(String("longpoll: ") + bot.longPoll);
  Serial.println(String("Everything ready in ") + millis());
}


void loop() {
  ArduinoOTA.handle();
  int current_ring = digitalRead(GPIO_RING);
  if (current_ring != last_ring) {
    Serial.println(String("ring ") + current_ring);
    bot.sendMessage(OWNER_CHATID, String("ring ") + current_ring, "");
    last_ring = current_ring;
    if (current_ring && lastOpen) {
      if (time(nullptr) - lastOpen < 120) {
        Serial.println(String("reopening"));
        bot.sendMessage(OWNER_CHATID, "reopening", "");
        openDoor(OWNER_CHATID);
      }
    }
  }
  int current_aux = digitalRead(GPIO_AUX);
  if (current_aux != last_aux) {
    Serial.println(String("aux ") + current_aux);
    bot.sendMessage(OWNER_CHATID, String("aux ") + current_aux, "");
    last_aux = current_aux;
  }

  if (interruptFlagAux) {
    interruptFlagAux = 0;
    Serial.println(String("aux interrupt"));
  }
  if (true) {
    if (millis() - bot_lasttime > BOT_MTBS) {
      int numNewMessages;
      do {

        int startts = millis();
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        Serial.println(String("polling tg: ") + (millis() - startts));
        handleNewMessages(numNewMessages);
      } while (numNewMessages);

      bot_lasttime = millis();
      // digitalWrite(LED_BUILTIN, LOW);
      // delay(100);
      // digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}
