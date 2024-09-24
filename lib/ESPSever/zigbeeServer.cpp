#include <zigbeeServer.h>
#include <algorithm>

uint32_t calculateCRC32(const char* data, size_t length);

ZigbeeServer* ZigbeeServer::_instance = nullptr;

ZigbeeServer::ZigbeeServer() : _zigbeeSerial(&Serial1)
{
}

void ZigbeeServer::begin() {
    ESP_LOGI("ZigbeeServer", "Starting...");
    initZigbee();
    // broadcastMessage();
    xTaskCreatePinnedToCore(
        [](void *pvParameters)
        {
            ZigbeeServer *zigbeeServer = static_cast<ZigbeeServer *>(pvParameters);
            for (;;)
            {
                zigbeeServer->loop();
                vTaskDelay(10 / portTICK_PERIOD_MS); // Giảm tần suất kiểm tra
            }
        },
        "ZigbeeServerTask",
        10000,
        this,
        1,
        NULL,
        0 // Chạy trên core 0
    );
}

void ZigbeeServer::loop() {
    static std::string incomingMessage; // Thêm static để lưu trữ tạm thời dữ liệu nhận được
    while (_zigbeeSerial->available()) {
        Serial.println(".");
        char c = _zigbeeSerial->read();
        incomingMessage += c;
        if (c == '\n') {
            if (!incomingMessage.empty()) {
                handleIncomingMessage(incomingMessage);
                ESP_LOGI("zigbeeServer", "Received: %s", incomingMessage.c_str());
                incomingMessage.clear();
            }
        }
    }
    if (!messageQueue.empty()) {
        std::string command = messageQueue.front();
        messageQueue.pop();
        ESP_LOGI("zigbeeServer", "Get command: %s", command.c_str());
        //calculate CRC

        uint32_t calculated_crc = calculateCRC32(command.c_str(), command.length());

        char crcString[9];
        snprintf(crcString, sizeof(crcString), "%08X", calculated_crc);
        ESP_LOGI("zigbeeServer", "CRC: %s", crcString);
        // end calculate CRC
        int retry = 0;
        while (retry < ZIGBEE_CONNECT_RETRY)
        {
            Serial.print("Sent command: ");
            Serial.println(command.c_str());
            unsigned long start_time = millis();
            bool is_sent = false;

            std::string message = command + ",CRC:" + crcString + "\n";
            _zigbeeSerial->printf(message.c_str());
            // Serial.println(message.c_str());
            ESP_LOGI("zigbeeServer", "Send: %s", message.c_str());
            while (millis() - start_time < ZIGBEE_CONNECT_TIMEOUT) {
                if (_zigbeeSerial->available()) {
                    String data = _zigbeeSerial->readStringUntil('\n');
                    data.trim();
                    std::string Data = std::string(data.c_str());

                    if (data.length() > 0) {
                        if (!command.find("ID:")) {
                            if (handleIncomingMessage(Data)) {
                                
                            }
                        } else {
                            std::string id = command.substr(3,command.find(",")-command.find("ID:")-3);
                            std::string cmd = command.substr(command.find("CMD:") + 4);
                            Serial.print("command: ");
                            Serial.print(cmd.c_str());
                            Serial.print(" - id: ");
                            Serial.println(id.c_str());

                            if (handleIncomingMessage(Data, cmd, id)) {
                                // In Arduino, we might use EEPROM to store device status
                                // For simplicity, we'll just print a message
                                Serial.print("Device ");
                                Serial.print(id.c_str());
                                Serial.println(" status changed to active");
                                is_sent = true;
                                break;
                            }
                        }
                    }
                    data.clear();
                    Data.clear();
                }
            }

            if (is_sent) break;
            ++retry;
            Serial.print("Retry: ");
            Serial.println(retry);
        }

        if (retry == ZIGBEE_CONNECT_RETRY) {
            Serial.print("Failed to send command: ");
            Serial.println(command.c_str());
            Serial.println("Error logged: Send command failed");
            if (command.find("ID:")){
                std::string id = command.substr(3, command.find(",")-command.find("ID:")-3);

                auto device = std::find_if(deviceList.begin(), deviceList.end(), [&id](const Device& device) {
                    return device.id == id;
                });

                device->status = false;
            }
        }   
    }
}

