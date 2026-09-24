# ESP32 Smart Watering

An ESP32 plant watering controller written in C with ESP-IDF. It monitors soil moisture, controls a watering pump, and provides a web interface for checking moisture levels and adjusting watering settings over your local Wi-Fi network.

## Features

- Automatic watering based on configurable moisture thresholds.
- Manual watering from the web interface or a physical button.
- Web interface for viewing soil moisture and setting the watering amount, trigger threshold, and rearm threshold.
- HTTP API for integration with other applications.
- Watering settings saved in nonvolatile storage and restored after a restart.
- Configurable GPIO assignments, button behavior, and calibration through Kconfig.
- Local access through mDNS at `http://watering.local` by default.

## Web interface

<p align="center">
  <img src=".github/mobile_main.jpg" width="280" alt="Mobile dashboard showing soil moisture and manual watering controls" />
  <img src=".github/mobile_config.jpg" width="280" alt="Mobile configuration page showing watering amount and moisture thresholds" />
</p>

**Water now** runs the pump for the amount configured on the settings page. On that page, **Set** applies and saves the new settings; **Back** returns to the dashboard without saving changes.

## Hardware and wiring

The example configuration uses:

- An ESP32 development board.
- A soil moisture sensor with an analog output.
- A pump and a suitable power supply.
- A GPIO-controlled pump driver, such as the transistor circuit below.
- An optional push button for manual watering.

The diagram illustrates the default connections. Select the driver, protection diode, resistor, and power supply to match your pump; component part numbers and values are not specified here.

<p align="center">
  <img src=".github/circuit.png" width="740" alt="Example circuit connecting an ESP32 to an analog moisture sensor, a manual button, and a transistor-controlled pump with a flyback diode" />
</p>

| Connection | Default configuration |
|---|---|
| Sensor analog output | GPIO35, ADC1 channel 7 |
| Pump driver control | GPIO27, active high |
| Manual watering button | GPIO26, button connected to ground |
| Button behavior | Pump runs while the button is pressed |

The pump is powered through the driver circuit; the GPIO supplies its control signal. The button uses the ESP32's internal pull-up. Pin assignments and control polarity can be changed in `menuconfig`.

## Getting started

### 1. Prepare the project

Clone or download this repository and open an ESP-IDF terminal in its root directory. You can also use the ESP-IDF extension for Visual Studio Code.

The project targets the original **ESP32** by default. The local dependency lock used when preparing this document records **ESP-IDF 6.1.0**; this is a development-environment reference, not a verified compatibility range.

The component manifest, [`main/idf_component.yml`](main/idf_component.yml), already declares the `espressif/cjson` and `espressif/mdns` dependencies. With the ESP-IDF Component Manager enabled, they are resolved during project configuration; you do not need to add them manually.

### 2. Configure the firmware

For a fresh checkout, select the target before configuring the project:

```sh
idf.py set-target esp32
idf.py menuconfig
```

Review these project menus:

| Menu | What to configure |
|---|---|
| Network Configuration | Wi-Fi SSID and password; optional mDNS hostname |
| ADC Configuration | Sensor GPIO, ADC channel, and sampling interval |
| Manual Watering Button | Enable the button, choose its pin, and select its behavior |
| Pump Configuration | Pump control pin and polarity, default watering settings, and calibration constants |

Set your **Wi-Fi SSID** and **Wi-Fi Password** under **Network Configuration**. Check that the ADC channel and GPIO match your wiring.

The VS Code extension also provides a graphical configuration editor:

<p align="center">
  <img src=".github/Kconfig.png" width="760" alt="ESP-IDF configuration editor showing network, ADC, and manual button settings" />
</p>

Use the terminal version of `menuconfig` to set the calibration constants **K** and **L**, as described below.

### 3. Calibrate your setup

The firmware converts pump runtime into a water amount and scales the sensor's raw ADC reading into a displayed moisture percentage.

**Pump constant K**

```text
water_dispensed_ml = K × pump_on_time_ms
K = measured_volume_ml / measured_runtime_ms
```

Measure the water dispensed over a known runtime using your assembled pump and tubing. For example, 100 mL delivered in 5,000 ms gives `K = 0.02` mL/ms. Enter your measured value as **Water time constant K** under **Pump Configuration**.

