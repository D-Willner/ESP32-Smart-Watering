#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/mcpwm_timer.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/adc_channel.h"
#include "sdkconfig.h"
#include "measurements.h"

#define ADC_ID ADC_UNIT_1
#define ADC_PIN CONFIG_ADC1_GPIO_PIN
#define ADC_CHANNEL CONFIG_ADC1_GPIO_CHANNEL
#define BUTTON_PIN CONFIG_WATER_BUTTON_GPIO_PIN

bool btn_press_flag;
QueueHandle_t queue;

void init_queue()
{
    queue = xQueueCreate(1,sizeof(int));
}

void button_ISR(void*)
{
    btn_press_flag = true;
}

void init_button()
{
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_ISR, NULL);
}

void adc_read_task(void* vParameters)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t adc_unit_init_cfg = {
        .unit_id = ADC_ID,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
        .clk_src = 0
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_unit_init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t adc_oneshot_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &adc_oneshot_cfg));
    char* TAG = "ADC TEST";

    while(1){
        int measurement;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &measurement));
        ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_2, ADC_CHANNEL, measurement);
        xQueueOverwrite(queue, &measurement);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void init_measurements()
{
    init_button();
    init_queue();
    xTaskCreate(adc_read_task, "ADC read task", 2024, NULL, 0, NULL);
}