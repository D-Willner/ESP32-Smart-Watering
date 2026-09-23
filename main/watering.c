#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/mcpwm_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "watering.h"
#include "measurements.h"

#define PUMP_CONTROL_PIN CONFIG_PUMP_CONTROL_PIN

#ifndef CONFIG_PUMP_CONTROL_INVERT
#define PUMP_ON_LEVEL 1
#define PUMP_OFF_LEVEL 0
#else
#define PUMP_ON_LEVEL 0
#define PUMP_OFF_LEVEL 1
#endif

#define MOISTURE_CHECK_INTERVALL CONFIG_MOISTURE_CHECK_INTERVALL

#define WATER_TIME_CONSTANT CONFIG_WATER_TIME_CONSTANT
#define ANALOGUE_MOISTURE_CONSTANT CONFIG_ANALOGUE_MOISTURE_CONSTANT

#define COMMAND_PUMP_START -1
#define COMMAND_PUMP_STOP -2

static const char* TAG = "WATERING";

static QueueHandle_t queue;
static _Atomic uint16_t pump_on_time;
static _Atomic uint16_t watering_trigger;
static _Atomic uint16_t rearm_trigger;
static atomic_bool watering;

void init_watering()
{
    gpio_reset_pin(PUMP_CONTROL_PIN);
    gpio_set_direction(PUMP_CONTROL_PIN, GPIO_MODE_INPUT);

    queue = xQueueCreate(1,sizeof(int32_t));
    watering = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
     if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }

    uint16_t temp;
    err = nvs_get_u16(handle, "pot", &temp);
    if(err) pump_on_time = CONFIG_DEFAULT_PUMP_ON_TIME;
    else pump_on_time = temp;
    err = nvs_get_u16(handle, "wt", &temp);
    if(err) watering_trigger = CONFIG_DEFAULT_WATERING_TRIGGER;
    else watering_trigger = temp;
    err = nvs_get_u16(handle, "rt", &temp);
    if(err) rearm_trigger = CONFIG_DEFAULT_REARM_TRIGGER;
    else rearm_trigger = temp;
    
    ESP_LOGI(TAG, "Initialized with water: %i, trigger: %i, rearm: %i", 
        pump_on_time, watering_trigger, rearm_trigger);

    nvs_close(handle);

    xTaskCreate(pump_control_task, "Pump Control Task", 2024, NULL, 5, NULL);
    xTaskCreate(watering_task, "Watering Task", 2024, NULL, 4, NULL);
}

void watering_task(void* vParameters)
{
    bool armed = true;
    while(1){
        uint16_t moisture = current_moisture();
        if(armed && moisture <= watering_trigger){
            run_pump(pump_on_time);
            armed = false;
        } else if(!armed && moisture > rearm_trigger){
            armed = true;
        }

        vTaskDelay(pdMS_TO_TICKS(MOISTURE_CHECK_INTERVALL));
    }
}

void pump_control_task(void* vParameters)
{
    while(1){
        int32_t command;
        BaseType_t ret = xQueueReceive(queue, &command, portMAX_DELAY);
        if(ret == errQUEUE_EMPTY) continue; // should not happen

        if(command == 0) continue;
        else if(command > 0){
            watering = true;
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_ON_LEVEL);
            vTaskDelay(pdMS_TO_TICKS(pump_on_time));
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
            watering = false;
        } else if(command == -1){
            watering = true;
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_ON_LEVEL);
        } else if(command == -2){
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
            watering = false;
        } else {
            ESP_LOGI(TAG, "Received invalid command: %i", command);
        }
    }
}

esp_err_t start_pump()
{
    if(xQueuePeek(queue, NULL, 0) != pdPASS) return ESP_FAIL;

    int32_t command = COMMAND_PUMP_START;
    return xQueueOverwrite(queue, &command) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t start_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken)
{
    int32_t command;
    if(xQueuePeek(queue, &command, 0) != pdPASS) return ESP_FAIL;

    command = COMMAND_PUMP_START;
    return xQueueOverwriteFromISR(queue, &command, pxHigherPriorityTaskWoken) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t stop_pump()
{
    int32_t command = COMMAND_PUMP_STOP;
    return xQueueOverwrite(queue, &command) == pdPASS ? ESP_OK : ESP_FAIL;
}


esp_err_t stop_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken)
{
    int32_t command = COMMAND_PUMP_STOP;
    return xQueueOverwriteFromISR(queue, &command, pxHigherPriorityTaskWoken) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t run_pump(uint16_t time_ms)
{
    int32_t command;
    //if(xQueuePeek(queue, &command, 0) != pdPASS) return ESP_FAIL;

    command = (int32_t)time_ms;
    return xQueueOverwrite(queue, &command) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t run_pump_fromISR(uint16_t time_ms, BaseType_t* pxHigherPriorityTaskWoken)
{
    int32_t command;
    if(xQueuePeekFromISR(queue, &command) != pdPASS) return ESP_FAIL;

    command = (int32_t)time_ms;
    return xQueueOverwriteFromISR(queue, &command, pxHigherPriorityTaskWoken) == pdPASS ? ESP_OK : ESP_FAIL;
}

uint16_t get_pump_on_time()
{
    return pump_on_time;
}

uint16_t get_pump_amount()
{
    return WATER_TIME_CONSTANT * pump_on_time;
}

float get_trigger_humidity_pct()
{
    return ANALOGUE_MOISTURE_CONSTANT * watering_trigger;
}

float get_rearm_humidity_pct()
{
    return ANALOGUE_MOISTURE_CONSTANT * rearm_trigger;
}

bool is_watering()
{
    return watering;
}

void set_pump_on_time(uint16_t time_ms)
{
    pump_on_time = time_ms;
}

void set_trigger_humidity(uint16_t val)
{
    watering_trigger = val;
}

void set_rearm_humidity(uint16_t val)
{
    rearm_trigger = val;
}

void save_config(uint16_t time_ms, uint16_t watering_trigger_val, uint16_t rearm_trigger_val)
{
    pump_on_time = time_ms;
    watering_trigger = watering_trigger_val;
    rearm_trigger = rearm_trigger_val;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
     if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u16(handle, "pot", time_ms);
    if(err) ESP_LOGE(TAG, "Could not save pump_on_time to NVS: (%s)", esp_err_to_name(err)); 
    err = nvs_set_u16(handle, "wt", watering_trigger_val);
    if(err) ESP_LOGE(TAG, "Could not save watering_trigge to NVS: (%s)", esp_err_to_name(err)); 
    err = nvs_set_u16(handle, "rt", rearm_trigger_val);
    if(err) ESP_LOGE(TAG, "Could not save rearm_trigger to NVS: (%s)", esp_err_to_name(err)); 

    err = nvs_commit(handle);
    if(err) ESP_LOGE(TAG, "Could not save to NVS: (%s)", esp_err_to_name(err));

    nvs_close(handle);
}
