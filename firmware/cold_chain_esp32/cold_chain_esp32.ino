#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#if __has_include("cold_chain_config.h")
#include "cold_chain_config.h"
#else
#error "Copy cold_chain_config.example.h to cold_chain_config.h and configure it before building."
#endif

#define RFID_SS 5
#define RFID_RST 27

#define DHT_PIN 26
#define DHT_TYPE DHT11

MFRC522 rfid(RFID_SS, RFID_RST);
Adafruit_BMP085 bmp;
DHT dht(DHT_PIN, DHT_TYPE);
bool bmpReady = false;

struct SensorData {
  String rfidUID;
  String cardType;
  float dhtTemperature;
  float humidity;
  float bmpTemperature;
  float pressure;
};

SensorData sensorData;

SemaphoreHandle_t dataMutex;

struct TelemetryRecord {
  float dhtTemperature;
  float bmpTemperature;
  float humidity;
  float pressureHpa;
  bool hasDhtTemperature;
  bool hasBmpTemperature;
  bool hasHumidity;
  bool hasPressure;
  char timestamp[25];
};

static const uint8_t OFFLINE_QUEUE_CAPACITY = 8;
TelemetryRecord offlineQueue[OFFLINE_QUEUE_CAPACITY];
uint8_t offlineQueueHead = 0;
uint8_t offlineQueueCount = 0;
uint16_t mqttPacketId = 1;

bool hasText(const char *value) {
  return value != NULL && value[0] != '\0';
}

