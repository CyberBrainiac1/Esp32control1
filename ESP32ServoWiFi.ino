#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

namespace ServoRemote {
constexpr char kDefaultStationSSID[] = "YOUR_WIFI_NAME";
constexpr char kDefaultStationPassword[] = "YOUR_WIFI_PASSWORD";

constexpr char kAccessPointSSID[] = "ESP32-Control";
constexpr char kAccessPointPassword[] = "control123";
constexpr char kHostname[] = "esp32-control";
constexpr char kBluetoothName[] = "ESP32-Control-BLE";
constexpr char kBluetoothServiceUuid[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kBluetoothRxUuid[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

constexpr int kServoPin = 18;
constexpr int kInitialAngle = 90;
constexpr int kMinPulseWidthMicros = 500;
constexpr int kMaxPulseWidthMicros = 2400;
constexpr unsigned long kStationConnectTimeoutMs = 12000;
constexpr unsigned long kReconnectIntervalMs = 10000;
}  // namespace ServoRemote

enum class NetworkMode {
  station,
  accessPoint,
};

Servo gimbalServo;
WebServer server(80);

NetworkMode networkMode = NetworkMode::accessPoint;
unsigned long lastReconnectAttemptAt = 0;
int currentAngle = ServoRemote::kInitialAngle;

int clampAngle(int angle) {
  if (angle < 0) {
    return 0;
  }

  if (angle > 180) {
    return 180;
  }

  return angle;
}

void applyAngle(int angle) {
  currentAngle = clampAngle(angle);
  gimbalServo.write(currentAngle);
  Serial.printf("Servo angle set to %d\n", currentAngle);
}

String activeAddress() {
  if (networkMode == NetworkMode::station && WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }

  return WiFi.softAPIP().toString();
}

String networkModeLabel() {
  return networkMode == NetworkMode::station ? "station" : "ap";
}

bool credentialsConfigured() {
  return strcmp(ServoRemote::kDefaultStationSSID, "YOUR_WIFI_NAME") != 0 &&
         strcmp(ServoRemote::kDefaultStationPassword, "YOUR_WIFI_PASSWORD") != 0;
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  sendCorsHeaders();
  server.send(204);
}

int extractAngleFromPayload(const String& payload) {
  String digits;

  for (unsigned int i = 0; i < payload.length(); i += 1) {
    const char c = payload.charAt(i);
    if (isDigit(c) || (c == '-' && digits.length() == 0)) {
      digits += c;
    } else if (digits.length() > 0) {
      break;
    }
  }

  if (digits.isEmpty() || digits == "-") {
    return ServoRemote::kInitialAngle;
  }

  return digits.toInt();
}

void handleRoot() {
  sendCorsHeaders();
  server.send(
      200,
      "application/json",
      "{\"message\":\"ESP32 control is running\",\"status\":\"ok\"}");
}

void handleStatus() {
  sendCorsHeaders();

  const bool wifiConnected = networkMode == NetworkMode::station &&
                             WiFi.status() == WL_CONNECTED;

  String response = "{";
  response += "\"ok\":true,";
  response += "\"angle\":" + String(currentAngle) + ",";
  response += "\"hostname\":\"" + String(ServoRemote::kHostname) + "\",";
  response += "\"bluetoothName\":\"" + String(ServoRemote::kBluetoothName) + "\",";
  response += "\"mode\":\"" + networkModeLabel() + "\",";
  response += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
  response += "\"ip\":\"" + activeAddress() + "\",";
  response += "\"ssid\":\"";
  response += networkMode == NetworkMode::station ? WiFi.SSID() : ServoRemote::kAccessPointSSID;
  response += "\"";
  response += "}";

  server.send(200, "application/json", response);
}

void handleServo() {
  int requestedAngle = ServoRemote::kInitialAngle;

  if (server.hasArg("angle")) {
    requestedAngle = server.arg("angle").toInt();
  } else if (server.hasArg("plain")) {
    requestedAngle = extractAngleFromPayload(server.arg("plain"));
  }

  applyAngle(requestedAngle);
  sendCorsHeaders();

  String response = "{";
  response += "\"ok\":true,";
  response += "\"angle\":" + String(currentAngle);
  response += "}";

  server.send(200, "application/json", response);
}

void handleNotFound() {
  sendCorsHeaders();
  server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
}

bool beginMDNS() {
  if (!MDNS.begin(ServoRemote::kHostname)) {
    Serial.println("mDNS failed to start");
    return false;
  }

  MDNS.addService("http", "tcp", 80);
  Serial.printf("mDNS ready at http://%s.local\n", ServoRemote::kHostname);
  return true;
}

bool connectToStation() {
  if (!credentialsConfigured()) {
    Serial.println("Wi-Fi station credentials are still placeholders");
    return false;
  }

  Serial.printf("Connecting to Wi-Fi SSID \"%s\"\n", ServoRemote::kDefaultStationSSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ServoRemote::kDefaultStationSSID, ServoRemote::kDefaultStationPassword);

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < ServoRemote::kStationConnectTimeoutMs) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Station connection timed out");
    WiFi.disconnect(true, true);
    return false;
  }

  networkMode = NetworkMode::station;
  Serial.printf("Connected to Wi-Fi. IP: %s\n", WiFi.localIP().toString().c_str());
  beginMDNS();
  return true;
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ServoRemote::kAccessPointSSID, ServoRemote::kAccessPointPassword);
  networkMode = NetworkMode::accessPoint;

  Serial.printf("Started ESP32 access point \"%s\"\n", ServoRemote::kAccessPointSSID);
  Serial.printf("AP password: %s\n", ServoRemote::kAccessPointPassword);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

class ServoBluetoothCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    const String value = characteristic->getValue().c_str();
    if (value.length() == 0) {
      return;
    }

    applyAngle(extractAngleFromPayload(value));
  }
};

void startBluetooth() {
  BLEDevice::init(ServoRemote::kBluetoothName);
  BLEServer* bleServer = BLEDevice::createServer();
  BLEService* bleService = bleServer->createService(ServoRemote::kBluetoothServiceUuid);
  BLECharacteristic* rxCharacteristic = bleService->createCharacteristic(
      ServoRemote::kBluetoothRxUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);

  rxCharacteristic->setCallbacks(new ServoBluetoothCallbacks());
  bleService->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(ServoRemote::kBluetoothServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.printf("Bluetooth ready as %s\n", ServoRemote::kBluetoothName);
}

void configureHttpServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/status", HTTP_OPTIONS, handleOptions);
  server.on("/api/servo", HTTP_POST, handleServo);
  server.on("/api/servo", HTTP_GET, handleServo);
  server.on("/api/servo", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void ensureNetworkReady() {
  if (networkMode == NetworkMode::station && WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (networkMode == NetworkMode::accessPoint) {
    return;
  }

  if (millis() - lastReconnectAttemptAt < ServoRemote::kReconnectIntervalMs) {
    return;
  }

  lastReconnectAttemptAt = millis();
  Serial.println("Wi-Fi disconnected, attempting reconnect");

  if (!connectToStation()) {
    startAccessPoint();
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  gimbalServo.setPeriodHertz(50);
  gimbalServo.attach(
      ServoRemote::kServoPin,
      ServoRemote::kMinPulseWidthMicros,
      ServoRemote::kMaxPulseWidthMicros);

  applyAngle(ServoRemote::kInitialAngle);
  startBluetooth();

  if (!connectToStation()) {
    startAccessPoint();
  }

  configureHttpServer();
  Serial.println("ESP32 Control is ready");
}

void loop() {
  server.handleClient();
  ensureNetworkReady();
  delay(10);
}
