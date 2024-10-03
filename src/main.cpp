#include <Arduino.h>
#include "zigbeeServer.h"
#include "PEClient.h"
#include "esp_log.h"
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

#define WIFI_SSID "VanPhong2.4g"
#define WIFI_PASSWORD "Vp@1denchin"
// #define WIFI_SSID "Co Nhu"
// #define WIFI_PASSWORD "conhu123"

#define MQTT_SERVER "broker.tbedev.cloud"
#define MQTT_PORT 1883

// #define CLIENT_ID "50bf5161-f426-4c26-8ea1-a5ff53461cbb"
// #define USERNAME "demo"
// #define PASSWORD "demoesp"
#define CLIENT_ID "e03a686f-80d0-4606-a013-3e9bf47b36ae"
#define USERNAME "tiensy"
#define PASSWORD "06102003"
#define LED1_PIN 2

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org",3600 * 0, 60000); // Update mỗi 60 giây

PEClient peClient(WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, MQTT_PORT, CLIENT_ID, USERNAME, PASSWORD);
ZigbeeServer zigbeeServer;
void led1Callback(String value);
void sendAttributes();
void onCollectData(const char *id, const char *data);
void getDevice(String value);

struct Metric {
    std::string name;
    double value;
    uint64_t ts;
};

struct Attribute {
    std::string name;
    std::string value;
};

std::queue<Metric> metricQueue; // Khai báo queue để lưu trữ các metric
std::vector<Attribute> attributes; // Khai báo vector attributes
SemaphoreHandle_t metricQueueMutex; // Mutex để bảo vệ truy cập vào hàng đợi

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