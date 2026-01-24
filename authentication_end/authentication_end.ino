#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <FastLED.h>
#include <WebServer.h>
#include <ETH.h>

#define BUZZER_PIN  25   
#define LED_PIN   4
#define NUM_LEDS  72

// ===== COLORES =====
#define BLUE    CRGB(0,0,255)
#define PURPLE  CRGB(128,0,128)
#define RED     CRGB(255,0,0)

// ===== MODOS =====
enum LedMode {
  LED_OFF,
  LED_ON,
  LED_FLASH,
  LED_BREATH
};

CRGB leds[NUM_LEDS];

// Estados del LED para el proceso de AUTENTICACIÓN
enum LedState {
  IDLE,            // Sistema encendido y listo
  WAIT_FINGER,     // Esperando que el usuario coloque el dedo
  FINGER_OK,       // Huella detectada correctamente
  PROCESSING,      // Procesando huella / buscando coincidencia
  SUCCESS,         // Huella válida (acceso permitido)
  DENIED,          // Huella no encontrada / acceso denegado
  ERROR,           // Error del sensor o del sistema
  OFF              // LED apagado
};

void notifyBackendEvent(String event, String message = "");
void sendAuthToBackend(uint16_t id, uint16_t confidence);
const char* fingerprintErrorToString(uint8_t errorCode);

// ================== CONFIGURACIÓN WIFI ==================
const char* ssid = "X3 pro";
const char* password = "a1234567";

// ================== BACKEND ==================
const char* backendServerURL = "http://192.168.224.219:8000/autenticacion";

// ================== SENSOR ==================
#define RX_SENSOR 16
#define TX_SENSOR 17

HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

bool sensorConnected = false;
bool lastSensorState = false;

WebServer server(80);

// ================== SETUP ==================
void setup() {
  Serial.begin(9600);
  delay(100);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  pinMode(BUZZER_PIN, OUTPUT);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");

  server.on("/auth/request", HTTP_POST, authenticateFingerprint);
  server.on("/visitor/access", HTTP_POST, handleVisitorAccess);

  server.begin();

  // Sensor
  mySerial.begin(57600, SERIAL_8N1, RX_SENSOR, TX_SENSOR);
  if (!finger.verifyPassword()) {
    Serial.println("No se encontró el sensor de huellas");
    while (1);
  }

  Serial.println("Sistema de autenticación listo");
  setLedState(IDLE);

}

// ================== LOOP ==================
void loop() {
  
  server.handleClient();

  checkSensor();
}

void checkSensor() {

  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 3000) return;
  lastCheck = now;

  bool currentState = finger.verifyPassword();

  if (currentState != lastSensorState) {

    if (ETH.linkUp()) {
      HTTPClient http;
      http.begin("http://TU_BACKEND/api/sensor/status");
      http.addHeader("Content-Type", "application/json");

      String payload;
      if (currentState) {
        payload = "{\"status\":\"SENSOR_OK\"}";
        Serial.println("Fingerprint sensor connected");
      } else {
        payload = "{\"status\":\"SENSOR_DISCONNECTED\"}";
        Serial.println("Fingerprint sensor disconnected");
      }

      http.POST(payload);
      http.end();
    }

    sensorConnected = currentState;
    lastSensorState = currentState;
  }
}

void buzzer(LedState state) {

  switch (state) {

    case IDLE:
      noTone(BUZZER_PIN);
      break;

    case WAIT_FINGER:
      // Beep-beep-beep lento (espera)
      for (int i = 0; i < 2; i++) {
        tone(BUZZER_PIN, 2300, 130);
        delay(150);
      }
      noTone(BUZZER_PIN);
      break;

    case FINGER_OK:
      // Beep corto agudo (detección)
      tone(BUZZER_PIN, 3000, 100);
      delay(120);
      noTone(BUZZER_PIN);
      break;

    case PROCESSING:
      for (int i = 0; i < 8; i++) {
        tone(BUZZER_PIN, 3000, 60);  // tono suave
        //delay(120);                // pausa corta entre pitidos
      }

    case SUCCESS:
      // Doble beep ascendente (éxito)
      tone(BUZZER_PIN, 2000, 120);
      delay(150);
      tone(BUZZER_PIN, 3200, 150);
      delay(180);
      noTone(BUZZER_PIN);
      break;

    case ERROR:
      // Beep grave largo (error)
      tone(BUZZER_PIN, 200, 700);
      delay(750);
      noTone(BUZZER_PIN);
      break;

    case OFF:
      noTone(BUZZER_PIN);
      break;
  }
}


void setStripLed(LedMode mode, CRGB color) {

  FastLED.setBrightness(100);

  switch (mode) {

    case LED_OFF:
      FastLED.clear();
      FastLED.show();
      break;

    case LED_ON:
      fill_solid(leds, NUM_LEDS, color);
      FastLED.show();
      break;

    case LED_FLASH:
      for (int i = 0; i < 6; i++) {        // parpadeo tipo R503
        fill_solid(leds, NUM_LEDS, color);
        FastLED.show();
        delay(180);
        FastLED.clear();
        FastLED.show();
        delay(180);
      }
      break;

    case LED_BREATH:
      for (int b = 10; b <= 200; b += 5) {
        FastLED.setBrightness(b);
        fill_solid(leds, NUM_LEDS, color);
        FastLED.show();
        delay(20);
      }
      for (int b = 200; b >= 10; b -= 5) {
        FastLED.setBrightness(b);
        fill_solid(leds, NUM_LEDS, color);
        FastLED.show();
        delay(20);
      }
      break;
  }
}

