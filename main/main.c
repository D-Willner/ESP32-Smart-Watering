#include "http_server.h"
#include "measurements.h"
#include "watering.h"

void app_main(void)
{
    init_measurements();
    start_http_server();
}
