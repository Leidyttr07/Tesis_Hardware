#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Adafruit_Fingerprint.h>
#include <FastLED.h>

const char* ssid     = "Galaxy J667DF";
const char* password = "lmz12345";

#define LED_PIN            4
#define NUM_LEDS           21
#define BUZZER_PIN         25
#define BUZZER_RESOLUTION  8
#define BUZZER_BASE_FREQ   2000
#define LED_AUX            22

const int RX_sensor = 16;
const int TX_sensor = 17;

const char* backendServerURL = "http://192.168.43.119:8000/enroll/callback";

// ── Hardware ────────────────────────────────────────────────────────────────
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
WebServer server(80);
CRGB leds[NUM_LEDS];

// ── Estado global ────────────────────────────────────────────────────────────
int id = -1;

// =============================================================================
// LEDs / Buzzer
// =============================================================================
void setColor(CRGB color) {
  FastLED.clear();
  leds[0] = leds[6] = leds[12] = leds[19] = color;
  FastLED.show();
}

void beep(int freq, int duration) {
  ledcWriteTone(BUZZER_PIN, freq);
  delay(duration);
  ledcWriteTone(BUZZER_PIN, 0);
}

void beepSuccess() { beep(2000, 150); beep(3200, 150); }
void beepError()   { beep(400, 300); }
void beepWait()    { beep(2500, 100); }

// =============================================================================
// Helpers
// =============================================================================
String buildEnrollJson(int node_id, const String& status, int user_id, const String& message) {
  return "{\"node_id\":" + String(node_id) +
         ",\"status\":\"" + status + "\"" +
         ",\"user_id\":" + String(user_id) +
         ",\"message\":\"" + message + "\"}";
}

bool forwardPostToBackend(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] Sin conexión WiFi");
    return false;
  }
  HTTPClient http;
  http.begin(backendServerURL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  String resp = http.getString();
  http.end();
  Serial.printf("[HTTP] POST %s → %d | %s\n", backendServerURL, code, resp.c_str());
  return (code == 200);
}

// Espera hasta que NO haya dedo en el sensor (timeout en ms, 0 = infinito)
void waitFingerRemoved(unsigned long timeoutMs = 8000) {
  unsigned long start = millis();
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    if (timeoutMs && (millis() - start > timeoutMs)) break;
    delay(20);
  }
}

// Espera hasta que haya un dedo en el sensor (retorna true si detectó dedo)
bool waitFingerPresent(unsigned long timeoutMs = 15000) {
  unsigned long start = millis();
  while (true) {
    int p = finger.getImage();
    if (p == FINGERPRINT_OK) return true;
    if (timeoutMs && (millis() - start > timeoutMs)) return false;
    delay(20);
  }
}

int getFreeID() {
  finger.getTemplateCount();
  Serial.printf("[INFO] Templates usados: %d\n", finger.templateCount);
  for (int i = 1; i <= 127; i++) {
    if (finger.loadModel(i) != FINGERPRINT_OK) {
      Serial.printf("[INFO] ID libre: %d\n", i);
      return i;
    }
  }
  Serial.println("[ERROR] Memoria del sensor llena");
  return -1;
}

// =============================================================================
// Captura de una muestra completa (imagen → feature → slot)
// slot = 1 o 2  |  retorna true en éxito
// =============================================================================
bool captureFeature(uint8_t slot) {
  Serial.printf("[ENROLL] Capturando muestra %d…\n", slot);

  // Esperar dedo (timeout 15 s)
  if (!waitFingerPresent(15000)) {
    Serial.println("[ERROR] Timeout esperando dedo");
    return false;
  }
  Serial.println("[ENROLL] Imagen capturada");

  uint8_t p = finger.image2Tz(slot);
  if (p != FINGERPRINT_OK) {
    Serial.printf("[ERROR] image2Tz(%d) = %d\n", slot, p);
    return false;
  }
  Serial.printf("[ENROLL] Muestra %d convertida OK\n", slot);
  return true;
}

// =============================================================================
// Proceso completo de una iteración de enroll (3 capturas dobles = 3 modelos)
// Retorna true si la huella quedó guardada en el sensor con el id global
// =============================================================================
bool enrollOnce() {
  // -- Muestra 1 --
  if (!captureFeature(1)) return false;

  Serial.println("[ENROLL] Retire el dedo…");
  beepWait();
  setColor(CRGB::Black);
  finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_BLUE);
  String jsonWaiting = buildEnrollJson(201, "waiting", id, "Levante su dedo");
  forwardPostToBackend(jsonWaiting);
  waitFingerRemoved(8000);
  delay(1000);

  // -- Muestra 2 --
  setColor(CRGB::Blue);
  finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE);
  if (!captureFeature(2)) return false;

  // -- Crear y guardar modelo --
  uint8_t p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.printf("[ERROR] createModel = %d\n", p);
    return false;
  }

  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    Serial.printf("[ERROR] storeModel = %d\n", p);
    return false;
  }

  Serial.printf("[ENROLL] Iteración OK → ID %d\n", id);
  return true;
}