void ZigbeeServer::addDevice(const char *id) {
    Device device;
    device.id = id;
    deviceList.push_back(device);
}
void ZigbeeServer::addPenddingDevice(const char *id){
    Device device;
    device.id = id;
    device.lastest_t = 0;
    pendingDeviceList.push_back(device);
}

void ZigbeeServer::onMessage(std::function<void(const char *id, const char *data)> callback) {
    messageCallback = callback;
}

void ZigbeeServer::onChange(std::function<void()> callback) {
    onChangeCallback = callback;
}

ZigbeeServer* ZigbeeServer::getInstance() {
    if (_instance == nullptr) {
        _instance = new ZigbeeServer();
    }
    return _instance;
}

void ZigbeeServer::checkDevice(const char *id) {
    sendCommand(id, "CHECK");
}

void ZigbeeServer::sendCommand(const char *id, const char *cmd) {
    std::string message = std::string("ID:") + id +",SECRET_KEY:123"+ ",CMD:" + cmd;
    messageQueue.push(message);
}

void ZigbeeServer::sendCommand(const char *id, const char *secrect_key, const char *cmd) {
    std::string message = std::string("ID:") + id +",SECRECT_KEY:"+ secrect_key +",CMD:" + cmd;
    messageQueue.push(message);
}

void ZigbeeServer::broadcastMessage() {
    messageQueue.push("CMD:BRD:DISC");
    // _zigbeeSerial->println("CMD:BRD:DISC");
}

void ZigbeeServer::initZigbee() {
    _zigbeeSerial->begin(9600, SERIAL_8N1, 16, 17); // Thay đổi RX_PIN và TX_PIN theo cấu hình của bạn
    // _zigbeeSerial->println("AT+ZSET:ROLE=COORD");
    // delay(1000);
    // _zigbeeSerial->println("AT+PANID=1234");
    // delay(1000);
    // _zigbeeSerial->println("AT+START");  
}

uint32_t calculateCRC32(const char* data, size_t length) {
    uint32_t crc = 0xffffffff;
    while (length--) {
        uint8_t c = *data++;
        for (uint32_t i = 0x80; i > 0; i >>= 1) {
            bool bit = crc & 0x80000000;
            if (c & i) {
                bit = !bit;
            }
            crc <<= 1;
            if (bit) {
                crc ^= 0x04c11db7;
            }
        }
    }
    return ~crc;
}

bool checkCRC32(const std::string& data_with_crc) {
    size_t pos = data_with_crc.rfind(",CRC:");
    if (pos == std::string::npos) {
        return false;
    }

    std::string data = data_with_crc.substr(0, pos);
    std::string received_crc = data_with_crc.substr(pos + 5);

    uint32_t calculated_crc = calculateCRC32(data.c_str(), data.length());
    
    char crcString[9];
    snprintf(crcString, sizeof(crcString), "%08X", calculated_crc);
    
    // ESP_LOGI("zigbeeServer", "Full input: %s", data_with_crc.c_str());
    // ESP_LOGI("zigbeeServer", "Data part: %s", data.c_str());
    // ESP_LOGI("zigbeeServer", "CRC part: %s", received_crc.c_str());

    // ESP_LOGI("zigbeeServer", "Calculated CRC length: %d", strlen(crcString));
    // ESP_LOGI("zigbeeServer", "Received CRC length: %d", received_crc.length());

    received_crc = received_crc.substr(0, 8);
    return std::string(crcString) == std::string(received_crc);
}

bool ZigbeeServer::handleIncomingMessage(const std::string& message){
    if (!checkCRC32(message)) {
        ESP_LOGE("handleIncomingMessage", "Invalid CRC");
        return false;
    }

    if(message.find("CMD:")) handleCommand(message);
    if(message.find("DATA:")) handleData(message);

    return true;
}

