#ifndef WATERING_H
#define WATERING_H
#include <inttypes.h>

//  Initializes and starts watering_task and pump_control_task
void init_watering();

//  Decides when to water the plant
void watering_task(void* vParameters);

//  Controls the pump that waters the plant
void pump_control_task(void* vParameters);

//  interacts with pump_control_task
//  only run_pump or both the others may be used at the same time
void start_pump();
void stop_pump();

void run_pump(uint16_t time_ms);

#endif