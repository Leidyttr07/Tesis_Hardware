#include <WiFi.h>            // Para ESP32, usa <ESP8266WiFi.h> si es ESP8266
#include <HTTPClient.h>      // Para hacer solicitudes HTTP
#include <WebServer.h>       // Para ejecutar el servidor web en el ESP
#include <Adafruit_Fingerprint.h>
#include <FastLED.h>
#include <ETH.h>

#define BUZZER_PIN  25   
#define LED_PIN   4
#define NUM_LEDS  72

// ===== COLORES TIRA LED =====

#define BLUE    CRGB(0,0,255)
#define PURPLE  CRGB(128,0,128)
#define RED     CRGB(255,0,0)

CRGB leds[NUM_LEDS];


// ===== MODOS TIRA LED=====
enum LedMode {
  LED_OFF,
  LED_ON,
  LED_FLASH,
  LED_BREATH
};

enum SystemState {
  SYSTEM_IDLE,             // Sistema listo / en espera
  REGISTRATION_STARTED,    // Llega POST / inicio de registro
  WAITING_FINGER,          // Esperando que pongan el dedo
  FINGER_CAPTURED,         // Huella capturada correctamente
  REMOVE_FINGER,           // Pida retirar el dedo
  PROCESSING,              // Creando modelo / descargando template
  SUCCESS,                 // Registro exitoso
  ERROR_STATE,              // Error 
  OFF            // LED completamente apagado
};

//// ===== PROTOTIPOS DE FUNCIONES =====

// Core
void checkSensor();
void buzzer(SystemState state);
void setStripLed(LedMode mode, CRGB color);
void setLedState(SystemState state);

// Web server handlers
void handlePostRequest();
void handleVisitorAccess();

// Fingerprint – registro
uint8_t getFingerprintEnroll();
uint16_t findFreeID();
bool downloadFingerprintTemplate(uint16_t id);

// Fingerprint – utilidades
String fingerprintToHexString(uint8_t *data, size_t length);
const char* fingerprintErrorToString(uint8_t errorCode);

// Backend / red
bool forwardPostToBackend(String payload);
void notifyBackendEvent(String event, String message = "");

// Configura tu red Wi-Fi
const char* ssid = "X3 pro";      // Reemplaza con el nombre de tu red Wi-Fi
const char* password = "a1234567"; // Reemplaza con la contraseña de tu red Wi-Fi
//WT32 ETH01
/*
const int RX_sensor=5;
const int TX_sensor=17;
*/
//ESP32
const int RX_sensor=16;
const int TX_sensor=17;

// Dirección del servidor backend Java (donde reenviarás los datos)
const char* backendServerURL = "http://192.168.224.219:8000"; 
//const char* backendServerURL = "http://192.168.224.85:8081/huella"; 
//const char* SENSOR_STATUS_URL = "http://192.168.224.219:8000/sensor_estado";
//const char* EVENT_URL         = "http://192.168.224.219:8000/evento_biometrico";


//***************SENSOR**************
bool sensorConnected = false;
bool lastSensorState = false;

// Configuramos Serial2 para el sensor dactilar y Serial0 para la comunicación con la PC
HardwareSerial mySerial(2); // Serial2 en ESP32 (pines IO17 para TX y IO16 para RX)

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
uint16_t id;
uint8_t fingerTemplate[512]; // El template real
//************************************************


// Crear un servidor web en el puerto 80
WebServer server(80);

void setup() {
  // Inicia el monitor serial
  Serial.begin(9600);
  while (!Serial);  
  delay(100);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  pinMode(BUZZER_PIN, OUTPUT);
  
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

  // Define la ruta y el manejo del GET en enrolamiento e info device
  server.on("/device/info", HTTP_GET, handleDeviceInfo);
  server.on("/enroll/start", HTTP_GET, handlePostRequest);
  //server.on("/receive", HTTP_POST, handlePostRequest);
  server.on("/visitor/access", HTTP_POST, handleVisitorAccess); // 

  // Iniciar el servidor
  server.begin();
  Serial.println("Servidor iniciado, esperando POST en /receive");

  // Configuración del puerto serial para el sensor dactilar
  mySerial.begin(57600, SERIAL_8N1, RX_sensor, TX_sensor); // RX = IO16, TX = IO17
  //Aseguramos la conexion del sensor  
  while (!finger.verifyPassword()) { 
    Serial.println("Did not find fingerprint sensor :("); 
    delay(500); 
  }
  setLedState(SYSTEM_IDLE);
}

