/*
 * Lixeira Inteligente — firmware do dispositivo (ESP32 / Wokwi)
 * Projeto de Objetos Inteligentes Conectados — Universidade Presbiteriana Mackenzie
 *
 * Um único modelo de dispositivo, replicado para cada lixeira.
 * Para criar a lixeira 2 e a 3, copie este projeto e altere apenas BIN_ID.
 *
 * Sensores : PIR (presença)  +  HC-SR04 (nível de resíduos)
 * Atuador  : servomotor SG90 (tampa), comandado via MQTT pelo Node-RED
 *
 * Bibliotecas (libraries.txt): PubSubClient, ESP32Servo, ArduinoJson (v7)
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ======================= CONFIGURAÇÃO =======================
const char* BIN_ID      = "lixeira01";          // lixeira02, lixeira03...
const char* WIFI_SSID   = "Wokwi-GUEST";        // rede Wi-Fi padrão do Wokwi
const char* WIFI_PASS   = "";
const char* MQTT_HOST   = "broker.hivemq.com";  // broker público
const uint16_t MQTT_PORT = 1883;
const char* TOPIC_BASE  = "upm/ioc/lixeira";    // prefixo exclusivo do grupo

// Pinos
const int PIN_PIR   = 27;
const int PIN_TRIG  = 5;
const int PIN_ECHO  = 18;
const int PIN_SERVO = 13;

// Geometria da lixeira (cm) — usada para converter distância em % de ocupação
const float BIN_DEPTH_CM    = 50.0f;  // distância sensor -> fundo (lixeira vazia)
const float SENSOR_FULL_CM  = 5.0f;   // distância mínima confiável (lixeira 100% cheia)

// Temporizações (ms)
const unsigned long TELEMETRY_MS    = 5000;   // período de envio do nível
const unsigned long LID_OPEN_MS     = 5000;   // tempo com a tampa aberta
const unsigned long PRESENCE_GAP_MS = 3000;   // intervalo mínimo entre eventos de presença
const unsigned long MQTT_RETRY_MS   = 5000;

// Servo
const int LID_CLOSED_DEG = 0;
const int LID_OPEN_DEG   = 90;
// ============================================================

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
Servo        lidServo;

String tTelemetria, tPresenca, tComando, tStatus;

bool          lidOpen        = false;
unsigned long lidCloseAt     = 0;
unsigned long lastTelemetry  = 0;
unsigned long lastPresence   = 0;
unsigned long lastMqttTry    = 0;
bool          lastPirState   = false;

// ---------- Sensores ----------
float readDistanceOnceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long dur = pulseIn(PIN_ECHO, HIGH, 30000UL);  // timeout 30 ms
  if (dur == 0) return NAN;
  return (dur * 0.0343f) / 2.0f;                         // velocidade do som ~343 m/s
}

// Mediana de 5 leituras para reduzir ruído; NAN se nenhuma leitura for válida
float readDistanceCm() {
  float v[5];
  int n = 0;
  for (int i = 0; i < 5; i++) {
    float d = readDistanceOnceCm();
    if (!isnan(d)) v[n++] = d;
    delay(30);
  }
  if (n == 0) return NAN;
  for (int i = 1; i < n; i++) {            // ordenação por inserção
    float k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  return v[n / 2];
}

float distanceToLevelPct(float d) {
  float pct = (BIN_DEPTH_CM - d) / (BIN_DEPTH_CM - SENSOR_FULL_CM) * 100.0f;
  if (pct < 0.0f)   pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

// ---------- Atuador ----------
void openLid() {
  lidServo.write(LID_OPEN_DEG);
  lidOpen = true;
  lidCloseAt = millis() + LID_OPEN_MS;
  Serial.println("[tampa] aberta");
}

void closeLid() {
  lidServo.write(LID_CLOSED_DEG);
  lidOpen = false;
  Serial.println("[tampa] fechada");
}

// ---------- MQTT ----------
void onMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toLowerCase();
  Serial.printf("[mqtt] %s -> %s\n", topic, msg.c_str());
  if (String(topic) == tComando) {
    if (msg == "abrir")  openLid();
    if (msg == "fechar") closeLid();
  }
}

bool connectMqtt() {
  String clientId = String("esp32-") + BIN_ID + "-" + String((uint32_t)esp_random(), HEX);
  // Last Will: se o dispositivo cair, o broker publica "offline" (retido)
  if (mqtt.connect(clientId.c_str(), nullptr, nullptr, tStatus.c_str(), 1, true, "offline")) {
    mqtt.publish(tStatus.c_str(), "online", true);
    mqtt.subscribe(tComando.c_str());
    Serial.println("[mqtt] conectado");
    return true;
  }
  Serial.printf("[mqtt] falha rc=%d\n", mqtt.state());
  return false;
}

void publishTelemetry() {
  float d = readDistanceCm();
  JsonDocument doc;
  doc["id"] = BIN_ID;
  if (isnan(d)) {
    doc["distancia_cm"] = nullptr;
    doc["nivel_pct"]    = nullptr;
    doc["sensor_ok"]    = false;
  } else {
    doc["distancia_cm"] = round(d * 10.0f) / 10.0f;
    doc["nivel_pct"]    = round(distanceToLevelPct(d) * 10.0f) / 10.0f;
    doc["sensor_ok"]    = true;
  }
  doc["tampa"]    = lidOpen ? "aberta" : "fechada";
  doc["rssi"]     = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(tTelemetria.c_str(), (const uint8_t*)buf, n, false);
  Serial.printf("[telemetria] %s\n", buf);
}

void publishPresence() {
  JsonDocument doc;
  doc["id"] = BIN_ID;
  doc["presenca"] = true;
  char buf[96];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(tPresenca.c_str(), (const uint8_t*)buf, n, false);
  Serial.println("[presenca] evento enviado");
}

// ---------- Setup / Loop ----------
void connectWifi() {
  Serial.printf("[wifi] conectando a %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // canal 6 acelera a conexão no Wokwi
  while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
  Serial.printf("\n[wifi] conectado, IP %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  lidServo.setPeriodHertz(50);
  lidServo.attach(PIN_SERVO, 500, 2400);
  closeLid();

  String base = String(TOPIC_BASE) + "/" + BIN_ID;
  tTelemetria = base + "/telemetria";
  tPresenca   = base + "/presenca";
  tComando    = base + "/cmd/tampa";
  tStatus     = base + "/status";

  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setCallback(onMessage);
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) connectWifi();

  if (!mqtt.connected()) {
    if (now - lastMqttTry >= MQTT_RETRY_MS) {
      lastMqttTry = now;
      connectMqtt();
    }
  } else {
    mqtt.loop();

    // Presença: detecta borda de subida do PIR e envia evento ao Node-RED
    bool pir = digitalRead(PIN_PIR) == HIGH;
    if (pir && !lastPirState && (now - lastPresence >= PRESENCE_GAP_MS)) {
      lastPresence = now;
      publishPresence();
    }
    lastPirState = pir;

    // Telemetria periódica do nível
    if (now - lastTelemetry >= TELEMETRY_MS) {
      lastTelemetry = now;
      publishTelemetry();
    }
  }

  // Fecha a tampa após o tempo configurado
  if (lidOpen && (long)(now - lidCloseAt) >= 0) closeLid();
}
