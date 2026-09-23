#ifndef WATERING_H
#define WATERING_H
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

//  Initializes and starts watering_task and pump_control_task
void init_watering();

//  Decides when to water the plant
void watering_task(void* vParameters);

//  Controls the pump that waters the plant
void pump_control_task(void* vParameters);

//  interacts with pump_control_task
//  only run_pump or both the others may be used at the same time
//  if a command to the pump is currently queued, start_pump and run_pump return ESP_FAIL otherwise ESP_OK
esp_err_t start_pump();
esp_err_t start_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken);

esp_err_t stop_pump();
esp_err_t stop_pump_fromISR(BaseType_t* pxHigherPriorityTaskWoken);

esp_err_t run_pump(uint16_t time_ms);

#endif