void loop() {
  // Procesar las solicitudes entrantes
  server.handleClient();
  //Estado del sensor
  checkSensor();
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

void checkSensor() {

  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (now - lastCheck < 3000) return;
  lastCheck = now;

  bool currentState = finger.verifyPassword();

  if (currentState != lastSensorState) {

    if (ETH.linkUp()) {
      HTTPClient http;
      http.begin(backendServerURL + "/devices/status");
      http.addHeader("Content-Type", "application/json");
      
      String mac = ETH.macAddress();
      String payload;
      if (currentState) {
        payload = "{\"mac_address\":\"" + mac + "\",\"status\":\"Online\"}"; // SENSOR_OK
        Serial.println("Fingerprint sensor connected");
      } else {
        payload = "{\"mac_address\":\"" + mac + "\",\"status\":\"Offline\"}"; // SENSOR_DISCONNECTED
        Serial.println("Fingerprint sensor disconnected");
      }

      http.POST(payload);
      http.end();
    }

    sensorConnected = currentState;
    lastSensorState = currentState;
  }
}


void buzzer(SystemState state) {

  switch (state) {

    case SYSTEM_IDLE:
      noTone(BUZZER_PIN);
      break;

    case WAITING_FINGER:
      // Beep-beep-beep lento (espera)
      for (int i = 0; i < 2; i++) {
        tone(BUZZER_PIN, 2300, 130);
        delay(150);
      }
      noTone(BUZZER_PIN);
      break;

    case FINGER_CAPTURED:
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
      break;

    case SUCCESS:
      // Doble beep ascendente (éxito)
      tone(BUZZER_PIN, 2000, 120);
      delay(150);
      tone(BUZZER_PIN, 3200, 150);
      delay(180);
      noTone(BUZZER_PIN);
      break;

    case ERROR_STATE:
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

void setLedState(SystemState state) {
  switch (state) {

    case SYSTEM_IDLE:
      // Sistema encendido, esperando solicitudes
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 0, FINGERPRINT_LED_BLUE);
      setStripLed(LED_BREATH, BLUE);
      break;

    case REGISTRATION_STARTED:
      // Registro iniciado
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
      setStripLed(LED_ON, PURPLE);
      break;

    case WAITING_FINGER:
      // Esperando que el usuario ponga el dedo
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_PURPLE, 0);
      setStripLed(LED_FLASH, PURPLE);
      buzzer(WAITING_FINGER);
      break;

    case FINGER_CAPTURED:
      // Huella detectada correctamente
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE);
      setStripLed(LED_ON, PURPLE);
      buzzer(FINGER_CAPTURED);
      break;

    case REMOVE_FINGER:
      // Pedir retirar el dedo
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_BLUE, 0);
      setStripLed(LED_FLASH, BLUE);
      break;

    case PROCESSING:
      // Procesamiento interno (crear modelo, descargar template)
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 0, FINGERPRINT_LED_PURPLE);
      setStripLed(LED_BREATH, PURPLE);     
      buzzer(PROCESSING); 
      break;

    case SUCCESS:
      // Registro exitoso
      finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_BLUE);
      setStripLed(LED_ON, BLUE);
      buzzer(SUCCESS);     
      break;

    case ERROR_STATE:
      // Error crítico
      finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 10);
      setStripLed(LED_FLASH, RED);
      buzzer(ERROR_STATE);
      break;

    case OFF:
      // Apagar completamente el LED del sensor
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_BLUE);
      setStripLed(LED_OFF, BLUE);
      buzzer(OFF);
      break;

    default:
      finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_BLUE);
      setStripLed(LED_OFF, BLUE);
      setStripLed(LED_OFF, BLUE);
      buzzer(OFF);
      break;
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