bool ZigbeeServer::handleIncomingMessage(const std::string& message, const std::string& cmd, const std::string& id, bool check_id) {
    if (!checkCRC32(message)) {
        ESP_LOGE("handleIncomingMessage", "Invalid CRC");
        return false;
    }

    if(message.find("CMD:") != std::string::npos) {
        handleCommand(message);
        std::string incoming_command = message.substr(message.find("CMD:")+4);
        if(check_id){
            return (incoming_command == cmd && id == message.substr(3, message.find(",")-message.find("ID:")-3));
        }
        else return incoming_command == cmd;
    } else if(message.find("DATA:") != std::string::npos) {
        handleData(message);
        if(check_id){
            return (cmd == "get_data" && id == message.substr(3, message.find(",")-3));
        }
        else return cmd == "get_data";

    } else {
        ESP_LOGE("handleIncomingMessage", "Invalid message: %s", message.c_str());
    }
}

void ZigbeeServer::handleCommand(const std::string& message) {
    std::string id = message.substr(3, message.find(",") - message.find("ID:") - 3);
    std::string command = message.substr(message.find("CMD:") + 4, message.find(",CRC:") - message.find("CMD:") - 4);
    ESP_LOGI("zigbeeServer", "In handleCommand - ID: %s, Command: %s", id.c_str(), command.c_str());
    
    if (command == "BRD:DISC") {
        auto it = std::find_if(deviceList.begin(), deviceList.end(), [&id](const Device& device) {
            return device.id == id;
        });

        if (it == deviceList.end()) {
            auto pendingIt = std::find_if(pendingDeviceList.begin(), pendingDeviceList.end(), [&id](const Device& device) {
                return device.id == id;
            });
            if(pendingIt == pendingDeviceList.end()){
                addPenddingDevice(id.c_str());
                pendingIt->lastest_t++;
                if (onChangeCallback) {
                    onChangeCallback();
                }
            } else {
                if (pendingIt->lastest_t >= 10) {
                    pendingDeviceList.erase(pendingIt);
                }
            }

            ESP_LOGI("handleCommand", "Pending devices:");
            for (const auto& device : pendingDeviceList) {
                ESP_LOGI("handleCommand", "  ID: %s", device.id.c_str());
            }            
        }
    } else if(command.find("led_status:") != std::string::npos){
        std::string status = command.substr(command.find(":") + 1);
        ESP_LOGI("zigbeeServer", "LED Status for device %s: %s", id.c_str(), status.c_str());
        
        auto it = std::find_if(deviceList.begin(), deviceList.end(), 
            [&id](const Device& device) { return device.id == id; });
        if (it != deviceList.end()) {
            it->status = status;
            if (onChangeCallback) {
                onChangeCallback();
            }
        }
    } else if(command.find("reset_data") != std::string::npos){
        ESP_LOGI("zigbeeServer", "Resetting data for device %s", id.c_str());
    
        auto it = std::find_if(deviceList.begin(), deviceList.end(), 
            [&id](const Device& device) { return device.id == id; });
        if (it != deviceList.end()) {
            it->resetData();
            if (onChangeCallback) {
                onChangeCallback();
            }
        }
    } else if(command.find("get_data") != std::string::npos) handleData(message);

    else {
        if (messageCallback) {
             messageCallback(id.c_str(), command.c_str());
        }
    }
}

void ZigbeeServer::handleData(const std::string& message) {
    size_t pos = message.find(",DATA:");
    std::string id = message.substr(3, pos - 3); // Skip "ID:"
    std::string data = message.substr(pos + 6, message.find(",CRC:") - pos - 6);

    auto it = std::find_if(deviceList.begin(), deviceList.end(), [&id](const Device& device) {
            return device.id == id;
        });
    if( it != deviceList.end()){
        it->status = true;
    } else {
        auto pendingIt = std::find_if(pendingDeviceList.begin(), pendingDeviceList.end(), [&id](const Device& device) {
            return device.id == id;
        });
        if (pendingIt == pendingDeviceList.end())
        {
            addPenddingDevice(id.c_str());
            pendingIt->lastest_t++;
            if (onChangeCallback) {
                onChangeCallback();
            }
        } else{
            if (pendingIt->lastest_t >= 10) pendingDeviceList.erase(pendingIt);
        }
        
    }
}