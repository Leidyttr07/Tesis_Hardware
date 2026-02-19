#include <WiFi.h>            // Para ESP32, usa <ESP8266WiFi.h> si es ESP8266
#include <HTTPClient.h>      // Para hacer solicitudes HTTP
#include <WebServer.h>       // Para ejecutar el servidor web en el ESP
#include <Adafruit_Fingerprint.h>

// Configura tu red Wi-Fi
const char* ssid = "CRUZ"; 
const char* password = "98339345nico";
//ESP32
const int RX_sensor=16;
const int TX_sensor=17;

// Dirección del servidor backend Java (donde reenviarás los datos)
const char* backendServerURL = "http://192.168.1.6:8000/enroll/callback"; 

//***************SENSOR**************
// Configuramos Serial2 para el sensor dactilar y Serial0 para la comunicación con la PC
HardwareSerial mySerial(2); // Serial2 en ESP32 (pines IO17 para TX y IO16 para RX)

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
int id = -1;
uint8_t fingerTemplate[512]; // El template real
//************************************************


// Crear un servidor web en el puerto 80
WebServer server(80);

void setup() {
  // Inicia el monitor serial
  Serial.begin(115200);
  while (!Serial);  
  delay(100);

  // Conectar a la red Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Conectado a la red WiFi");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());

  // Define la ruta y el manejo del get enrroll start
  server.on("/enroll/start", HTTP_GET, handleEnrollRequest);
  // Define la ruta y manejo de device info
  server.on("/device/info", HTTP_GET, handleDeviceInfo);

  // Iniciar el servidor
  server.begin();
  Serial.println("Servidor Unikey de Registro Iniciado (enroll/start y device/info)");


  // Configuración del puerto serial para el sensor dactilar
  mySerial.begin(57600, SERIAL_8N1, RX_sensor, TX_sensor); // RX = IO16, TX = IO17
  finger.begin(57600);
  //Aseguramos la conexion del sensor  
  if (!finger.verifyPassword()) {
    Serial.println("Sensor no encontrado");
    while (1);
  }
}

void loop() {
  // Procesar las solicitudes entrantes
  server.handleClient();
}


uint8_t getFingerprintEnroll() {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      //finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.print(".");
      break;
    default:
      //finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      //delay(2000);
      //Serial.println("Unknown error");
      break;
    }
  }

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    default:
      Serial.println("Unknown error");
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      delay(2000);
      return p;
  }

  Serial.println("Remove finger");
  delay(10);
  
  
  
  Serial.print("ID "); Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      //finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_PURPLE);
      break;
    default:
      Serial.println("Unknown error");
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      delay(2000);
      break;
    }
  }

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    default:
      Serial.println("Unknown error");
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      delay(2000);
      return p;
  }

  Serial.print("Creating model for #");  Serial.println(id);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else {
    Serial.println("Unknown error");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
    delay(2000);
    return p;
  }

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
  }else {
    Serial.println("Unknown error");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
    delay(2000);
    return p;
  }

  return true;
}

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

// Manejar la solicitud get que llega a "/enroll/start"
void handleEnrollRequest() {

  if (server.hasArg("node_id")) {
    Serial.println("Inicio de enroll solicitado");
    String nodeIdParam = server.arg("node_id");
    int node_id = nodeIdParam.toInt();
    Serial.println("Enroll solicitado para node_id: " + String(node_id));

    // 1️⃣ Obtener ID libre
    id = getFreeID();
    // Si no hay ID libres
    if (id == -1) {
      String jsonError = "{\"node_id\":" + String(node_id) +
                        ",\"status\":\"error\",\"user_id\":-1,"
                        "\"message\":\"Memoria llena\"}";

      forwardPostToBackend(jsonError);
      server.send(500, "application/json", jsonError);
      return;
    }
    // 🔥 AVISAR AL BACKEND QUE LA CAPTURA EMPEZO
    server.send(200, "application/json", "{\"status\":\"started\"}");

    for(int i=0; i<3; i++){
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
      for(int j=0;j<3;j++){
        while (! getFingerprintEnroll() );
      }
      
      delay(100);
      // Esperar que retire el dedo (tu lógica original)
      if (i < 2) {
        String jsonWaiting = "{";
        jsonWaiting += "\"node_id\":" + String(node_id) + ",";
        jsonWaiting += "\"status\":\"waiting\",";
        jsonWaiting += "\"user_id\":" + String(id) + ",";
        jsonWaiting += "\"message\":\"Levante su dedo\"}";
        forwardPostToBackend(jsonWaiting);
      }
      int p = 0;
      while (p != FINGERPRINT_NOFINGER) {
        finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_PURPLE);
        p = finger.getImage();
        delay(10);
      }
    }

    String jsonSuccess = "{";
    jsonSuccess += "\"node_id\":" + String(node_id) + ",";
    jsonSuccess += "\"status\":\"success\",";
    jsonSuccess += "\"user_id\":" + String(id) + ",";
    jsonSuccess += "\"message\":\"Huella registrada correctamente\"}";
    
    Serial.print("JSON a enviar: ");
    Serial.println(jsonSuccess);

    // Avisar al backend que la captura ha sido completada en /callback
    forwardPostToBackend(jsonSuccess);

  } else {
    // Enviar un error si no hay payload
    server.send(400, "application/json", "{\"error\":\"No se encontró el cuerpo de la solicitud\"}");
  }
}

// Función para reenviar el POST al servidor backend Java
bool forwardPostToBackend(String payload) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Inicia una nueva solicitud POST al servidor backend
    http.begin(backendServerURL);
    http.addHeader("Content-Type", "application/json");

    // Enviar la solicitud POST con el JSON recibido
    int httpResponseCode = http.POST(payload);

    // Verificar la respuesta del servidor
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("Código de respuesta del backend: " + String(httpResponseCode));
      Serial.println("Respuesta del backend: " + response);
      http.end();
      return true;


    } else {
      Serial.println("Error al hacer POST al backend");
      http.end();
      return false;
    }

    // Finalizar la solicitud
    
  } else {
    Serial.println("Error: No hay conexión WiFi");
    return false;
  }
}

int getFreeID() {

  finger.getTemplateCount();
  Serial.print("Templates usados: ");
  Serial.println(finger.templateCount);

  for (int i = 1; i <= 127; i++) {

    uint8_t p = finger.loadModel(i);

    if (p == FINGERPRINT_OK) {
      // Está ocupada
      continue;
    } else {
      // Cualquier otro código significa que está libre
      Serial.print("ID libre encontrada: ");
      Serial.println(i);
      return i;
    }
  }

  Serial.println("No hay IDs libres");
  return -1;
}