bool downloadFingerprintTemplate(uint16_t id)  { /* true → plantilla descargada correctamente
                                                  false → error en algún paso*/
  Serial.println("------------------------------------");
  Serial.print("Attempting to load #"); Serial.println(id);
  uint8_t p = finger.loadModel(id); //CARGAR EL MODELO DESDE FLASH A RAM DEL SENSOR
  switch (p) {
    case FINGERPRINT_OK:
      Serial.print("Template "); Serial.print(id); Serial.println(" loaded");
      break;
    default:
      Serial.print("Unknown error "); Serial.println(p);
      //setLedState(ERROR_STATE);
      delay(2000);
      return false;
  }

  Serial.print("Attempting to get #"); Serial.println(id);
  p = finger.getModel(); //Envíar por UART la plantilla que tiene cargada en RAM
  switch (p) {
    case FINGERPRINT_OK:
      Serial.print("Template "); Serial.print(id); Serial.println(" transferring:");
      break;
    default:
      Serial.print("Unknown error "); Serial.println(p);
      //setLedState(ERROR_STATE);
      delay(2000);
      return false;
  }

  uint8_t bytesReceived[534]; // 2 paquetes de datos
  memset(bytesReceived, 0xff, 534);   // memset(destino, valor, tamaño);


  uint32_t starttime = millis();
  int i = 0;
  while (i < 534 && (millis() - starttime) < 20000) {
    if (mySerial.available()) {
      bytesReceived[i++] = mySerial.read();
    }
  }
  Serial.print(i); Serial.println(" bytes read.");
  Serial.println("Decoding packet...");

  
  memset(fingerTemplate, 0xff, 512);

  // Filtrando solo los paquetes de datos
  int uindx = 9, index = 0;
  memcpy(fingerTemplate + index, bytesReceived + uindx, 256);   // Primeros 256 bytes
  // Copia 256 bytes desde bytesReceived[9] y pégalos en fingerTemplate[0]*/
  uindx += 256;
  uindx += 2;    // Saltar el checksum
  uindx += 9;    // Saltar el siguiente header
  index += 256;
  memcpy(fingerTemplate + index, bytesReceived + uindx, 256);   // Siguientes 256 bytes
  //Copia 256 bytes desde bytesReceived[276] y pégalos en fingerTemplate[256]
  Serial.println("\ndone.");

  return true;
}

uint16_t findFreeID() {
  finger.getParameters();               // Actualiza parámetros del sensor
  uint16_t maxID = finger.capacity;

  Serial.print("Capacidad del sensor: ");
  Serial.println(maxID);

  for (uint16_t i = 1; i <= maxID; i++) {
    uint8_t p = finger.loadModel(i);

    if (p != FINGERPRINT_OK) {
      Serial.print("ID libre encontrado: ");
      Serial.println(i);
      return i;
    }
  }

  Serial.println("ERROR: Sensor lleno");
  return 0; // 0 = sin espacio
}

uint8_t getFingerprintEnroll() {  //devuelve códigos del sensor

  id = findFreeID();
  if (id == 0) {
    server.send(500, "application/json",
                "{\"error\":\"Memoria del sensor llena\"}");
    return 0;
  }

  int p = -1;
  
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);

  while (p != FINGERPRINT_OK) {
    setLedState(WAITING_FINGER); //"Esperando que el usuario ponga el dedo (primera vez) -> "Coloque el dedo""

    p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      setLedState(FINGER_CAPTURED);
      Serial.println("Image taken");
      notifyBackendEvent("waiting_second_scan", "Primera huella capturada");
    } 
    else if (p != FINGERPRINT_NOFINGER) {
      notifyBackendEvent("ERROR", fingerprintErrorToString(p));
      setLedState(ERROR_STATE);
      delay(2000);
    }
  }

  p = finger.image2Tz(1); // Convierte la imagen capturada en un vector biométrico, Se guarda en buffer interno #1 del sensor

  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    default:
      notifyBackendEvent("ERROR", fingerprintErrorToString(p));
      Serial.println("error");
      setLedState(ERROR_STATE);
      delay(2000);
      return p;
  }

  Serial.println("Remove finger");
  setLedState(REMOVE_FINGER); //“Retire el dedo”
  delay(10);
  
  Serial.print("ID "); Serial.println(id);
  p = -1;

  Serial.println("Place same finger again");
  
  while (p != FINGERPRINT_OK) {
    setLedState(WAITING_FINGER); //“Coloque el mismo dedo nuevamente”
    p = finger.getImage();

    if (p == FINGERPRINT_OK) {
      setLedState(FINGER_CAPTURED);
      //notifyBackendEvent("success", "Segunda huella capturada");
      Serial.println("Image taken");
    } 
    else if (p != FINGERPRINT_NOFINGER) {
      notifyBackendEvent("ERROR", fingerprintErrorToString(p));
      setLedState(ERROR_STATE);
      delay(2000);
    }
  }

  p = finger.image2Tz(2);

  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      notifyBackendEvent("SECOND_CAPTURE_OK", "Segunda huella capturada");
      break;
    default:
      notifyBackendEvent("ERROR", fingerprintErrorToString(p));
      Serial.println("error");
      setLedState(ERROR_STATE);
      delay(2000);
      return p;
  }

  Serial.print("Creating model for #");  Serial.println(id);
  setLedState(PROCESSING);

  p = finger.createModel();  /* El sensor compara: Buffer 1 y Buffer 2, si son suficientemente similares:
                                    Fusiona ambos
                                    Crea UNA plantilla biométrica estable
                                Si NO coinciden: Huella inconsistente*/
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else {
    Serial.println("Unknown error");
    notifyBackendEvent("ERROR", fingerprintErrorToString(p));
    setLedState(ERROR_STATE);
    delay(2000);
    return p;
  }

  p = finger.storeModel(id); // GUARDAR EN MEMORIA FLASH DEL SENSOR

  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    notifyBackendEvent("ENROLL_SUCCESS", "Huella registrada correctamente");
    setLedState(SUCCESS); //“Registro completado correctamente”
  }else {
    Serial.println("Unknown error");
    notifyBackendEvent("ERROR", fingerprintErrorToString(p));
    setLedState(ERROR_STATE);
    delay(2000);
    return p;
  }
  return true;
}


