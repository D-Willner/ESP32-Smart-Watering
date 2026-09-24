#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H
#include <inttypes.h>

void init_measurements();
void start_measurements();

void adc_read_task(void* vParameters);

uint16_t current_moisture();

float current_moisture_pct();

#endif // MEASUREMENTS_H