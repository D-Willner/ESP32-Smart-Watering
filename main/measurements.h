#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern QueueHandle_t moisture_queue;

void init_measurements();
void start_measurements(TaskHandle_t watering_task_handle);

void adc_read_task(void* vParameters);

uint16_t current_moisture();

float current_moisture_pct();

#endif // MEASUREMENTS_H