#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// ==========================================================
// ==== THAY ĐỔI CÁC THÔNG SỐ NÀY CHO PHÙ HỢP ==============
// ==========================================================

// --- WiFi của bạn ---
const char* ssid = "Lng";
const char* password = "17122004";

// --- Các chân GPIO cho thiết bị ---
const int LED_PIN_1 = 2; // Đèn 1
const int LED_PIN_2 = 12; // Đèn 2
const int FAN_PIN = 14;   // Quạt
const int ML_PIN = 27;    // Máy lạnh (ví dụ)

// --- Cảm biến giả lập ---
// Không cần chân GPIO cho cảm biến giả lập

// ==========================================================
// ==== PHẦN CODE LOGIC (KHÔNG CẦN SỬA) ====================
// ==========================================================

// --- UDP Config ---
WiFiUDP udp;
const int localPort = 5005; // Cổng ESP32 lắng nghe
IPAddress appIP;            // Biến lưu IP của app
int appPort = 0;            // Biến lưu port của app

// --- Biến cho cảm biến giả lập ---
float simulatedTemp = 25.0;  // Nhiệt độ ban đầu
float simulatedHumid = 60.0; // Độ ẩm ban đầu
bool tempRising = true;      // Hướng thay đổi nhiệt độ
bool humidRising = true;     // Hướng thay đổi độ ẩm

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  
  // Cài đặt chân cho các thiết bị
  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(ML_PIN, OUTPUT);

  // Tắt hết thiết bị lúc khởi động
  digitalWrite(LED_PIN_1, LOW);
  digitalWrite(LED_PIN_2, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(ML_PIN, LOW);

  // Kết nối WiFi
  Serial.print("Connecting to WiFi...");
  delay(1000);
  WiFi.begin(ssid,password);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.println("WiFi SSID: " + String(WiFi.SSID()));
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi!");
  }

  // Bắt đầu lắng nghe UDP
  Serial.printf("UDP listening on port %d\n", localPort);
  Serial.println("Waiting for a command from the app to get its IP...");
  udp.begin(localPort);

  // Khởi tạo giá trị cảm biến giả lập
  Serial.println("Simulated sensors initialized");
}

// Hàm giả lập đọc nhiệt độ
float readSimulatedTemperature() {
  // Thay đổi nhiệt độ trong khoảng 20-35 độ C
  if (tempRising) {
    simulatedTemp += 0.2;
    if (simulatedTemp >= 35.0) tempRising = false;
  } else {
    simulatedTemp -= 0.2;
    if (simulatedTemp <= 25.0) tempRising = true;
  }
  return simulatedTemp;
}

// Hàm giả lập đọc độ ẩm
float readSimulatedHumidity() {
  // Thay đổi độ ẩm trong khoảng 40-80%
  if (humidRising) {
    simulatedHumid += 0.5;
    if (simulatedHumid >= 80.0) humidRising = false;
  } else {
    simulatedHumid -= 0.5;
    if (simulatedHumid <= 40.0) humidRising = true;
  }
  return simulatedHumid;
}

// Hàm điều khiển thiết bị
void controlDevice(const String& deviceId, bool isOn) {
    int pin = -1;
    if (deviceId == "led1") pin = LED_PIN_1;
    else if (deviceId == "led2") pin = LED_PIN_2;
    else if (deviceId == "fan") pin = FAN_PIN;
    else if (deviceId == "ml") pin = ML_PIN;
    
    if (pin != -1) {
        digitalWrite(pin, isOn ? HIGH : LOW);
        Serial.printf("Device '%s' turned %s\n", deviceId.c_str(), isOn ? "ON" : "OFF");
    }
}

// Gửi dữ liệu cảm biến về App
void sendSensorData(const String& sensorId, const String& type, float value) {
  if (appPort == 0) return; // Chưa biết địa chỉ app, không gửi

  JsonDocument doc;
  doc["type"] = "sensor_data";
  doc["sensor_id"] = sensorId;
  doc["value"] = value;

  char buffer[128];
  size_t n = serializeJson(doc, buffer);
  
  udp.beginPacket(appIP, appPort);
  udp.write((const uint8_t*)buffer, n);
  udp.endPacket();

  Serial.printf("Sent to App: %s\n", buffer);
}


void loop() {
  // 1. Lắng nghe lệnh từ App
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // Lưu lại địa chỉ IP và Port của App
    appIP = udp.remoteIP();
    appPort = udp.remotePort();
    
    char buffer[256];
    int len = udp.read(buffer, 255);
    if (len > 0) {
      buffer[len] = 0;
      Serial.printf("UDP from %s:%d -> %s\n", appIP.toString().c_str(), appPort, buffer);

      JsonDocument doc;
      if (deserializeJson(doc, buffer) == DeserializationError::Ok) {
        String cmd = doc["cmd"] | "";

        // Lệnh PING để ESP lấy địa chỉ IP của App
        if (cmd == "ping") {
          udp.beginPacket(appIP, appPort);
          udp.write((const uint8_t*)"{\"response\":\"pong\"}", 19);
          udp.endPacket();
        }
        // Lệnh điều khiển thiết bị
        else if (cmd == "control_device") {
          String deviceId = doc["device_id"];
          bool isOn = doc["is_on"];
          controlDevice(deviceId, isOn);
        }
      }
    }
  }

  // 2. Đọc và gửi dữ liệu cảm biến mỗi 5 giây
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 5000) {
    lastRead = millis();
    
    // Đọc giá trị từ cảm biến giả lập
    float h = readSimulatedHumidity();
    float t = readSimulatedTemperature();

    // Gửi dữ liệu độ ẩm
    sendSensorData("dht_humid", "humid", h);
    
    // Gửi dữ liệu nhiệt độ
    sendSensorData("dht_temp", "temp", t);
    
    // In giá trị ra Serial để debug
    Serial.printf("Simulated - Temperature: %.1f°C, Humidity: %.1f%%\n", t, h);
  }
}