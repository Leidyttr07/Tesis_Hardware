#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ===== WIFI CONFIG =====
const char* ssid = "IESCOM-ORTIZ";
const char* password = "JuanManuel2026";

// ===== BACKEND CONFIG =====
const char* BACKEND_CALLBACK_URL = "http://192.168.18.209:8000/enroll/callback";

// ===== SERVER =====
WebServer server(80);

// ===== UTILS =====
void sendEnrollCallback(int node_id, String status, int user_id = -1, String message = "");

// ========================
// SETUP
// ========================
void setup() {
  Serial.begin(115200);
  delay(100);

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  digitalWrite(2, LOW);
  // ===== ROUTES =====
  server.on("/device/info", HTTP_GET, handleDeviceInfo);
  server.on("/enroll/start", HTTP_GET, handleEnrollStart);

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();
}

// ========================
// HANDLERS
// ========================

// ---- DEVICE INFO ----
void handleDeviceInfo() {
  String response = "{";
  response += "\"device_type\":\"ESP32_ACCESS_NODE\",";
  response += "\"ip_address\":\"" + WiFi.localIP().toString() + "\",";
  response += "\"mac_address\":\"" + WiFi.macAddress() + "\"";
  response += "}";

  Serial.println("[INFO] /device/info solicitado");
  server.send(200, "application/json", response);
}

// ---- ENROLL START ----
void handleEnrollStart() {
  if (!server.hasArg("node_id")) {
    server.send(400, "application/json", "{\"error\":\"node_id requerido\"}");
    return;
  }

  int node_id = server.arg("node_id").toInt();
  
  // Respondemos 200 OK inmediatamente para liberar al backend
  server.send(200, "application/json", "{\"message\":\"Enroll iniciado\"}");
  
  // Nota: En un entorno real, aquí activarías una bandera para que el loop() haga el enroll.
  // Para la simulación, podemos forzar el envío pero es vital que el server.send haya ocurrido.
}

// ========================
// CALLBACK TO BACKEND
// ========================
void sendEnrollCallback(int node_id, String status, int user_id, String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] WiFi desconectado");
    return;
  }

  HTTPClient http;
  http.begin(BACKEND_CALLBACK_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"node_id\":" + String(node_id) + ",";
  payload += "\"status\":\"" + status + "\"";

  if (user_id >= 0) {
    payload += ",\"user_id\":" + String(user_id);
  }

  if (message != "") {
    payload += ",\"message\":\"" + message + "\"";
  }

  payload += "}";

  Serial.println("[CALLBACK] Enviando a backend:");
  Serial.println(payload);

  int httpCode = http.POST(payload);

  Serial.print("[CALLBACK] HTTP code: ");
  Serial.println(httpCode);

  http.end();
}

