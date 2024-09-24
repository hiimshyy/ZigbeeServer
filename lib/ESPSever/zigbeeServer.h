#ifndef ZIGBEESERVER_H
#define ZIGBEESERVER_H

#include <vector>
#include <string>
#include <queue>
#include <functional>
#include <map>
#include <ArduinoJson.h>
#include "HardwareSerial.h"
#include <algorithm>
#include <sstream>

struct Device {
    std::string id;
    std::string zb_id;
    std::string secret_key;
    std::string status;
    int lastest_t = 0;

    void resetData(){
        ESP_LOGI("Device", "Resetting data for device %s", id.c_str());
        status = "OFF";
        lastest_t = 0;
        
        ESP_LOGI("Device", "Data reset completed for device %s", id.c_str());
    }
};

class ZigbeeServer{
    public:
    ZigbeeServer();
    void begin();
    void loop();
    void addDevice(const char *id);
    void addPenddingDevice(const char *id);
    void onMessage(std::function<void(const char *id, const char *data)> callback);
    void onChange(std::function<void()> callback);
    static ZigbeeServer* getInstance();
    void checkDevice(const char *id);
    void sendCommand(const char *id, const char *cmd);
    void sendCommand(const char *id, const char *secrect_key, const char *cmd);
    void broadcastMessage();
    

    std::vector<Device> deviceList;
    std::vector<Device> pendingDeviceList;

private:
    void initZigbee();
    //uint32_t calculateCRC32(const char* data, size_t length);
    bool handleIncomingMessage(const std::string& message, const std::string& cmd, const std::string& id,bool check_id = true);
    bool handleIncomingMessage(const std::string& message);
    void handleCommand(const std::string& message);
    void handleData(const std::string& message);
    void change_device_stt_by_ID(const std::string& id, bool status);
    HardwareSerial *_zigbeeSerial;

    std::map<String, std::function<void(String)>> _callbacks;
    static ZigbeeServer *_instance;
    std::queue<std::string> messageQueue;
    std::function<void(const char *id, const char *data)> messageCallback;
    std::function<void(const char *id, const char *data)> reciveCallback;
    std::function<void()> onChangeCallback;
    
    const int ZIGBEE_CONNECT_RETRY = 3;
    const unsigned long ZIGBEE_CONNECT_TIMEOUT = 1000;
};

#endif // ZIGBEESERVER_H