// Controla el LED del sensor según el estado del sistema de autenticación
void setLedState(LedState state) {

  switch (state) {

    case IDLE:
      // Sistema listo, sin interacción
      // Azul en efecto respiración → "Puede autenticarse"
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 0, FINGERPRINT_LED_BLUE);
      setStripLed(LED_BREATH, BLUE);
      break;

    case WAIT_FINGER:
      // Esperando que el usuario coloque el dedo
      // Morado intermitente → "Coloque el dedo"
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_PURPLE, 0);
      setStripLed(LED_FLASH, PURPLE);
      buzzer(WAIT_FINGER);
      break;

    case FINGER_OK:
      // El sensor detectó correctamente la huella
      // Morado fijo → "Huella detectada"
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
      setStripLed(LED_ON, PURPLE);
      buzzer(FINGER_OK);
      break;

    case PROCESSING:
      // Procesando huella (comparación interna)
      // Morado respirando → "Verificando identidad"
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 0, FINGERPRINT_LED_PURPLE);
      setStripLed(LED_BREATH, PURPLE);
      buzzer(PROCESSING);
      break;

    case SUCCESS:
      // Huella válida encontrada
      // Azul fijo → "Acceso permitido"
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE);
      setStripLed(LED_ON, BLUE);
      buzzer(SUCCESS);
      break;

    case DENIED:
      // Huella no encontrada o acceso denegado
      // Rojo parpadeo rápido → "Acceso denegado"
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 6);
      setStripLed(LED_FLASH, RED);
      buzzer(ERROR);
      break;

    case ERROR:
      // Error del sensor o del sistema
      // Rojo parpadeo lento → "Error"
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      setStripLed(LED_FLASH, RED);
      buzzer(ERROR);
      break;

    case OFF:
      // Apagar completamente el LED
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_PURPLE);
      setStripLed(LED_OFF, BLUE);
      buzzer(OFF);
      break;
  }
}



// ================== AUTENTICACIÓN ==================
void authenticateFingerprint() {

  int p = -1;
  while (p != FINGERPRINT_OK) {
    setLedState(WAIT_FINGER);

    uint8_t p = finger.getImage();

    if (p != FINGERPRINT_OK){
      setLedState(ERROR);
      notifyBackendEvent("ERROR", fingerprintErrorToString(p));
      return;
    }
    notifyBackendEvent("FIRST_CAPTURE_OK", "Huella capturada");
    setLedState(FINGER_OK);
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK){
    notifyBackendEvent("AUTH_ERROR", fingerprintErrorToString(p));
    setLedState(ERROR);
    return;
  }
  
  setLedState(PROCESSING);

  p = finger.fingerSearch();

  if (p == FINGERPRINT_OK) {
    setLedState(SUCCESS);
    delay(1500);

    uint16_t id = finger.fingerID;
    uint16_t confidence = finger.confidence;

    notifyBackendEvent("AUTH_SUCCESS", "Huella válida");
    Serial.print("Huella válida - ID: ");
    Serial.print(id);
    Serial.print(" Confianza: ");
    Serial.println(confidence);

    sendAuthToBackend(id, confidence);
    delay(2000);  // evitar múltiples envíos
    server.send(200, "application/json",
                "{\"status\":\"ACCESS_GRANTED\"}");
  }
  else {
    Serial.print("Huella inválida, acceso denegado");
    notifyBackendEvent("ERROR", fingerprintErrorToString(p));
    setLedState(DENIED);
    server.send(401, "application/json",
                "{\"status\":\"ACCESS_DENIED\"}");

  }

  setLedState(OFF);

}


// ================== ENVÍO AL BACKEND ==================
void sendAuthToBackend(uint16_t id, uint16_t confidence) {

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(backendServerURL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"id\":" + String(id) + ",";
  json += "\"confidence\":" + String(confidence);
  json += "}";

  int httpCode = http.POST(json);

  if (httpCode == 200) {
    Serial.println("Autenticación enviada al backend");
  } else {
    setLedState(ERROR);
    Serial.println("Error enviando autenticación");
  }

  http.end();
}

void notifyBackendEvent(String event, String message) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin("http://192.168.224.219:8000/evento_biometrico");
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"evento\":\"" + event + "\"";

  if (message != "") {
    payload += ",\"mensaje\":\"" + message + "\"";
  }

  payload += "}";

  http.POST(payload);
  http.end();
}


const char* fingerprintErrorToString(uint8_t errorCode) {

  switch (errorCode) {

    case FINGERPRINT_IMAGEFAIL:
      return "No se pudo capturar la huella";

    case FINGERPRINT_IMAGEMESS:
      return "Imagen borrosa";

    case FINGERPRINT_FEATUREFAIL:
      return "No se extrajeron características";

    case FINGERPRINT_INVALIDIMAGE:
      return "Imagen inválida";

    case FINGERPRINT_ENROLLMISMATCH:
      return "Las huellas no coinciden";

    case FINGERPRINT_BADLOCATION:
      return "Error en la ubicación / ID inválido";

    case FINGERPRINT_FLASHERR:
      return "No se pudo guardar en memoria";

    case FINGERPRINT_NOTFOUND:
      return "Huella no registrada";

    default:
      return "Error desconocido del sensor";
  }
}

void handleVisitorAccess() {

  Serial.println("Acceso de visitante autorizado");

  // Activar feedback visual y sonoro
  setLedState(SUCCESS);   
  delay(5000);
  setLedState(OFF);

  // Responder al backend
  server.send(200, "application/json",
              "{\"status\":\"Visitor access granted\"}");
}
