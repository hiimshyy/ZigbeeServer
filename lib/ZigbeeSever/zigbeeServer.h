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

#define ZIGBEE_CONNECT_RETRY 3
#define ZIGBEE_CONNECT_TIMEOUT 1000

struct Device {
    std::string id;
    std::string zb_id;
    std::string secret_key;
    std::string status;
    unsigned long lastest_t = 0;
};

class ZigbeeServer{

    public:
        ZigbeeServer();
        void begin();
        void loop();
        void addDevice(const char *id);
        void addPenddingDevice(const char *id);
        void updatePendingList(std::function<void()> callback);
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
        bool handleIncomingMessage(const std::string& message, const std::string& cmd, const std::string& id,bool check_id = true);
        bool handleIncomingMessage(const std::string& message);
        void handleCommand(const std::string& message);
        void handleData(const std::string& message);
        //void change_device_stt_by_ID(const std::string& id, bool status);
        void checkPendingDevices();
        HardwareSerial *_zigbeeSerial;

        static ZigbeeServer *_instance;
        std::queue<std::string> messageQueue;
        std::function<void(const char *id, const char *data)> messageCallback;
        std::function<void()> onChangeCallback;
        std::function<void()> updateCallback;
};

#endif // ZIGBEESERVER_H