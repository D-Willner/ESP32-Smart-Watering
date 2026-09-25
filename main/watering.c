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

#define WATER_TIME_CONSTANT CONFIG_WATER_TIME_CONSTANT
#define ANALOGUE_MOISTURE_CONSTANT CONFIG_ANALOGUE_MOISTURE_CONSTANT
#define ANALOGUE_MOISTURE_OFFSET CONFIG_ANALOGUE_MOISTURE_OFFSET

#define COMMAND_PUMP_START -1
#define COMMAND_PUMP_STOP -2

static const char* TAG = "WATERING";

static QueueHandle_t command_queue;
static _Atomic uint16_t pump_on_time;
static _Atomic uint16_t watering_trigger;
static _Atomic uint16_t rearm_trigger;
static atomic_bool watering;

static void watering_task(void* pvParameters)
{
    bool armed = true;
    uint16_t moisture;
    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        moisture = current_moisture();
        //ESP_LOGI(TAG, "Watering task received value %i", moisture);

        if(armed && moisture <= watering_trigger){
            ESP_LOGI(TAG, "Starting pump because humidity is %i", moisture);
            run_pump(pump_on_time);
            armed = false;
        } else if(!armed && moisture > rearm_trigger){
            ESP_LOGI(TAG, "Rearming pump because humidity is %i", moisture);
            armed = true;
        }
    }
}

static void pump_control_task(void* vParameters)
{
    while(1){
        int32_t command;
        BaseType_t ret = xQueueReceive(command_queue, &command, portMAX_DELAY);
        if(ret == errQUEUE_EMPTY) continue; // should not happen

        if(command == 0) continue;
        else if(command > 0){
            ESP_LOGI(TAG, "Pump will be run for %i ms", command);
            watering = true;
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_ON_LEVEL);
            vTaskDelay(pdMS_TO_TICKS(command));
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
            watering = false;
        } else if(command == COMMAND_PUMP_START){
            ESP_LOGI(TAG, "Pump started");
            watering = true;
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_ON_LEVEL);
        } else if(command == COMMAND_PUMP_STOP){
            ESP_LOGI(TAG, "Pump stopped");
            gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
            watering = false;
        } else {
            ESP_LOGI(TAG, "Received invalid command: %i", command);
        }
    }
}

esp_err_t start_pump()
{
    int32_t command = COMMAND_PUMP_START;
    return xQueueSendToBack(command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t start_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken)
{
    int32_t command = COMMAND_PUMP_START;
    return xQueueSendToBackFromISR(command_queue, &command, pxHigherPriorityTaskWoken) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t stop_pump()
{
    int32_t command = COMMAND_PUMP_STOP;
    return xQueueOverwrite(command_queue, &command) == pdTRUE ? ESP_OK : ESP_FAIL;
}


esp_err_t stop_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken)
{
    int32_t command = COMMAND_PUMP_STOP;
    return xQueueOverwriteFromISR(command_queue, &command, pxHigherPriorityTaskWoken) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t run_pump(uint16_t time_ms)
{

    int32_t command = (int32_t)time_ms;
    return xQueueSendToBack(command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t run_pump_fromISR(uint16_t time_ms, BaseType_t* pxHigherPriorityTaskWoken)
{
    int32_t command = (int32_t)time_ms;
    return xQueueSendToBackFromISR(command_queue, &command, pxHigherPriorityTaskWoken) == pdTRUE ? ESP_OK : ESP_FAIL;
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
    return analog_to_humidity_pct(watering_trigger);
}

float get_rearm_humidity_pct()
{
    return analog_to_humidity_pct(rearm_trigger);
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

uint16_t humidity_pct_to_analog(uint16_t humidity)
{
    return (humidity * (1/ANALOGUE_MOISTURE_CONSTANT)) + ANALOGUE_MOISTURE_OFFSET;
}

float analog_to_humidity_pct(uint16_t val)
{
    return ANALOGUE_MOISTURE_CONSTANT * (val - ANALOGUE_MOISTURE_OFFSET);
}

esp_err_t save_config(uint16_t time_ms, uint16_t watering_trigger_val, uint16_t rearm_trigger_val)
{
    pump_on_time = time_ms;
    watering_trigger = watering_trigger_val;
    rearm_trigger = rearm_trigger_val;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
     if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return ESP_FAIL;
    }

    uint8_t fail = 0;
    err = nvs_set_u16(handle, "pot", time_ms);
    if(err) {
        ESP_LOGE(TAG, "Could not save pump_on_time to NVS: (%s)", esp_err_to_name(err)); 
        fail++;
    }
    err = nvs_set_u16(handle, "wt", watering_trigger_val);
    if(err) {
        ESP_LOGE(TAG, "Could not save watering_trigge to NVS: (%s)", esp_err_to_name(err)); 
        fail++;
    }
    err = nvs_set_u16(handle, "rt", rearm_trigger_val);
    if(err) {
        ESP_LOGE(TAG, "Could not save rearm_trigger to NVS: (%s)", esp_err_to_name(err)); 
        fail++;
    }

    if(fail == 0){
        err = nvs_commit(handle);
        if(err) {
            ESP_LOGE(TAG, "Could not save to NVS: (%s)", esp_err_to_name(err));
            fail++;
        }
    }
    

    if(!fail) ESP_LOGI(TAG, "Accepted and saved new settings: pump_on_time: %i, watering_trigger_val: %i, rearm_trigger_val: %i", 
        pump_on_time, watering_trigger, rearm_trigger);
    nvs_close(handle);
    return fail == 0 ? ESP_OK : ESP_FAIL;
}

void init_watering_control()
{
    gpio_reset_pin(PUMP_CONTROL_PIN);
    gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
    gpio_set_direction(PUMP_CONTROL_PIN, GPIO_MODE_OUTPUT);

    command_queue = xQueueCreate(1,sizeof(int32_t));
    watering = false;
    pump_on_time = CONFIG_DEFAULT_PUMP_ON_TIME;
    watering_trigger = CONFIG_DEFAULT_WATERING_TRIGGER;
    rearm_trigger = CONFIG_DEFAULT_REARM_TRIGGER;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    } else{
        uint16_t temp;
        err = nvs_get_u16(handle, "pot", &temp);
        if(!err) pump_on_time = temp;
        err = nvs_get_u16(handle, "wt", &temp);
        if(!err) watering_trigger = temp;
        err = nvs_get_u16(handle, "rt", &temp);
        if(!err) rearm_trigger = temp;

        ESP_LOGI(TAG, "Initialized with water: %i, trigger: %i, rearm: %i", 
        pump_on_time, watering_trigger, rearm_trigger);

        nvs_close(handle);
    }

}

void start_watering_control(TaskHandle_t* watering_task_handle_out)
{
    xTaskCreate(pump_control_task, "Pump Control Task", 2024, NULL, 5, NULL);   // Pump control should be highest priority
    xTaskCreate(watering_task, "Watering Task", 2024, NULL, 4, watering_task_handle_out);
}