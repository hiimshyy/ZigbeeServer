#include <Arduino.h>
#include "zigbeeServer.h"
#include "PEClient.h"
#include "esp_log.h"
#include <Preferences.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "page.h"
#include <HTTPClient.h>
#include <sstream>
#include <vector>
#include <deque>
#include <string>
#include <queue>  
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define FLASH_NAME_SPACE "piot"
#define SSID_AP "Smart Meter"
#define PASSWORD_AP "12345678"
#define MAX_AP_CONNECTIONS 1

// #define WIFI_SSID "VanPhong2.4g"
// #define WIFI_PASSWORD "Vp@1denchin"

#define MQTT_SERVER "broker.tbedev.cloud"
#define MQTT_PORT 1883

// #define CLIENT_ID "50bf5161-f426-4c26-8ea1-a5ff53461cbb"
// #define USERNAME "demo"
// #define PASSWORD "demoesp"
// #define CLIENT_ID "e03a686f-80d0-4606-a013-3e9bf47b36ae"
// #define USERNAME "tiensy"
// #define PASSWORD "06102003"
#define LED1_PIN 2

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org",3600 * 0, 60000); // Update mỗi 60 giây
WiFiClient wifiClient;
Preferences preferences;
AsyncWebServer server(80);
DNSServer dnsServer;

bool isAPMode = false;

ZigbeeServer zigbeeServer;

void led1Callback(String value);
void sendAttributes();
void onCollectData(const char *id, const char *data);
void getDevice(String value);

void checkSwitchButton(void *pvParameters);
void handleFormSubmit(AsyncWebServerRequest *request);
void reloadPreferences();
String setupResponeHTML();

struct Metric {
    std::string name;
    double value;
    uint64_t ts;
};

struct Attribute {
    std::string name;
    std::string value;
};

struct FlashData
{
  String ssid;
  String password;
  String name;
  String device_id;
  String access_token;
  String client_id;
  String mqtt_username;
  String mqtt_password;
  int poll_interval;
};
FlashData flashData;

PEClient peClient;

std::queue<Metric> metricQueue; // Khai báo queue để lưu trữ các metric
std::vector<Attribute> attributes; // Khai báo vector attributes
SemaphoreHandle_t metricQueueMutex; // Mutex để bảo vệ truy cập vào hàng đợi

class CaptiveRequestHandler : public AsyncWebHandler
{
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request)
  {
    // request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request)
  {
    request->send_P(200, "text/html", setupResponeHTML().c_str());
  }
};

/**
 * @name sendMetricsTask
 * @brief Gửi dữ liệu đo được lên MQTT
 * 
 * @param {void*} pvParameters - Tham số truyền vào
 * 
 * @return None
 */
