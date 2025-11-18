

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include "time.h"  // Manejo del tiempo


// --- PINES ---
#define SS_PIN 5
#define RST_PIN 27
#define LED_VERDE 21  // LED para indicar éxito
#define LED_ROJO 22   // LED para indicar error


MFRC522 mfrc522(SS_PIN, RST_PIN);


// --- CONFIGURACIÓN DE RED Y SERVIDOR ---
const char* ssid = "PEINE-3";
const char* password = "etecPeine3";
const char* pocketbaseUrl = "http://10.56.2.3:8090";


// ---CONFIGURACIÓN DE TIEMPO (NTP) ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600;  // Offset para Argentina (GMT-3)
const int daylightOffset_sec = 0;      // Sin horario de verano


// --- FUNCIÓN PARA OBTENER EL UID DEL LLAVERO ---
String getUidString() {
 String uid = "";
 for (byte i = 0; i < mfrc522.uid.size; i++) {
   uid.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
   uid.concat(String(mfrc522.uid.uidByte[i], HEX));
 }
 uid.toUpperCase();
 return uid;
}


// --- FUNCIÓN PARA MANEJAR LOS LEDS ---
void setLedState(int ledPin, bool on) {
 digitalWrite(ledPin, on ? HIGH : LOW);
}


void setup() {
 Serial.begin(115200);
 SPI.begin();
 mfrc522.PCD_Init();


 pinMode(LED_VERDE, OUTPUT);
 pinMode(LED_ROJO, OUTPUT);


 setLedState(LED_VERDE, false);
 setLedState(LED_ROJO, false);


 Serial.println("Conectando a WiFi...");
 WiFi.begin(ssid, password);
 while (WiFi.status() != WL_CONNECTED) {
   delay(500);
   Serial.print(".");
 }
 Serial.println("\n¡Conectado a la red WiFi!");


 // ---INICIAR Y SINCRONIZAR EL TIEMPO ---
 Serial.println("Sincronizando la hora...");
 configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
 struct tm timeinfo;
 if (!getLocalTime(&timeinfo)) {
   Serial.println("Error al obtener la hora del servidor NTP.");
 } else {
   Serial.println("¡Hora sincronizada correctamente!");
 }


 Serial.println("Acerque su llavero RFID...");
}


void loop() {
 if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
   delay(50);
   return;
 }


 String rfidUid = getUidString();
 Serial.print("UID del llavero detectado: ");
 Serial.println(rfidUid);


 HTTPClient http;
 String studentId = "";
 String studentCourse = "";
 // ---Variables para nombre y apellido ---
 String studentName = "";
 String studentSurname = "";


 String url = String(pocketbaseUrl) + "/api/collections/students/records?filter=rfid_uid~'" + rfidUid + "'";
 http.begin(url);
 int httpCode = http.GET();


 if (httpCode == HTTP_CODE_OK) {
   String payload = http.getString();
   JsonDocument doc;
   deserializeJson(doc, payload);


   if (doc["totalItems"].as<int>() > 0) {
     // ---Extraemos todos los datos que necesitamos ---
     studentId = doc["items"][0]["id"].as<String>();
     studentName = doc["items"][0]["name"].as<String>();
     studentSurname = doc["items"][0]["surname"].as<String>();
     studentCourse = doc["items"][0]["curso"].as<String>();
     studentCourse.replace("º", "°");


     // --- Mostramos el nombre y apellido en la consola ---
     Serial.printf("Estudiante encontrado: %s %s, Curso: %s\n", studentName.c_str(), studentSurname.c_str(), studentCourse.c_str());


     String postUrl = String(pocketbaseUrl) + "/api/collections/attendance_management/records";
     http.begin(postUrl);
     http.addHeader("Content-Type", "application/json");


     JsonDocument attendanceDoc;
     // El JSON para PocketBase
     attendanceDoc["student"] = studentId;
     attendanceDoc["state"] = "present";
     attendanceDoc["course"] = studentCourse;


     struct tm timeinfo;
     if (getLocalTime(&timeinfo)) {
       char dateBuffer[11];
       strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeinfo);
       attendanceDoc["date"] = String(dateBuffer);


       char timeBuffer[9];
       strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);
       attendanceDoc["ingreso"] = String(timeBuffer);
     } else {
       Serial.println("No se pudo obtener la hora actual para el registro.");
     }


     String requestBody;
     serializeJson(attendanceDoc, requestBody);
     Serial.println("Enviando JSON: " + requestBody);


     int postCode = http.POST(requestBody);
     if (postCode == 200 || postCode == 204) {
       Serial.println("¡Asistencia registrada con éxito!");
       setLedState(LED_VERDE, true);
       delay(2000);
       setLedState(LED_VERDE, false);
     } else {
       Serial.printf("Error al registrar asistencia. Código: %d\n", postCode);
       setLedState(LED_ROJO, true);
       delay(2000);
       setLedState(LED_ROJO, false);
     }


   } else {
     Serial.println("Error: UID no encontrado en la base de datos.");
     setLedState(LED_ROJO, true);
     delay(2000);
     setLedState(LED_ROJO, false);
   }
 } else {
   Serial.printf("Error en la petición a PocketBase. Código: %d\n", httpCode);
   setLedState(LED_ROJO, true);
   delay(2000);
   setLedState(LED_ROJO, false);
 }


 http.end();
 mfrc522.PICC_HaltA();
 mfrc522.PCD_StopCrypto1();
 delay(3000);
}
