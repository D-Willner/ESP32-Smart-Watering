## ESP32 Smart Watering
IOT project built in C for ESP32 microcontrollers using the ESP IDF framework.
* Automatically waters your plant by controlling a pump based on measured soil moisture. Also allows for manual control using a button.
* Webinterface with mDNS to allow easy access from home network. Allows viewing current soil moisture, manually triggering watering and configuring the program settings. 
* HTTP API to allow other programs to interact with the ESP32. 
* Settings are stored in non volatile memory and remain after restarting the microcontroller.
* Project can be easily configured to different setups using Kconfig.

The following two images show the web interface accessed from a phone browser.

<p align="center">
<img src=".github/mobile_main.jpg" height="400" alt="Main page"/>
<img src=".github/mobile_config.jpg" height="400" alt="Config page"/>
</p>

### Technical Overview
The program consists of different FreeRTOS tasks:
* <b>Measurement:</b> Initializes the manual control button's GPIO settings and registers its ISR. Then initializes the ADC unit and starts a task, that measures the soil moisture in predetermined intervalls. 
* <b>Pump Control:</b> Task which controls the watering pump. Offers function to start and stop the pump or let it run for a fixed amount of time.
* <b>Watering:</b> Task which checks whether the pump should be started based on current soil moisture. It also controls the program configuration.
* <b>HTTP_server:</b> Initializes by connecting to Wifi and then starts an esp_http_server. The server registeres handlers for the API and the HTML pages, which are embedded as binary data using the ESP32 build system

The inter task communication is handled using atomic variables and queues.  
With default settings, the HTTP server can be accessed by the URL "watering.local" thanks to mDNS.
The HTTP API is then given by the following table.

| URL         | Method | Description                                              | JSON Format                                                                       |
|-------------|--------|----------------------------------------------------------|-----------------------------------------------------------------------------------|
| /api/status | GET    | Returns JSON with  current soil moisture and pump status | { "humidity_pct: double, "watering": bool }                                       |
| /api/water  | POST   | Starts watering the plant for configured amount          |                                                                                   |
| /api/config | GET    | Returns JSON with current program settings               | { "water_amount_ml: int, "trigger_humidity_pct: int, "rearm_humidity_pct": int }  |
| /api/config | POST   | Adopts the settings in the transferred JSON              | { "water_amount_ml: int, "trigger_humidity_pct": int, "rearm_humidity_pct": int } |

The project expects a pump which can be controlled by a simple high or low GPIO output, as well as a moisture sensor, which delivers an analog output. Additionally a button for manual control of the pump can be used. 
An example circuit that can be used for the standard configuration of the program is shown in the following diagram.

<p align="center">
<img src=".github/circuit.png" height="300" alt="Circuit"/>
</p>


### Usage Instructions
To compile and flash this program you can use the ESP-IDF plugin for VSCode.
After importing the project into VSCode you need to make sure the components "mDNS" and "Cjson" are both set as dependencies by typing the following into the ESP-IDF Terminal:

`idf.py add-dependency espressif/cjson^1.7.19~2`  
`idf.py add-dependency espressif/mdns`

Then the project must be configured either by using

`idf.py menuconfig`

or by using the graphical version of menuconfig, that is included in the VSCode plugin shown in the following image.
<p align="center">
<img src=".github/Kconfig.png" height="500" alt="KConfig page"/>
</p>

Most settings can be left unchanged, but *WiFi SSID* and *WiFi Password* must be set in the menu *Network Configuration*. Additionally to make the numbers reported in the web interface accurate the two constants *K* and *L* in *Pump Configuration* need to be set using the terminal version of menuconfig (use "f" for help on their meaning).  
Lastly the project can be build and flashed to an ESP32. A quick test can be done by accessing the web interface.