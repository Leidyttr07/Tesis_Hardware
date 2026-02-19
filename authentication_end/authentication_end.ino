#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <FastLED.h>
#include <ArduinoJson.h>
// ================= CONFIG =================
#define RX2_PIN 16
#define TX2_PIN 17
#define LED_PIN 4
#define NUM_LEDS 21
#define BUZZER_PIN 25
#define BUZZER_RESOLUTION 8
#define BUZZER_BASE_FREQ 2000

// ===== COLORES =====
//#define BLUE    CRGB(0,0,255)
//#define PURPLE  CRGB(128,0,128)
//#define RED     CRGB(255,0,0)

// --- CONFIGURACIÓN DE RED ---
const char* ssid = "CRUZ";
const char* password = "98339345nico";
//const char* serverUrl = "http://192.168.1.6:8000/access/validate"; // Reemplaza con tu IP/Dominio
const char* HOST_IP = "192.168.1.6";
const int HOST_PORT = 8000;

// --- CONFIGURACIÓN OBJECTS ---
HardwareSerial mySerial(2); // RX=16, TX=17
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
CRGB leds[NUM_LEDS];
// ================= STATES =================
enum SystemState {
  STATE_INIT,
  STATE_WIFI_CONNECTING,
  STATE_IDLE,
  STATE_FINGER_DETECTED,
  STATE_SUCCESS,
  STATE_DENIED,
  STATE_WAIT_FINGER_RELEASE
};

SystemState currentState = STATE_INIT;

enum BuzzerPattern {
  BUZZ_NONE,
  BUZZ_IDLE,
  BUZZ_SUCCESS,
  BUZZ_DENIED
};

BuzzerPattern currentPattern = BUZZ_NONE;

enum DeviceStatus {
  DEVICE_ONLINE,
  DEVICE_OFFLINE,
  DEVICE_MAINTENANCE
};

DeviceStatus currentDeviceStatus = DEVICE_OFFLINE;

// ================= HEARTBEAT =================
unsigned long heartbeatTimer = 0;
const unsigned long HEARTBEAT_INTERVAL = 120000; // 30 segundos
bool sensorOk = false;

// ================= TIMERS =================
unsigned long wifiTimer = 0;
unsigned long ledTimer = 0;
unsigned long stateTimer = 0;

// ================= FLAGS =================
bool ledBlinkState = false;
// =================================================
// ================= WIFI ==========================
// =================================================

void handleWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiTimer > 5000) {
      wifiTimer = millis();
      //WiFi.begin(ssid, password);
      WiFi.reconnect();
    }
  }
}

// =================================================
// ================= LED ===========================
// =================================================