void sendMetricsTask(void *pvParameters) {
    while (true) {
        if (peClient.connected()) {
            if (xSemaphoreTake(metricQueueMutex, portMAX_DELAY) == pdTRUE) {
                while (!metricQueue.empty()) {
                    Metric metric = metricQueue.front();
                    ESP_LOGI("Main", "Sending metric %s: %f - %llu", metric.name.c_str(), metric.value, metric.ts);
                    peClient.sendMetric(metric.ts, metric.name.c_str(), metric.value);
                    metricQueue.pop();
                }
                xSemaphoreGive(metricQueueMutex);
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Delay 1 giây giữa các lần gửi
    }
}

/**
 * @name setup
 * @brief Hàm khởi tạo
 * 
 * @param None
 * 
 * @return None
 */
void setup()
{
    Serial.begin(115200);
    zigbeeServer.begin();

    xTaskCreatePinnedToCore(
        checkSwitchButton,   /* Function to implement the task */
        "checkSwitchButton", /* Name of the task */
        2048,                /* Stack size in words */
        NULL,                /* Task input parameter */
        0,                   /* Priority of the task */
        NULL,                /* Task handle. */
        0
    );

    reloadPreferences();
    Serial.println("Connecting to WiFi");
    Serial.println(flashData.ssid);
    Serial.println(flashData.password);

    peClient.init(flashData.ssid.c_str(), flashData.password.c_str(), MQTT_SERVER, MQTT_PORT, flashData.client_id.c_str(), flashData.mqtt_username.c_str(), flashData.mqtt_password.c_str());
    peClient.begin();

    peClient.on("led1", led1Callback);
    peClient.on("devices", getDevice);

    while (!peClient.connected())
    {
        ESP_LOGI("Main", "Waiting for MQTT connection...");
        delay(1000);
    }

    pinMode(LED1_PIN, OUTPUT);
    digitalWrite(LED1_PIN, LOW);
    sendAttributes();

    metricQueueMutex = xSemaphoreCreateMutex(); // Tạo mutex

    timeClient.begin(); // Bắt đầu NTP client
    timeClient.update(); // Cập nhật thời gian ngay lập tức

    // Tạo task sendMetricsTask chạy trên Core 1
    xTaskCreatePinnedToCore(
        sendMetricsTask,
        "SendMetricsTask",
        10000,
        NULL,
        1,
        NULL,
        1 // Chạy trên core 1
    );
    
    zigbeeServer.onChange(sendAttributes);
    zigbeeServer.onMessage(onCollectData);
    zigbeeServer.updatePendingList(sendAttributes);
    zigbeeServer.sendCommand("TBE0123456789ZB","led_status:1");
}

/**
 * @name loop
 * @brief Hàm vòng lặp
 * 
 * @param None
 * 
 * @return None
 */
void loop()
{
    timeClient.update(); // Cập nhật thời gian mỗi chu kỳ loop
    ESP_LOGI("Main", "NTP Time: %s", timeClient.getFormattedTime().c_str());
    delay(1000);
}

/**
 * @name stringToBool
 * @brief Chuyển đổi chuỗi thành boolean
 * 
 * @param {const String &} value - Chuỗi cần chuyển đổi
 * 
 * @return {bool} - Giá trị boolean
 */
bool stringToBool(const String &value)
{
    return value.equalsIgnoreCase("true") || value == "1";
}

/**
 * @name led1Callback
 * @brief Callback khi có dữ liệu đến từ MQTT
 * 
 * @param {String} value - Dữ liệu nhận được
 * 
 * @return None
 */
void led1Callback(String value)
{
    digitalWrite(LED1_PIN, stringToBool(value));
}
void getDevice(String value)
{   
    ESP_LOGI("Get Device","Device ID: %s", value.c_str());
    std::vector<String> deviceIds;
    int start = 0;
    int end = value.indexOf(',');
    
    while (end != -1) {
        String id = value.substring(start, end);
        id.trim();
        ESP_LOGI("Get Device","ID: %s", id.c_str());
        if (id.length() > 0) {
            zigbeeServer.addDevice(id.c_str());
            ESP_LOGI("Get Device","Pushed back ID: %s", id.c_str());
        }
        start = end + 1;
        end = value.indexOf(',', start);
    }
    
    // Xử lý phần tử cuối cùng
    String lastId = value.substring(start);
    lastId.trim();
    if (lastId.length() > 0) {
        zigbeeServer.addDevice(lastId.c_str());
        ESP_LOGI("Get Device","Pushed back ID: %s", lastId.c_str());
    }
}

/**
 * @name sendAttributes
 * @brief Gửi thông số lên MQTT
 * 
 * @param None
 * 
 * @return None
 */
void sendAttributes()
{
    attributes.clear();
    Attribute attr;
    attr.name = "localIP";
    attr.value = WiFi.localIP().toString().c_str();
    attributes.push_back(attr);
    attr.name = "pendingDevices";
    String deviceIds = "";
    for (size_t i = 0; i < zigbeeServer.pendingDeviceList.size(); ++i)
    {
        deviceIds += zigbeeServer.pendingDeviceList[i].id.c_str();
        if (i < zigbeeServer.pendingDeviceList.size() - 1)
        {
            deviceIds += ","; // Thêm dấu phẩy giữa các ID, trừ ID cuối cùng
        }
    }
    attr.value = deviceIds.c_str();
    attributes.push_back(attr);
    for (Attribute attr : attributes)
    {
        peClient.sendAttribute(attr.name.c_str(), attr.value.c_str());
        Serial.println("Devices has sent!");
    }
}

/**
 * @name onCollectData
 * @brief Hàm thu thập dữ liệu từ thiết bị
 * 
 * @param {const char*} id - ID của thiết bị
 * @param {const char*} data - Dữ liệu từ thiết bị
 * 
 * @return None
 */
void onCollectData(const char *id, const char *data)
{
    ESP_LOGI("Main", "Collect data from device %s: %s", id, data);
    std::istringstream dataStream(data);
    std::string item;
    bool hasComma = strchr(data, ',') != nullptr; // Kiểm tra xem chuỗi có chứa dấu phẩy không

    if (hasComma) {
        while (std::getline(dataStream, item, ',')) {
            std::istringstream itemStream(item);
            std::string key;
            std::string valueStr;
            if (std::getline(itemStream, key, ':') && std::getline(itemStream, valueStr)) {
                std::string metricName = key + "_" + id;
                double value = std::stod(valueStr);
                uint64_t timestamp = timeClient.getEpochTime(); // Lấy thời gian từ NTP client
                timestamp *= 1000; // Chuyển đổi sang milliseconds
                ESP_LOGI("Main", "Collected metric %s: %f - %llu", metricName.c_str(), value, timestamp);
                Metric metric = {metricName, value, timestamp};

                // Thêm metric vào hàng đợi
                if (xSemaphoreTake(metricQueueMutex, portMAX_DELAY) == pdTRUE) {
                    metricQueue.push(metric);
                    xSemaphoreGive(metricQueueMutex);
                }
            }
        }
    } else {
        std::istringstream itemStream(data);
        std::string key;
        std::string valueStr;
        if (std::getline(itemStream, key, ':') && std::getline(itemStream, valueStr)) {
            std::string metricName = key + "_" + id;
            double value = std::stod(valueStr);
            uint64_t timestamp = timeClient.getEpochTime(); // Lấy thời gian từ NTP client
            timestamp *= 1000; // Chuyển đổi sang milliseconds
            ESP_LOGI("Main", "Collected metric %s: %f - %lld", metricName.c_str(), value, timestamp);
            Metric metric = {metricName, value, timestamp};

            // Thêm metric vào hàng đợi
            if (xSemaphoreTake(metricQueueMutex, portMAX_DELAY) == pdTRUE) {
                metricQueue.push(metric);
                xSemaphoreGive(metricQueueMutex);
            }
        }
    }

    if (metricQueue.size() > 100) {
      ESP_LOGI("Main", "Clearing metric queue");
        if (xSemaphoreTake(metricQueueMutex, portMAX_DELAY) == pdTRUE) {
            while (metricQueue.size() > 100) {
                metricQueue.pop();
            }
            xSemaphoreGive(metricQueueMutex);
        }
    }
}

void checkSwitchButton(void *pvparameter) {
  Serial.println("checkSwitchButton");
  const int holdTime = 3000; // Thời gian giữ để chuyển chế độ là 3000ms (3s)
  unsigned long pressStartTime = 0;
  bool isHolding = false;
  bool hasChangedMode = false;

  while (1) {
    int switchState = touchRead(T0);

    if (switchState < 30) {
      if (!isHolding) {
        pressStartTime = millis(); // Bắt đầu đo thời gian khi nút được nhấn
        isHolding = true;
        hasChangedMode = false;
        ESP_LOGI("Switch button", "Switch button pressed");
      }
      else if (millis() - pressStartTime >= holdTime && !hasChangedMode) {
        // Chuyển chế độ nếu nút được giữ trong 3 giây
        Serial.println("Switch button held for 3s - Changing mode");
        hasChangedMode = true; // Ensure this block only runs once per press

        if (isAPMode) {
          isAPMode = false;
          delay(1000);
          ESP.restart();
        } else {
          peClient.stop();
          if (peClient.mqttTaskHandle != NULL) {
            vTaskDelete(peClient.mqttTaskHandle); // Xóa task bằng handle
            peClient.mqttTaskHandle = NULL;       // Đặt handle về NULL để tránh xóa lại
          }
          WiFi.mode(WIFI_OFF);
          ESP_LOGE("AP Mode", "AP Mode");
          WiFi.mode(WIFI_AP);
          WiFi.softAP(SSID_AP, PASSWORD_AP, 1, false, MAX_AP_CONNECTIONS);

          server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                    { request->send_P(200, "text/html", setupResponeHTML().c_str()); });
          server.on("/submit", HTTP_POST, handleFormSubmit);
          dnsServer.start(53, "*", WiFi.softAPIP());
          server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
          server.begin();
          isAPMode = true;
          // xTaskCreatePinnedToCore(
          //     readandprint,   /* Function to implement the task */
          //     "readandprint", /* Name of the task */
          //     5000,           /* Stack size in words */
          //     NULL,           /* Task input parameter */
          //     0,              /* Priority of the task */
          //     NULL,           /* Task handle. */
          //     0);             /* Core where the task should run */
          //                     // }
        }
      }
    } else {
      isHolding = false; // Reset lại trạng thái nếu nút không còn được giữ
    }

    vTaskDelay(50 / portTICK_PERIOD_MS); // Chống dội nút bấm
  }
}

String setupResponeHTML() {
  reloadPreferences();
  Serial.println("request index");
  // Serial.println(flashData.ssid);
  String responseHTML = String(index_html);
  // Serial.println(responseHTML);
  ESP_LOGI("Setup", "client_id: %s", flashData.client_id.c_str());
  responseHTML.replace("$ssid", flashData.ssid);
  responseHTML.replace("$password", flashData.password);
  responseHTML.replace("$device_id", flashData.device_id);
  responseHTML.replace("$name", flashData.name);
  responseHTML.replace("$mqtt_username", flashData.mqtt_username);
  responseHTML.replace("$mqtt_password", flashData.mqtt_password);
  responseHTML.replace("$access_token", flashData.access_token);
  responseHTML.replace("$client_id", flashData.client_id);
  // for (int i = 0; i < flashData.leight(); i++)
  // {
  //   responseHTML.replace("$" + flashData[i], flashData[flashData[i]]);
  // }

  // Serial.println("==================================================================");
  // Serial.println(responseHTML);
  return responseHTML;
}

void handleFormSubmit(AsyncWebServerRequest *request)
{
  if (request->hasParam("ssid", true))
  {
    AsyncWebParameter *ssid = request->getParam("ssid", true);
    flashData.ssid = ssid->value();
  }
  if (request->hasParam("password", true))
  {
    AsyncWebParameter *password = request->getParam("password", true);
    flashData.password = password->value();
  }
  if (request->hasParam("device_id", true))
  {
    AsyncWebParameter *device_id = request->getParam("device_id", true);
    flashData.device_id = device_id->value();
  }
  if (request->hasParam("name", true))
  {
    AsyncWebParameter *name = request->getParam("name", true);
    flashData.name = name->value();
  }
  if (request->hasParam("mqtt_username", true))
  {
    AsyncWebParameter *mqtt_username = request->getParam("mqtt_username", true);
    flashData.mqtt_username = mqtt_username->value();
  }

  if (request->hasParam("mqtt_password", true))
  {
    AsyncWebParameter *mqtt_password = request->getParam("mqtt_password", true);
    flashData.mqtt_password = mqtt_password->value();
  }

  if (request->hasParam("access_token", true))
  {
    AsyncWebParameter *access_token = request->getParam("access_token", true);
    flashData.access_token = access_token->value();
  }

  if (request->hasParam("client_id", true))
  {
    AsyncWebParameter *client_id = request->getParam("client_id", true);
    flashData.client_id = client_id->value();
  }

  ESP_LOGI("Setup", "client_id: %s", flashData.client_id.c_str());

  preferences.putString("ssid", flashData.ssid);
  preferences.putString("password", flashData.password);
  preferences.putString("device_id", flashData.device_id);
  preferences.putString("name", flashData.name);
  // preferences.putString("access_token", flashData.access_token);
  preferences.putString("client_id", flashData.client_id);
  preferences.putString("mqtt_username", flashData.mqtt_username);
  preferences.putString("mqtt_password", flashData.mqtt_password);

  preferences.end();
  String message = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Smart Meter</title></head><body><h1>Smart Meter</h1><p>Setup successfully!</p></body></html>";
  request->send(200, "text/html", message);
  delay(1000);
  ESP.restart();
}
void reloadPreferences()
{
  preferences.begin(FLASH_NAME_SPACE, false);
  Serial.println("reloadPreferences");
  flashData.ssid = preferences.getString("ssid", "");
  flashData.password = preferences.getString("password", "");
  flashData.device_id = preferences.getString("device_id", "");
  flashData.name = preferences.getString("name", "");
  flashData.access_token = preferences.getString("access_token", "");
  flashData.client_id = preferences.getString("client_id", "");
  flashData.mqtt_username = preferences.getString("mqtt_username", "");
  flashData.mqtt_password = preferences.getString("mqtt_password", "");

  Serial.println(flashData.ssid);
}