bool validDeviceId() {
  const size_t length = strlen(COLDCHAIN_DEVICE_ID);
  if (length == 0 || length > 64) return false;
  for (size_t i = 0; i < length; i++) {
    const char character = COLDCHAIN_DEVICE_ID[i];
    if (!((character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') ||
          character == '_' || character == '-' || character == '.')) {
      return false;
    }
  }
  return true;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (!hasText(COLDCHAIN_WIFI_SSID)) {
    Serial.println("Wi-Fi is not configured; telemetry cannot be delivered.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(COLDCHAIN_WIFI_SSID, COLDCHAIN_WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed; telemetry delivery will be retried.");
    return false;
  }
  Serial.print("Wi-Fi connected; device address: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool currentTimestamp(char *output, size_t outputSize) {
  if (outputSize == 0) {
    return false;
  }
  output[0] = '\0';
  const time_t now = time(NULL);
  if (now < 1700000000) {
    return false;
  }
  struct tm utc;
  if (gmtime_r(&now, &utc) == NULL) {
    return false;
  }
  if (strftime(output, outputSize, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
    output[0] = '\0';
    return false;
  }
  return true;
}

bool snapshotTelemetry(const SensorData &data, TelemetryRecord &record) {
  record.hasDhtTemperature = isfinite(data.dhtTemperature) &&
      data.dhtTemperature >= -40.0f && data.dhtTemperature <= 85.0f;
  record.hasBmpTemperature = isfinite(data.bmpTemperature) &&
      data.bmpTemperature >= -40.0f && data.bmpTemperature <= 85.0f;
  record.hasHumidity = isfinite(data.humidity) &&
      data.humidity >= 0.0f && data.humidity <= 100.0f;
  const float pressureHpa = data.pressure / 100.0f;
  record.hasPressure = isfinite(pressureHpa) &&
      pressureHpa >= 300.0f && pressureHpa <= 1100.0f;
  record.dhtTemperature = data.dhtTemperature;
  record.bmpTemperature = data.bmpTemperature;
  record.humidity = data.humidity;
  record.pressureHpa = pressureHpa;

  if (!record.hasDhtTemperature && !record.hasBmpTemperature) {
    return false;
  }
  record.timestamp[0] = '\0';
  currentTimestamp(record.timestamp, sizeof(record.timestamp));
  return true;
}

String jsonNumber(float value, bool present) {
  return present ? String(value, 2) : String("null");
}

String telemetryJson(const TelemetryRecord &record, const char *connectionType) {
  String body;
  body.reserve(320);
  body = "{\"device_id\":\"";
  body += COLDCHAIN_DEVICE_ID;
  body += "\",\"timestamp\":\"";
  body += record.timestamp;
  body += "\",\"connection_type\":\"";
  body += connectionType;
  body += "\",\"telemetry\":{\"temperature_dht\":";
  body += jsonNumber(record.dhtTemperature, record.hasDhtTemperature);
  body += ",\"temperature_bmp\":";
  body += jsonNumber(record.bmpTemperature, record.hasBmpTemperature);
  body += ",\"humidity\":";
  body += jsonNumber(record.humidity, record.hasHumidity);
  body += ",\"pressure\":";
  body += jsonNumber(record.pressureHpa, record.hasPressure);
  body += "}}";
  return body;
}

void skipJsonWhitespace(const String &json, size_t &position) {
  while (position < json.length() &&
         (json[position] == ' ' || json[position] == '\n' ||
          json[position] == '\r' || json[position] == '\t')) {
    position++;
  }
}

bool parseJsonString(const String &json, size_t &position, String &value) {
  if (position >= json.length() || json[position] != '"') {
    return false;
  }
  position++;
  value = "";
  while (position < json.length()) {
    const char current = json[position++];
    if (current == '"') {
      return true;
    }
    if (static_cast<uint8_t>(current) < 0x20) {
      return false;
    }
    if (current != '\\') {
      value += current;
      continue;
    }
    if (position >= json.length()) {
      return false;
    }
    const char escaped = json[position++];
    if (escaped == '"' || escaped == '\\' || escaped == '/') {
      value += escaped;
    } else if (escaped == 'b') {
      value += '\b';
    } else if (escaped == 'f') {
      value += '\f';
    } else if (escaped == 'n') {
      value += '\n';
    } else if (escaped == 'r') {
      value += '\r';
    } else if (escaped == 't') {
      value += '\t';
    } else {
      return false;
    }
  }
  return false;
}

bool validJsonNumber(const String &token) {
  if (token.length() == 0) {
    return false;
  }
  size_t index = 0;
  if (token[index] == '-') index++;
  if (index >= token.length()) return false;
  if (token[index] == '0') {
    index++;
    if (index < token.length() && isDigit(token[index])) return false;
  } else {
    if (!isDigit(token[index]) || token[index] == '0') return false;
    while (index < token.length() && isDigit(token[index])) index++;
  }
  if (index < token.length() && token[index] == '.') {
    index++;
    const size_t fractionStart = index;
    while (index < token.length() && isDigit(token[index])) index++;
    if (index == fractionStart) return false;
  }
  if (index < token.length() && (token[index] == 'e' || token[index] == 'E')) {
    index++;
    if (index < token.length() && (token[index] == '+' || token[index] == '-')) index++;
    const size_t exponentStart = index;
    while (index < token.length() && isDigit(token[index])) index++;
    if (index == exponentStart) return false;
  }
  if (index != token.length()) return false;
  char *end = NULL;
  const double value = strtod(token.c_str(), &end);
  return end != token.c_str() && *end == '\0' && isfinite(value);
}

bool validateTelemetryResponse(const String &json) {
  size_t position = 0;
  skipJsonWhitespace(json, position);
  if (position >= json.length() || json[position++] != '{') {
    return false;
  }

  bool hasId = false;
  bool hasDeviceId = false;
  bool hasTemperature = false;
  bool hasHumidity = false;
  bool hasPressure = false;
  bool hasBattery = false;
  bool hasConnectionType = false;
  bool hasIsAlert = false;
  bool hasRecordedAt = false;
  bool idValid = false;
  bool deviceValid = false;
  bool temperatureValid = false;
  bool humidityValid = false;
  bool pressureValid = false;
  bool batteryValid = false;
  bool connectionValid = false;
  bool alertValid = false;
  bool recordedAtValid = false;

  skipJsonWhitespace(json, position);
  while (position < json.length() && json[position] != '}') {
    String key;
    if (!parseJsonString(json, position, key)) {
      return false;
    }
    skipJsonWhitespace(json, position);
    if (position >= json.length() || json[position++] != ':') {
      return false;
    }
    skipJsonWhitespace(json, position);

    String stringValue;
    String primitive;
    const bool isString = position < json.length() && json[position] == '"';
    if (isString) {
      if (!parseJsonString(json, position, stringValue)) {
        return false;
      }
    } else {
      const size_t valueStart = position;
      while (position < json.length() && json[position] != ',' &&
             json[position] != '}' && json[position] != ' ' &&
             json[position] != '\n' && json[position] != '\r' &&
             json[position] != '\t') {
        position++;
      }
      if (position == valueStart) {
        return false;
      }
      primitive = json.substring(valueStart, position);
    }

    if (key == "id") {
      if (hasId || isString || !validJsonNumber(primitive) ||
          primitive.indexOf('.') >= 0 || primitive.indexOf('e') >= 0 ||
          primitive.indexOf('E') >= 0 || primitive.toFloat() < 1) return false;
      hasId = idValid = true;
    } else if (key == "device_id") {
      if (hasDeviceId || !isString || stringValue != COLDCHAIN_DEVICE_ID) return false;
      hasDeviceId = deviceValid = true;
    } else if (key == "temperature") {
      if (hasTemperature || (isString) ||
          (primitive != "null" && !validJsonNumber(primitive))) return false;
      hasTemperature = temperatureValid = true;
    } else if (key == "humidity") {
      if (hasHumidity || isString ||
          (primitive != "null" && !validJsonNumber(primitive))) return false;
      hasHumidity = humidityValid = true;
    } else if (key == "pressure") {
      if (hasPressure || isString ||
          (primitive != "null" && !validJsonNumber(primitive))) return false;
      hasPressure = pressureValid = true;
    } else if (key == "battery_level") {
      if (hasBattery || isString ||
          (primitive != "null" && !validJsonNumber(primitive))) return false;
      hasBattery = batteryValid = true;
    } else if (key == "connection_type") {
      if (hasConnectionType || !isString || stringValue != "HTTPS") return false;
      hasConnectionType = connectionValid = true;
    } else if (key == "is_alert") {
      if (hasIsAlert || isString || (primitive != "true" && primitive != "false")) return false;
      hasIsAlert = alertValid = true;
    } else if (key == "recorded_at") {
      if (hasRecordedAt || !isString || stringValue.length() == 0) return false;
      hasRecordedAt = recordedAtValid = true;
    } else {
      return false;
    }

    skipJsonWhitespace(json, position);
    if (position < json.length() && json[position] == ',') {
      position++;
      skipJsonWhitespace(json, position);
      if (position >= json.length() || json[position] == '}') return false;
    } else if (position >= json.length() || json[position] != '}') {
      return false;
    }
  }
  if (position >= json.length() || json[position++] != '}') {
    return false;
  }
  skipJsonWhitespace(json, position);
  return position == json.length() && idValid && deviceValid && temperatureValid &&
      humidityValid && pressureValid && batteryValid && connectionValid &&
      alertValid && recordedAtValid;
}

void appendMqttString(std::vector<uint8_t> &packet, const char *value) {
  const size_t length = strlen(value);
  packet.push_back(static_cast<uint8_t>(length >> 8));
  packet.push_back(static_cast<uint8_t>(length));
  packet.insert(packet.end(), value, value + length);
}

bool writeMqttPacket(WiFiClientSecure &client, uint8_t header,
                     const std::vector<uint8_t> &body) {
  uint8_t encodedLength[4];
  size_t encodedCount = 0;
  size_t remaining = body.size();
  do {
    uint8_t byte = remaining % 128;
    remaining /= 128;
    if (remaining > 0) byte |= 0x80;
    encodedLength[encodedCount++] = byte;
  } while (remaining > 0 && encodedCount < sizeof(encodedLength));
  if (remaining > 0 || client.write(&header, 1) != 1 ||
      client.write(encodedLength, encodedCount) != encodedCount) {
    return false;
  }
  return body.empty() || client.write(body.data(), body.size()) == body.size();
}

bool readMqttByte(WiFiClientSecure &client, uint8_t &value, uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    if (client.available()) {
      const int incoming = client.read();
      if (incoming >= 0) {
        value = static_cast<uint8_t>(incoming);
        return true;
      }
    }
    if (!client.connected() && !client.available()) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

bool readMqttPacket(WiFiClientSecure &client, uint8_t &header,
                    std::vector<uint8_t> &body, uint32_t timeoutMs) {
  if (!readMqttByte(client, header, timeoutMs)) return false;
  size_t remaining = 0;
  size_t multiplier = 1;
  uint8_t byte = 0;
  uint8_t count = 0;
  do {
    if (count++ >= 4 || !readMqttByte(client, byte, timeoutMs)) return false;
    remaining += (byte & 0x7f) * multiplier;
    multiplier *= 128;
  } while ((byte & 0x80) != 0);
  if (remaining > 1024) return false;
  body.clear();
  body.reserve(remaining);
  for (size_t i = 0; i < remaining; i++) {
    if (!readMqttByte(client, byte, timeoutMs)) return false;
    body.push_back(byte);
  }
  return true;
}

bool publishMqttQos1(const TelemetryRecord &record) {
  if (!validDeviceId() || !connectWiFi() || !hasText(COLDCHAIN_MQTT_HOST) ||
      !hasText(COLDCHAIN_MQTT_CA_CERT) ||
      (hasText(COLDCHAIN_MQTT_PASSWORD) && !hasText(COLDCHAIN_MQTT_USERNAME))) {
    Serial.println("MQTT TLS is not configured or Wi-Fi is unavailable.");
    return false;
  }

  WiFiClientSecure client;
  client.setCACert(COLDCHAIN_MQTT_CA_CERT);
  client.setTimeout(6000);
  if (!client.connect(COLDCHAIN_MQTT_HOST, COLDCHAIN_MQTT_PORT)) {
    Serial.println("MQTT TLS/broker connection failed.");
    client.stop();
    return false;
  }

  std::vector<uint8_t> connectBody;
  appendMqttString(connectBody, "MQTT");
  connectBody.push_back(4);
  uint8_t connectFlags = 0x02;
  if (hasText(COLDCHAIN_MQTT_USERNAME)) connectFlags |= 0x80;
  if (hasText(COLDCHAIN_MQTT_PASSWORD)) connectFlags |= 0x40;
  connectBody.push_back(connectFlags);
  connectBody.push_back(0);
  connectBody.push_back(30);
  appendMqttString(connectBody, COLDCHAIN_DEVICE_ID);
  if (hasText(COLDCHAIN_MQTT_USERNAME)) {
    appendMqttString(connectBody, COLDCHAIN_MQTT_USERNAME);
  }
  if (hasText(COLDCHAIN_MQTT_PASSWORD)) {
    appendMqttString(connectBody, COLDCHAIN_MQTT_PASSWORD);
  }
  if (!writeMqttPacket(client, 0x10, connectBody)) {
    Serial.println("Could not send MQTT CONNECT packet.");
    client.stop();
    return false;
  }

  uint8_t responseHeader;
  std::vector<uint8_t> responseBody;
  if (!readMqttPacket(client, responseHeader, responseBody, 6000) ||
      responseHeader != 0x20 || responseBody.size() != 2 ||
      responseBody[0] != 0 || responseBody[1] != 0) {
    Serial.println("MQTT broker did not accept the TLS session.");
    client.stop();
    return false;
  }

  String topic = "coldchain/";
  topic += COLDCHAIN_DEVICE_ID;
  topic += "/telemetry";
  const String payload = telemetryJson(record, "MQTT");
  std::vector<uint8_t> publishBody;
  appendMqttString(publishBody, topic.c_str());
  const uint16_t packetId = mqttPacketId;
  mqttPacketId = mqttPacketId == 0xFFFF ? 1 : mqttPacketId + 1;
  publishBody.push_back(static_cast<uint8_t>(packetId >> 8));
  publishBody.push_back(static_cast<uint8_t>(packetId));
  publishBody.insert(publishBody.end(), payload.c_str(),
                     payload.c_str() + payload.length());

  if (!writeMqttPacket(client, 0x32, publishBody)) {
    Serial.println("Could not send MQTT QoS 1 PUBLISH packet.");
    client.stop();
    return false;
  }
  const bool acknowledged =
      readMqttPacket(client, responseHeader, responseBody, 6000) &&
      responseHeader == 0x40 && responseBody.size() == 2 &&
      responseBody[0] == static_cast<uint8_t>(packetId >> 8) &&
      responseBody[1] == static_cast<uint8_t>(packetId);
  client.stop();
  if (!acknowledged) {
    Serial.println("MQTT QoS 1 PUBACK was missing or invalid.");
    return false;
  }
  return true;
}

bool publishHttpsFallback(const TelemetryRecord &record) {
  if (!validDeviceId() || !connectWiFi() || !hasText(COLDCHAIN_HTTPS_HOST) ||
      !hasText(COLDCHAIN_HTTPS_CA_CERT) || !hasText(COLDCHAIN_API_KEY)) {
    Serial.println("HTTPS fallback is not configured or Wi-Fi is unavailable.");
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setCACert(COLDCHAIN_HTTPS_CA_CERT);
  secureClient.setTimeout(10000);
  HTTPClient http;
  http.setTimeout(10000);
  String url = "https://";
  url += COLDCHAIN_HTTPS_HOST;
  url += ":";
  url += String(COLDCHAIN_HTTPS_PORT);
  url += "/api/v1/telemetry/backup";
  if (!http.begin(secureClient, url)) {
    Serial.println("Could not initialize verified HTTPS connection.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", COLDCHAIN_API_KEY);
  const String payload = telemetryJson(record, "HTTPS");
  const int statusCode = http.POST(payload);
  bool delivered = false;
  if (statusCode != 201) {
    Serial.printf("HTTPS fallback failed; HTTP status=%d\n", statusCode);
  } else {
    const String response = http.getString();
    delivered = validateTelemetryResponse(response);
    if (!delivered) {
      Serial.println("HTTPS returned 201 but its telemetry response was malformed or mismatched.");
    }
  }
  http.end();
  return delivered;
}

bool deliverTelemetry(const TelemetryRecord &record) {
  if (record.timestamp[0] == '\0') {
    Serial.println("Telemetry has no valid UTC timestamp; delivery was not attempted.");
    return false;
  }
  if (publishMqttQos1(record)) {
    Serial.println("MQTT broker acknowledged telemetry; backend storage is not confirmed.");
    return true;
  }
  Serial.println("MQTT delivery failed; attempting verified HTTPS fallback.");
  if (publishHttpsFallback(record)) {
    Serial.println("Telemetry delivered via HTTPS fallback (201 response validated).");
    return true;
  }
  Serial.println("MQTT and HTTPS delivery failed.");
  return false;
}

bool enqueueTelemetry(const TelemetryRecord &record) {
  if (offlineQueueCount >= OFFLINE_QUEUE_CAPACITY) {
    Serial.println("OFFLINE QUEUE FULL: this new telemetry record could not be buffered.");
    return false;
  }
  const uint8_t tail = (offlineQueueHead + offlineQueueCount) % OFFLINE_QUEUE_CAPACITY;
  offlineQueue[tail] = record;
  offlineQueueCount++;
  Serial.printf("Buffered telemetry in RAM queue (%u/%u).\n",
                static_cast<unsigned>(offlineQueueCount),
                static_cast<unsigned>(OFFLINE_QUEUE_CAPACITY));
  return true;
}

void retryQueuedTelemetry() {
  if (offlineQueueCount == 0) return;
  TelemetryRecord &record = offlineQueue[offlineQueueHead];
  if (record.timestamp[0] == '\0' &&
      !currentTimestamp(record.timestamp, sizeof(record.timestamp))) {
    return;
  }
  if (!deliverTelemetry(record)) return;
  offlineQueueHead = (offlineQueueHead + 1) % OFFLINE_QUEUE_CAPACITY;
  offlineQueueCount--;
  Serial.printf("Confirmed queued delivery; %u record(s) remain.\n",
                static_cast<unsigned>(offlineQueueCount));
}

void setupRFID() {
  SPI.begin(18, 19, 23);
  rfid.PCD_Init();

  Serial.println("RFID Ready");
}

void setupBMP() {
  Wire.begin(21, 22);

  bmpReady = bmp.begin();
  if (bmpReady) {
    Serial.println("BMP180 Ready");
  } else {
    Serial.println("BMP180 NOT FOUND!");
  }
}

void setupDHT() {
  dht.begin();

  Serial.println("DHT11 Ready");
}

String getRFIDUID() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);

    if (i < rfid.uid.size - 1) {
      uid += ":";
    }
  }

  uid.toUpperCase();

  return uid;
}

void readRFID() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getRFIDUID();

  MFRC522::PICC_Type type;
  type = rfid.PICC_GetType(rfid.uid.sak);

  String cardType = rfid.PICC_GetTypeName(type);

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    sensorData.rfidUID = uid;
    sensorData.cardType = cardType;

    xSemaphoreGive(dataMutex);
  }

  Serial.println();
  Serial.println("RFID");
  Serial.print("UID: ");
  Serial.println(uid);
  Serial.print("Card Type: ");
  Serial.println(cardType);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void readDHT() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT11 reading failed!");
    return;
  }

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    sensorData.dhtTemperature = temperature;
    sensorData.humidity = humidity;

    xSemaphoreGive(dataMutex);
  }

  Serial.println();
  Serial.println("DHT11");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
}

