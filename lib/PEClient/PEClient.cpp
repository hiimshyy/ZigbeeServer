#include <PEClient.h>

PEClient *PEClient::_instance = nullptr;
char* PEClient::device_ids[MAX_DEVICES] = {0};
int PEClient::device_count = 0;

/**
 * @name PEClient
 * @brief Hàm khởi tạo PEClient 
 *
 * @param {const char*} wifiSSID - Tên wifi
 * @param {const char*} wifiPassword - Mật khẩu wifi
 * @param {const char*} mqttServer - Server MQTT
 * @param {int} mqttPort - Cổng MQTT
 * @param {const char*} clientId - ID của thiết bị
 * @param {const char*} token
 *
 * @return None
 */
PEClient::PEClient()
    : _client(_espClient)
{
    _client.setServer(_mqttServer, _mqttPort);
    _client.setCallback(callback);

    _sendMetricTopic = "v1/devices/";
    _sendMetricTopic += _clientId;
    _sendMetricTopic += "/metrics";

    _sendAttributeTopic = "v1/devices/";
    _sendAttributeTopic += _clientId;
    _sendAttributeTopic += "/attributes";

    _instance = this;
}
PEClient::PEClient(const char *wifiSSID, const char *wifiPassword, const char *mqttServer, int mqttPort, const char *clientId, const char *username, const char *password)
    : _ssid(wifiSSID), _password(wifiPassword), _mqttServer(mqttServer), _mqttPort(mqttPort), _clientId(clientId), _username(username), _passwordMqtt(password), _client(_espClient)
{
    _client.setServer(_mqttServer, _mqttPort);
    _client.setCallback(callback);

    _sendMetricTopic = "v1/devices/";
    _sendMetricTopic += _clientId;
    _sendMetricTopic += "/metrics";

    _sendAttributeTopic = "v1/devices/";
    _sendAttributeTopic += _clientId;
    _sendAttributeTopic += "/attributes";

    _instance = this;
}

PEClient::~PEClient()
{
    _client.disconnect();
    ESP_LOGI("MqttClient", "Disconnected");

}
void PEClient::init(const char *wifiSSID, const char *wifiPassword, const char *mqttServer, int mqttPort, const char *clientId, const char *username, const char *password)
{
    _ssid = wifiSSID;
    _password = wifiPassword;
    _mqttServer = mqttServer;
    _mqttPort = mqttPort;
    _clientId = clientId;
    _username = username;
    _passwordMqtt = password;

    _client.setServer(_mqttServer, _mqttPort);
    _client.setCallback(callback);

    _sendMetricTopic = "v1/devices/";
    _sendMetricTopic += _clientId;
    _sendMetricTopic += "/metrics";

    _sendAttributeTopic = "v1/devices/";
    _sendAttributeTopic += _clientId;
    _sendAttributeTopic += "/attributes";

    _instance = this;
}

/**
 * @name begin
 * @brief Khởi tạo PEClient
 * 
 * @param None
 * 
 * @return None
 */
void PEClient::begin() {
    // Serial.begin(115200);
    initWiFi();
    _is_stopped = false;
    xTaskCreatePinnedToCore(
        [](void *pvParameters)
        {
            PEClient *peClient = static_cast<PEClient *>(pvParameters);
            for (;;)
            {
                peClient->loop();
                vTaskDelay(10);
            }
        },
        "PEClientTask",
        10000,
        this,
        1,
        &mqttTaskHandle,
        1 // Chạy trên core 1
    );
}

/**
 * @name loop
 * @brief Vòng lặp chính của PEClient
 * 
 * @param None
 * 
 * @return None
 */
void PEClient::loop() {
    if (!_client.connected())
    {
        reconnect();
    }
    _client.loop();
}

void PEClient::stop() {
    _client.disconnect();
    _is_stopped = true;
    ESP_LOGI("MqttClient", "Disconnected");
        // Xóa task để giải phóng tài nguyên
    // if (mqttTaskHandle != NULL)
    // {
    //     vTaskDelete(mqttTaskHandle); // Xóa task bằng handle
    //     mqttTaskHandle = NULL;       // Đặt handle về NULL để tránh xóa lại
    // }
    WiFi.disconnect();
    ESP_LOGI("MqttClient", "WiFi disconnected");

}

/**
 * @name connected
 * @brief Kiểm tra xem PEClient có kết nối được với MQTT hay không
 * 
 * @param None
 * 
 * @return boolean - True nếu kết nối được, False nếu ngược lại
 */
boolean PEClient::connected() {
    return _client.connected();
}


/**
 * @name initWiFi
 * @brief Khởi tạo kết nối WiFi
 * 
 * @param None
 * 
 * @return None
 */