**Moisture constant L**

```text
displayed_moisture_pct = L × raw_ADC_reading
```

Enter the scale factor as **Analogue moisture constant L**. For example, assigning a reference reading of 2,000 to a displayed value of 100% gives `L = 0.05`. This establishes a relative scale; it does not establish an absolute soil water-content measurement.

The current conversion uses one multiplier, with no offset or two-point calibration. Automatic watering assumes lower ADC readings mean drier soil. Check your sensor's response in dry and wet conditions before choosing the trigger and rearm thresholds.

### 4. Build, flash, and open the interface

```sh
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your board's serial port, for example `COM3` on Windows or `/dev/ttyUSB0` on Linux.

Once the ESP32 connects to Wi-Fi, open **http://watering.local** in a browser on the same local network. If you changed the mDNS hostname, use that name instead. If the name does not resolve, use the IP address printed in the serial monitor, such as `http://192.168.1.123`.

Check the moisture reading, set a suitable watering amount and thresholds, and use **Water now** to check delivery against your calibration.

## How automatic watering works

Automatic watering uses two thresholds:

1. When armed, the controller starts one timed watering cycle if the sensor reading is at or below the **trigger threshold**.
2. It then disarms automatic watering.
3. It rearms only after the sensor reading rises above the **rearm threshold**.

Choose a rearm threshold higher than the trigger threshold. For example, with a trigger of 20% and a rearm threshold of 40%, the controller waters at or below 20%, waits for moisture to exceed 40%, and can then water again when it falls back to 20% or below.

If watering does not raise the reading above the rearm threshold, the controller remains disarmed. The automatic watering state starts armed after a restart. Manual watering requests are handled separately from this threshold cycle.

The physical button supports either **While pressed** operation or a **Predetermined** watering duration, selected at build time.

### Saved settings and firmware configuration

The web interface saves the watering amount and both thresholds in ESP32 nonvolatile storage. Internally, these are stored as pump runtime and raw ADC thresholds.

Wi-Fi credentials, GPIO assignments, button behavior, and calibration constants are firmware configuration options. Changes to these require rebuilding and flashing. If you change calibration constants, review and save your watering settings again because the stored raw values will be interpreted using the new calibration.

## HTTP API

The examples below use the default host, `http://watering.local`.

| Endpoint | Method | Purpose | Successful response |
|---|---|---|---|
| `/api/status` | GET | Read moisture and pump state | `200 OK`, JSON |
| `/api/water` | POST | Request watering for the configured amount; no request body required | `202 Accepted`, empty body |
| `/api/config` | GET | Read watering settings | `200 OK`, JSON |
| `/api/config` | POST | Apply and save all three watering settings | `204 No Content`, empty body |

### Read status

```sh
curl http://watering.local/api/status
```

Example response:

```json
{
  "humidity_pct": 25.0,
  "watering": false
}
```

### Read or update configuration

`GET /api/config` returns an object in this format. Send all three fields as JSON to `POST /api/config` to update the settings, using `Content-Type: application/json`.

```json
{
  "water_amount_ml": 100,
  "trigger_humidity_pct": 20,
  "rearm_humidity_pct": 40
}
```

The `humidity_pct` field names are the API's names for soil moisture readings and thresholds. Water amounts are estimates derived from pump runtime and K; they are not measured by a flow sensor.

## Technical overview

The firmware uses FreeRTOS tasks, queues, and atomic variables to coordinate sensing and pump control.

| Module | Responsibility |
|---|---|
| [`main/main.c`](main/main.c) | Initializes nonvolatile storage, measurement, watering, and networking |
| [`main/measurements.c`](main/measurements.c) | Samples ADC readings and handles manual button interrupts |
| [`main/watering.c`](main/watering.c) | Runs pump control and automatic watering tasks; loads and saves watering settings |
| [`main/http_server.c`](main/http_server.c) | Initializes Wi-Fi and mDNS and registers web page and API handlers |
| [`main/web/`](main/web/) | Contains HTML pages embedded in the firmware at build time |
| [`main/Kconfig.projbuild`](main/Kconfig.projbuild) | Defines project configuration options |

Sensor readings and pump commands pass through queues. Shared watering configuration and pump state use atomic variables. The web interface is served directly by the ESP32 using `esp_http_server`.