// =============================================================================
// Handler: GET /enroll/start?node_id=XX
// =============================================================================
void handleEnrollRequest() {
  if (!server.hasArg("node_id")) {
    server.send(400, "application/json", "{\"error\":\"Falta node_id\"}");
    return;
  }

  int node_id = server.arg("node_id").toInt();
  Serial.printf("[ENROLL] Solicitud para node_id=%d\n", node_id);

  // 1. Buscar ID libre en el sensor
  id = getFreeID();
  if (id == -1) {
    String err = buildEnrollJson(node_id, "error", -1, "Memoria del sensor llena");
    forwardPostToBackend(err);
    server.send(500, "application/json", err);
    return;
  }

  // 2. Confirmar inicio al cliente HTTP (no bloquea al backend)
  server.send(200, "application/json", "{\"status\":\"started\"}");

  // 3. Tres iteraciones de captura doble
  const int TOTAL_ROUNDS = 3;
  for (int round = 0; round < TOTAL_ROUNDS; round++) {
    Serial.printf("\n[ENROLL] === Ronda %d/%d ===\n", round + 1, TOTAL_ROUNDS);

    setColor(CRGB::Blue);
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE);

    bool ok = enrollOnce();

    if (!ok) {
      // ── ERROR: abortar, borrar template parcial y notificar backend ──
      Serial.println("[ERROR] Fallo en captura, abortando enroll");
      finger.deleteModel(id);          // Limpiar lo que pudo quedar guardado
      setColor(CRGB::Red);
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      beepError();
      delay(2000);
      setColor(CRGB::Black);

      String errJson = buildEnrollJson(node_id, "error", id, "Fallo al capturar la huella");
      forwardPostToBackend(errJson);
      id = -1;
      return;                          // Salir sin continuar
    }

    // ── Éxito de ronda ──
    if (round < TOTAL_ROUNDS - 1) {
      // Rondas intermedias: pedir que levante el dedo
      beepWait();
      setColor(CRGB::Black);
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_BLUE);

      String jsonWaiting = buildEnrollJson(node_id, "waiting", id, "Levante su dedo");
      forwardPostToBackend(jsonWaiting);

      waitFingerRemoved(8000);
    } else {
      // Última ronda
      setColor(CRGB::Green);
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_BLUE);

      beepSuccess();
    }
  }

  // 4. Notificar éxito al backend
  String jsonSuccess = buildEnrollJson(node_id, "success", id, "Huella registrada correctamente");
  Serial.println("[ENROLL] Enviando success → " + jsonSuccess);

  if (!forwardPostToBackend(jsonSuccess)) {
    // El sensor ya guardó la huella pero el backend falló → log crítico
    Serial.println("[CRÍTICO] Backend no confirmó el registro. ID sensor: " + String(id));
    // Opcional: borrar del sensor si la BD no confirma
    finger.deleteModel(id);
    setColor(CRGB::Red);
    beepError();
    delay(3000);
  }

  delay(3000);
  setColor(CRGB::Black);
  id = -1;
}

// =============================================================================
// Handler: GET /device/info
// =============================================================================
void handleDeviceInfo() {
  String response = "{\"device_type\":\"ESP32_ACCESS_NODE\","
                    "\"ip_address\":\"" + WiFi.localIP().toString() + "\","
                    "\"mac_address\":\"" + WiFi.macAddress() + "\"}";
  server.send(200, "application/json", response);
  Serial.println("[INFO] /device/info solicitado");
  digitalWrite(LED_AUX, LOW);  delay(500);
  digitalWrite(LED_AUX, HIGH); delay(500);
  digitalWrite(LED_AUX, LOW);
  beepSuccess();
}

// =============================================================================
// Setup / Loop
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  ledcAttach(BUZZER_PIN, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
  ledcWriteTone(BUZZER_PIN, 0);

  pinMode(LED_AUX, OUTPUT);
  digitalWrite(LED_AUX, HIGH);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Conectando");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[WiFi] Conectado → " + WiFi.localIP().toString());

  // Rutas
  server.on("/enroll/start", HTTP_GET, handleEnrollRequest);
  server.on("/device/info",  HTTP_GET, handleDeviceInfo);
  server.begin();
  Serial.println("[SERVER] Servidor Unikey iniciado");

  // Sensor dactilar
  mySerial.begin(57600, SERIAL_8N1, RX_sensor, TX_sensor);
  finger.begin(57600);
  if (!finger.verifyPassword()) {
    Serial.println("[ERROR] Sensor dactilar no encontrado");
    while (1) { delay(1000); }
  }
  Serial.println("[SENSOR] Sensor dactilar OK");
}

void loop() {
  server.handleClient();
}