String fingerprintToHexString(uint8_t *data, size_t length) {
  String hexString = "";
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 16) hexString += "0";
    hexString += String(data[i], HEX);
  }
  return hexString;
}


// Manejar la solicitud POST que llega a "/receive"
void handlePostRequest() {

  if (server.hasArg("node_id")) {                 //Verificar que el POST tenga cuerpo (payload) -> "plain" es el body completo del POST
    setLedState(REGISTRATION_STARTED); //"Llega solicitud POST (inicio del registro) -> "registro en curso" "

    String node_id = server.arg("node_id");  // Obtener el cuerpo de la solicitud POST como cedula
    Serial.println("Nodo recibido:");
    Serial.println(node_id); // Imprimir la cédula recibida

    // Empezar a construir el JSON que irá al backend
    String jsonToSend = "{\"template\":{";          //Esto crea el inicio del JSON:  
    jsonToSend += "\"node_id\":\"" + node_id + "\",";  
    jsonToSend += "\"status\":\"success""\",";  
    jsonToSend += "\"user_id\":" + String(id) + ",";
    
    while (! getFingerprintEnroll() );  // Bloquea el servidor: Mientras no haya huella válida, el ESP32 no responde HTTP
      if(downloadFingerprintTemplate(id)){   /* Esa función devuelve: true → si TODO salió bien
                                                                        false → si falló algo
                                                                        Entonces este if significa: “Si se pudo extraer correctamente la plantilla del sensor…” */
          //Carga el modelo desde su memoria, Lo envía por UART, El ESP32 lo reconstruye en fingerTemplate[512]
          //send fingerprint
        String fingerprintHex = fingerprintToHexString(fingerTemplate, 512); //Convertir la plantilla (binario) a hexadecimal
        jsonToSend += "\"template\":\""+ fingerprintHex +"\"}}"; //lo termina el json
      }
      delay(100);
      /*int p = 0;
      while (p != FINGERPRINT_NOFINGER) {
        p = finger.getImage();          /*Le dice al sensor: “Intenta capturar una imagen de huella AHORA”, El sensor responde con un código de estado:
                                          FINGERPRINT_OK        // Hay un dedo y la imagen se capturó bien
                                          FINGERPRINT_NOFINGER  // NO hay dedo sobre el sensor
                                          FINGERPRINT_IMAGEFAIL // Error al capturar imagen
                                          FINGERPRINT_PACKETRECIEVEERR // Error de comunicación 

        delay(10);
      }
      */
    Serial.print("JSON a enviar: ");
    Serial.println(jsonToSend);

    // Reenviar el JSON al backend Java
    if(forwardPostToBackend(jsonToSend)){   /*  Intenta hacer un HTTP POST al backend, Envía el JSON, Espera respuesta
                                                  Devuelve true → backend respondió bien
                                                  Devuelve false → hubo error (red, backend caído, timeout, etc.) */
      server.send(200, "application/json", "{\"message\":\"POST recibido y reenviado\"}"); //“Recibí tu solicitud, hice todo el proceso biométrico y la reenvié correctamente al servidor central.”
      // Responder al cliente que el POST fue recibido
      // setLedState(SUCCESS);
    }else{
      server.send(400, "application/json", "{\"error\":\"Error de comunicación con el servidor de registro\"}"); //“Yo sí recibí tu solicitud pero NO pude comunicarme con el servidor central”
    }

    setLedState(OFF);

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

void notifyBackendEvent(String event, String message) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin("http://192.168.224.219:8000/evento_biometrico");
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"status\":\"" + event + "\"";

  if (message != "") {
    payload += ",\"message\":\"" + message + "\"";
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


