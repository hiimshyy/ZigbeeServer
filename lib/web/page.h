#ifndef _index_page_h
#define _index_page_h

#include <pgmspace.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Document</title>
    <style>
      body {
        font-family: Arial, Helvetica, sans-serif;
      }
      * {
        box-sizing: border-box;
      }
      .container {
        width: 100%;
        max-width: 640px;
        display: flex;
        flex-direction: column;
        margin: auto;
        align-items: center;
      }
      .form {
        border-radius: 5px;
        background-color: #f2f2f2;
        padding: 50px;
      }
      form {
        display: flex;
        flex-direction: column;
        justify-content: center;
        min-width: 320px;
      }
      input[type="text"],
      select,
      textarea {
        width: 100%;
        padding: 12px;
        border: 1px solid #ccc;
        border-radius: 4px;
        box-sizing: border-box;
        margin-top: 6px;
        margin-bottom: 16px;
        resize: vertical;
      }
      input[type="submit"] {
        background-color: #04aa6d;
        color: white;
        padding: 12px 20px;
        border: none;
        border-radius: 4px;
        cursor: pointer;
        margin: 30px 0 0 0;
      }
      input[type="submit"]:hover {
        background-color: #5bad5f;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h2>Setup ESP32</h2>
      <div class="form">
        <form action="/submit" method="POST">
          <label for="ssid">Wifi SSID</label>
          <input type="text" id="ssid" name="ssid" placeholder="$ssid" />
          <label for="password">Wifi password</label>
          <input type="text" id="password" name="password" placeholder="$password" />
          <label for="lname">Device id</label>
          <input type="text" id="device_id" name="device_id" placeholder="$device_id" />
          <label for="lname">Client ID</label>
          <input type="text" id="client_id" name="client_id" placeholder="$client_id" />
          <label for="lname">MQTT Username</label>
          <input type="text" id="mqtt_username" name="mqtt_username" placeholder="$mqtt_username" />
          <label for="lname">mqtt_password</label>
          <input type="text" id="mqtt_password" name="mqtt_password" placeholder="$mqtt_password" />
          <input type="submit" value="Submit" />
        </form>
      </div>
    </div>
    <script>
      window.onload = function() {
        document.getElementById("ssid").value = "$ssid";
        document.getElementById("password").value = "$password";
        document.getElementById("device_id").value = "$device_id";
        document.getElementById("name").value = "$name";
      };
    </script>
  </body>
</html>
)rawliteral";

#endif