void readBMP() {
  if (!bmpReady) {
    return;
  }
  float temperature = bmp.readTemperature();
  float pressure = bmp.readPressure();

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    sensorData.bmpTemperature = temperature;
    sensorData.pressure = pressure;

    xSemaphoreGive(dataMutex);
  }

  Serial.println();
  Serial.println("BMP180");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Pressure: ");
  Serial.print(pressure);
  Serial.println(" Pa");

  Serial.print("Pressure: ");
  Serial.print(pressure / 100.0);
  Serial.println(" hPa");
}

void readSensors() {
  readRFID();
  readDHT();
  readBMP();
}

void sensorTask(void *parameter) {
  while (true) {
    readSensors();

    Serial.println("----------------------------");

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void networkTask(void *parameter) {
  WiFi.mode(WIFI_STA);
  bool ntpConfigured = false;
  bool sensorReadyReported = false;
  bool timeReadyReported = false;
  while (true) {
    const bool wifiConnected = connectWiFi();
    if (wifiConnected && !ntpConfigured) {
      configTime(0, 0, COLDCHAIN_NTP_SERVER);
      ntpConfigured = true;
    }

    SensorData data;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      data = sensorData;

      xSemaphoreGive(dataMutex);
    }

    TelemetryRecord record;
    if (!snapshotTelemetry(data, record)) {
      if (!sensorReadyReported) {
        Serial.println("Waiting for at least one valid temperature reading.");
        sensorReadyReported = true;
      }
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    sensorReadyReported = false;

    bool timestampReady = record.timestamp[0] != '\0';
    if (!timestampReady) {
      timestampReady = currentTimestamp(record.timestamp, sizeof(record.timestamp));
    }
    if (!timestampReady && !timeReadyReported) {
      Serial.println("NTP time is unavailable; buffering readings until a valid UTC timestamp is available.");
      timeReadyReported = true;
    } else if (timestampReady) {
      timeReadyReported = false;
    }

    if (!wifiConnected) {
      enqueueTelemetry(record);
    } else if (offlineQueueCount > 0) {
      retryQueuedTelemetry();
      if (offlineQueueCount > 0) {
        enqueueTelemetry(record);
      } else if (!timestampReady || !deliverTelemetry(record)) {
        enqueueTelemetry(record);
      }
    } else if (!timestampReady || !deliverTelemetry(record)) {
      enqueueTelemetry(record);
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  sensorData.dhtTemperature = NAN;
  sensorData.humidity = NAN;
  sensorData.bmpTemperature = NAN;
  sensorData.pressure = NAN;
  dataMutex = xSemaphoreCreateMutex();

  setupRFID();
  setupBMP();
  setupDHT();

  Serial.println();
  Serial.println("System Ready");

  xTaskCreatePinnedToCore(
    sensorTask,
    "Sensor Task",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    networkTask,
    "Network Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
