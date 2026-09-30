#include <stdio.h>
#include <stdatomic.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/mcpwm_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "driver/ledc.h"
#include "sdkconfig.h"

#include "watering.h"
#include "measurements.h"

#define PUMP_CONTROL_PIN CONFIG_PUMP_CONTROL_PIN

#ifdef CONFIG_USE_PWM
#define PWM_PIN 25 // TODO: Set to 
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_DUTY CONFIG_PWM_DUTY
#define PWM_RESOLUTION LEDC_TIMER_13_BIT
#endif

//#define CONFIG_PWM_FADE

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

#define ANALOGUE_MOISTURE_INVERTED

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

#ifndef ANALOGUE_MOISTURE_INVERTED
        if(armed && moisture <= watering_trigger){
            ESP_LOGI(TAG, "Starting pump because humidity is %i", moisture);
            run_pump(pump_on_time);
            armed = false;
        } else if(!armed && moisture > rearm_trigger){
            ESP_LOGI(TAG, "Rearming pump because humidity is %i", moisture);
            armed = true;
        }
#else
        if(armed && moisture >= watering_trigger){
            ESP_LOGI(TAG, "Starting pump because humidity is %i", moisture);
            run_pump(pump_on_time);
            armed = false;
        } else if(!armed && moisture < rearm_trigger){
            ESP_LOGI(TAG, "Rearming pump because humidity is %i", moisture);
            armed = true;
        }
#endif
    }
}


static void motor_start()
{
#ifdef CONFIG_USE_PWM

#ifndef CONFIG_PUMP_CONTROL_INVERT
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, (PWM_DUTY*(pow(2,PWM_RESOLUTION)) /100 ));
#else
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, ((100 - PWM_DUTY)*pow(2,PWM_RESOLUTION)) /100 );
#endif
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);

#else
    gpio_set_level(PUMP_CONTROL_PIN, PUMP_ON_LEVEL);
#endif
}

static void motor_stop()
{
#ifdef CONFIG_USE_PWM

#ifndef CONFIG_PUMP_CONTROL_INVERT
    ledc_stop(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, 0);
#else
    ledc_stop(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, 1);
#endif

#else
    gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
#endif
}

static void pump_control_task(void* vParameters)
{
    int32_t command;
    while(1){
        BaseType_t ret = xQueueReceive(command_queue, &command, portMAX_DELAY);
        if(ret == errQUEUE_EMPTY) continue; // should not happen

        if(command == 0) continue;
        else if(command > 0){
            ESP_LOGI(TAG, "Pump will be run for %i ms", command);
            watering = true;
            motor_start();
            xQueueReceive(command_queue, &command, pdMS_TO_TICKS(command));
            motor_stop();
            watering = false;
        } else if(command == COMMAND_PUMP_START){
            ESP_LOGI(TAG, "Pump started");
            watering = true;
            motor_start();
        } else if(command == COMMAND_PUMP_STOP){
            ESP_LOGI(TAG, "Pump stopped");
            motor_stop();
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

uint16_t humidity_pct_to_analog(float humidity)
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

#ifdef CONFIG_USE_PWM
void init_pwm()
{
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 4096,
        .duty_resolution = PWM_RESOLUTION,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    
    ledc_channel_config_t channel_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PWM_CHANNEL,
        .gpio_num = PWM_PIN,
        .timer_sel = LEDC_AUTO_CLK,
        .duty = 0, 
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    ledc_stop(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, 0);
}
#endif

void init_watering_control()
{
#ifdef CONFIG_USE_PWM
    init_pwm();
#else
    gpio_reset_pin(PUMP_CONTROL_PIN);
    gpio_set_level(PUMP_CONTROL_PIN, PUMP_OFF_LEVEL);
    gpio_set_direction(PUMP_CONTROL_PIN, GPIO_MODE_OUTPUT);
#endif

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