void PEClient::initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
    ESP_LOGI("PEClient", "Connecting to the WiFi network");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(1000);
        ESP_LOGI("PEClient", "WiFi status: %d", WiFi.status());
        if(WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_CONNECTION_LOST || WiFi.status() == WL_CONNECT_FAILED) {
            ESP_LOGE("PEClient", "WiFi disconnected");
            // ESP.restart();
        }
    }
    ESP_LOGI("PEClient", "Connected to the WiFi network");
    ESP_LOGI("PEClient", "IP address: %s", WiFi.localIP().toString().c_str());
}

/**
 * @name reconnect
 * @brief Kết nối lại với MQTT
 * 
 * @param None
 * 
 * @return None
 */
void PEClient::reconnect()
{
    while (!_client.connected())
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            initWiFi();
        }
        ESP_LOGI("PEClient", "Attempting MQTT connection...");
        if (_client.connect(_clientId, _username, _passwordMqtt))
        {
            ESP_LOGI("PEClient", "connected");
            String topic = "v1/devices/";
            topic += _clientId;
            topic += "/attributes/set";
            _client.subscribe(topic.c_str());
        }
        else
        {
            ESP_LOGE("PEClient", "failed, rc=%d try again in 5 seconds", _client.state());
            delay(5000);
        }
    }
}

/**
 * @name callback
 * @brief Xử lý dữ liệu nhận được từ MQTT
 *
 * @param {char*} topic - Chủ đề
 * @param {byte*} message - Dữ liệu
 * @param {unsigned int} length - Độ dài dữ liệu
 *
 * @return None
 */
void PEClient::callback(char *topic, byte *message, unsigned int length)
{
    String messageTemp = "";
    for (int i = 0; i < length; i++)
    {
        messageTemp += (char)message[i];
    }

    ESP_LOGD("PEClient", "Message arrived on topic: %s. Message: %s", topic, messageTemp.c_str());

    // Parse the JSON message
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, messageTemp);
    if (error)
    {
        ESP_LOGE("PEClient", "deserializeJson() failed: %s", error.c_str());
        return;
    }

    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair kv : obj)
    {
        String key = kv.key().c_str();
        String value = kv.value().as<String>();
        ESP_LOGI("PECallback","Key: %s Value: %s", key, value.c_str());
        

        if (_instance->_callbacks.find(key) != _instance->_callbacks.end())
        {
            _instance->_callbacks[key](value);
        }
    }
}

/**
 * @name sendMetric
 * @brief Gửi dữ liệu đo được lên MQTT
 *
 * @param {uint64_t} timestamp - Thời gian
 * @param {const char*} key - Tên thông số
 * @param {double} value - Giá trị
 *
 * @return None
 */
void PEClient::sendMetric(uint64_t timestamp, const char *key, double value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;
    doc["ts"] = timestamp;

    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendMetricTopic.c_str(), buffer);
    ESP_LOGI("PEClient", "Send metric: %s", buffer);
}

/**
 * @name sendMetric
 * @brief Gửi dữ liệu đo được lên MQTT
 *
 * @param {const char*} key - Tên thông số
 * @param {double} value - Giá trị
 *
 * @return None
 */
void PEClient::sendMetric(const char *key, double value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;

    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendMetricTopic.c_str(), buffer);
}

/**
 * @name sendAttribute
 * @brief Gửi thông số lên MQTT
 *
 * @param {const char*} key - Tên thông số
 * @param {double} value - Giá trị
 *
 * @return None
 */
void PEClient::sendAttribute(const char *key, double value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;

    JsonObject attributes = doc["attributes"].to<JsonObject>();
    attributes[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendAttributeTopic.c_str(), buffer);
}

/**
 * @name sendAttribute
 * @brief Gửi thông số lên MQTT
 *
 * @param {const char*} key - Tên thông số
 * @param {const char*} value - Giá trị
 *
 * @return None
 */
void PEClient::sendAttribute(const char *key, const char *value)
{
    if (!_client.connected())
    {
        return;
    }
    JsonDocument doc;

    JsonObject attributes = doc["attributes"].to<JsonObject>();
    attributes[key] = value;

    char buffer[256];
    serializeJson(doc, buffer);

    _client.publish(_sendAttributeTopic.c_str(), buffer);
}

/**
 * @name on
 * @brief Đăng ký callback cho một thông số
 *
 * @param {const char*} key - Tên thông số
 * @param {void (*)(String)} callback - Hàm callback
 *
 * @return None
 */
void PEClient::on(const char *key, void (*callback)(String))
{
    _callbacks[key] = std::bind(callback, std::placeholders::_1);
}
