# ESP32 Smart Watering

An embedded C IoT project for the ESP32, built with the ESP-IDF framework and FreeRTOS. The firmware combines periodic soil moisture measurement, automatic pump control, persistent configuration, and a browser-based interface served directly by the microcontroller over WiFi.
* Automatic plant watering through controlling a pump based on measured soil moisture. 
* Web interface with mDNS to allow easy access from home network. Displays current soil moisture and allows manually triggering watering and configuring the program settings. 
* JSON HTTP API to allow other programs to interact with the ESP32. 
* Persistent storage of settings using ESP32 nonvolatile storage.
* Build-time configuration of project settings like pins, timing and calibration by using Kconfig.

The following two images show the web interface accessed from a phone browser.

<p align="center">
<img src=".github/mobile_main.jpg" height="500" alt="Main page"/>
<img src=".github/mobile_config.jpg" height="500" alt="Config page"/>
</p>

The automatic watering works by periodically reading the soil humidity using an appropriate sensor. Once this value crosses under the trigger humidity threshold a pump is started and waters the plant. The humidity level must then cross over the rearm humidity threshold before the pump will be started again.  
The right image shows how the thresholds and the water amount can be configured using the web interface. In the left image the current soil humidity and the option to manually start the pump are visible.

## Hardware and wiring

The project expects a pump which is controlled through a GPIO-driven switching circuit, as well as a soil humidity sensor, which delivers an analog output. Additionally a button for manual control of the pump can be used. 
An example circuit, that can be used for the standard configuration of the program is shown in the following diagram.

<p align="center">
<img src=".github/circuit.png" height="300" alt="Circuit"/>
</p>

## Technical Overview
The program consists of different FreeRTOS tasks:
* <b>Measurement:</b> Initializes the manual control button's GPIO settings and registers its ISR. Then initializes the ADC unit and starts a task, that measures the soil humidty in predetermined intervals. 
* <b>Pump Control:</b> Task which controls the watering pump. Provides functions to start and stop the pump or let it run for a fixed amount of time.
* <b>Watering:</b> Task which periodically checks whether the pump should be started based on current soil moisture. It also controls the program configuration.
* <b>HTTP Server:</b> Initializes by connecting to Wifi and then starts an esp_http_server. The server registers handlers for the API and the HTML pages, which are embedded as binary data using the ESP32 build system

The inter task communication is handled using atomic variables, notifications and queues.  
With default settings, the HTTP server can be accessed by the URL "http://watering.local".
The HTTP API is described by the following table.

| Endpoint    | Method | Description                                              | JSON Format                                                                       |
|-------------|--------|----------------------------------------------------------|-----------------------------------------------------------------------------------|
| /api/status | GET    | Returns JSON with  current soil moisture and pump status | { "humidity_pct: double, "watering": bool }                                       |
| /api/water  | POST   | Starts watering the plant for configured amount          |                                                                                   |
| /api/config | GET    | Returns JSON with current program settings               | { "water_amount_ml: int, "trigger_humidity_pct: int, "rearm_humidity_pct": int }  |
| /api/config | POST   | Applies and saves the settings in the transferred JSON              | { "water_amount_ml: int, "trigger_humidity_pct": int, "rearm_humidity_pct": int } |

## Usage Instructions
This program uses ESP-IDF version 6.1 as well as the modules "mDNS" and "Cjson".
To compile and flash this program the ESP-IDF plugin for VSCode can be used.
The project must be configured either by using

`idf.py menuconfig`

or by using the graphical version of menuconfig, that is included in the VSCode plugin and partially shown in the following image.
<p align="center">
<img src=".github/Kconfig.png" height="300" alt="KConfig page"/>
</p>

Most settings can be left unchanged, but *WiFi SSID* and *WiFi Password* must be set in the menu *Network Configuration*. Additionally to make the numbers reported in the web interface accurate, three constants *K*, *L* and *A* need to be calibrated and set using the terminal version of menuconfig.  
The pump constant *K* describes how much water the pump dispenses per unit of time:  

`water_dispensed[ml] = K × pump_active[ms]`

Next the constants *L* and *A* describe the relation between the signal the ADC reads and the soil humidity:  

`soil_humidity[%] = L × (ADC_reading - A)`

The project can be built and flashed to an ESP32 connected by USB in the usual way, for example by typing the commands  

```
idf.py build  
idf.py -p PORT flash
```