void setColor(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

void blinkColor(CRGB color, int interval) {
  if (millis() - ledTimer > interval) {
    ledTimer = millis();
    ledBlinkState = !ledBlinkState;

    if (ledBlinkState)
      setColor(color);
    else
      setColor(CRGB::Black);
  }
}

// =================================================
// ================= BUZZER ========================
// =================================================
unsigned long buzzerTimer = 0;
int buzzerStep = 0;

void buzzerOn(int freq) {
  ledcWriteTone(BUZZER_PIN, freq);
}

void buzzerOff() {
  ledcWriteTone(BUZZER_PIN, 0);
}

void playBuzzer(BuzzerPattern pattern) {
  currentPattern = pattern;
  buzzerStep = 0;
  buzzerTimer = millis();
}
void handleBuzzer() {

  switch (currentPattern) {

    // ================= IDLE =================
    case BUZZ_IDLE:

      if (buzzerStep == 0) {
        buzzerOn(2500);
        buzzerTimer = millis();
        buzzerStep = 1;
      }
      else if (buzzerStep == 1 && millis() - buzzerTimer >= 100) {
        buzzerOff();
        buzzerTimer = millis();
        buzzerStep = 2;
      }
      else if (buzzerStep == 2 && millis() - buzzerTimer >= 120) {
        currentPattern = BUZZ_NONE;
      }

      break;

    // ================= SUCCESS =================
    case BUZZ_SUCCESS:

      if (buzzerStep == 0) {
        buzzerOn(2000);
        buzzerTimer = millis();
        buzzerStep = 1;
      }
      else if (buzzerStep == 1 && millis() - buzzerTimer >= 120) {
        buzzerOff();
        buzzerTimer = millis();
        buzzerStep = 2;
      }
      else if (buzzerStep == 2 && millis() - buzzerTimer >= 150) {
        buzzerOn(3200);
        buzzerTimer = millis();
        buzzerStep = 3;
      }
      else if (buzzerStep == 3 && millis() - buzzerTimer >= 150) {
        buzzerOff();
        buzzerTimer = millis();
        buzzerStep = 4;
      }
      else if (buzzerStep == 4 && millis() - buzzerTimer >= 180) {
        currentPattern = BUZZ_NONE;
      }

      break;

    // ================= DENIED =================
    case BUZZ_DENIED:

      if (buzzerStep == 0) {
        buzzerOn(300);
        buzzerTimer = millis();
        buzzerStep = 1;
      }
      else if (buzzerStep == 1 && millis() - buzzerTimer >= 200) {
        buzzerOff();
        buzzerTimer = millis();
        buzzerStep = 2;
      }
      else if (buzzerStep == 2 && millis() - buzzerTimer >= 200) {
        buzzerOn(300);
        buzzerTimer = millis();
        buzzerStep = 3;
      }
      else if (buzzerStep == 3 && millis() - buzzerTimer >= 200) {
        buzzerOff();
        buzzerTimer = millis();
        buzzerStep = 4;
      }
      else if (buzzerStep == 4 && millis() - buzzerTimer >= 250) {
        currentPattern = BUZZ_NONE;
      }

      break;

    case BUZZ_NONE:
    default:
      break;
  }
}


// =================================================
// ================= BACKEND =======================
// =================================================

void sendToBackend(int user_id, bool success) {

  if (WiFi.status() != WL_CONNECTED) return;
  String serverUrl = "http://" + String(HOST_IP) + ":" + String(HOST_PORT) + "/access/validate";
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["node_mac"] = WiFi.macAddress();

  if (success)
    doc["user_id"] = user_id;
  else
    doc["user_id"] = nullptr;

  char buffer[128];
  serializeJson(doc, buffer);

  http.POST((uint8_t*)buffer, strlen(buffer));
  http.end();
}

// ================================================
// ============= Funcion HEARTBEAT ===============
void sendHeartbeat() {

  if (WiFi.status() != WL_CONNECTED) {
    currentDeviceStatus = DEVICE_OFFLINE;
    return;
  }

  if (!sensorOk) {
    currentDeviceStatus = DEVICE_MAINTENANCE;
  } else {
    currentDeviceStatus = DEVICE_ONLINE;
  }
  
  String url = "http://" + String(HOST_IP) + ":" + String(HOST_PORT) + "/devices/status";
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<192> doc;
  doc["mac_address"] = WiFi.macAddress();
  doc["ip_address"] = WiFi.localIP().toString();

  switch (currentDeviceStatus) {
    case DEVICE_ONLINE:
      doc["status"] = "Online";
      break;
    case DEVICE_OFFLINE:
      doc["status"] = "Offline";
      break;
    case DEVICE_MAINTENANCE:
      doc["status"] = "Maintenance";
      break;
  }

  char buffer[192];
  serializeJson(doc, buffer);

  http.POST((uint8_t*)buffer, strlen(buffer));
  http.end();
}

// =================================================
// ================= SETUP =========================
// =================================================

void setup() {
  Serial.begin(115200);

  ledcAttach(BUZZER_PIN, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
  ledcWriteTone(BUZZER_PIN, 0);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  setColor(CRGB::Blue);
  
  // 1. Inicializar Sensor R503
  mySerial.begin(57600, SERIAL_8N1, RX2_PIN, TX2_PIN);
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Sensor R503 detectado.");
    sensorOk = true;
  } else {
    Serial.println("No se encontró el sensor. Revisa conexiones.");
    //while (1) { delay(1); }
    sensorOk = false;
  }
  
  // 2. Conectar a Wi-Fi
  WiFi.begin(ssid, password);
  currentState = STATE_WIFI_CONNECTING;
  Serial.println("Sistema Unikey de autenticación listo");
}

// =================================================
// ================= LOOP ==========================
// =================================================

void loop() {
  // ================= HEARTBEAT TIMER =================
  if (millis() - heartbeatTimer > HEARTBEAT_INTERVAL) {
      heartbeatTimer = millis();
      sendHeartbeat();
  }
  // ============== Reconectar wifi y buzzer =============
  handleWiFi();
  handleBuzzer();

  switch (currentState) {

    case STATE_WIFI_CONNECTING:

      blinkColor(CRGB::Blue, 500);

      if (WiFi.status() == WL_CONNECTED) {
        setColor(CRGB::Blue);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE);
        currentState = STATE_IDLE;
      }
      break;

    // ============================================
    case STATE_IDLE:

      if (finger.getImage() == FINGERPRINT_OK) {
        currentState = STATE_FINGER_DETECTED;
      }
      break;

    // ============================================
    case STATE_FINGER_DETECTED:
      playBuzzer(BUZZ_IDLE);
      if (finger.image2Tz() == FINGERPRINT_OK &&
          finger.fingerSearch() == FINGERPRINT_OK) {

        currentState = STATE_SUCCESS;
      } else {
        currentState = STATE_DENIED;
      }

      break;

    // ============================================
    case STATE_SUCCESS:

      setColor(CRGB::Green);
      playBuzzer(BUZZ_SUCCESS);
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_PURPLE, 3);
      sendToBackend(finger.fingerID, true);

      stateTimer = millis();
      currentState = STATE_WAIT_FINGER_RELEASE;
      break;

    // ============================================
    case STATE_DENIED:

      setColor(CRGB::Red);
      playBuzzer(BUZZ_DENIED);
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 3); // Rojo
      sendToBackend(-1, false);

      stateTimer = millis();
      currentState = STATE_WAIT_FINGER_RELEASE;
      break;

    // ============================================
    case STATE_WAIT_FINGER_RELEASE:

      if (finger.getImage() == FINGERPRINT_NOFINGER &&
          millis() - stateTimer > 800) {
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE);    
        setColor(CRGB::Blue);
        currentState = STATE_IDLE;
      }
      break;

    default:
      currentState = STATE_IDLE;
      break;
  }
}