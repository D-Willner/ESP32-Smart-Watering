#ifndef WATERING_H
#define WATERING_H
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

//  Initialize and start watering_task and pump_control_task
void init_watering_control(void);
void start_watering_control(TaskHandle_t* watering_task_handle_out);

//  interacts with pump_control_task
//  only run_pump or both the others may be used at the same time
//  if a command to the pump is currently queued, start_pump and run_pump return ESP_FAIL otherwise ESP_OK
esp_err_t start_pump();
esp_err_t start_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken);

esp_err_t stop_pump();
esp_err_t stop_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken);

esp_err_t run_pump(uint16_t time_ms);
esp_err_t run_pump_fromISR(uint16_t time_ms, BaseType_t* pxHigherPriorityTaskWoken);

//  safe read write access (currently thread safety is guaranteed by atomic types)
uint16_t get_pump_on_time();

uint16_t get_pump_amount();

float get_trigger_humidity_pct();

float get_rearm_humidity_pct();

bool is_watering();

void set_pump_on_time(uint16_t time_ms);

void set_trigger_humidity(uint16_t val);

void set_rearm_humidity(uint16_t val);

esp_err_t save_config(uint16_t time_ms, uint16_t watering_trigger_val, uint16_t rearm_trigger_val);

uint16_t humidity_pct_to_analog(uint16_t humidity);
float analog_to_humidity_pct(uint16_t val);

#endif