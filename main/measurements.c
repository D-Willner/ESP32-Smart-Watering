#include <stdio.h>
#include "driver/gpio.h"
#include "driver/mcpwm_timer.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/adc_channel.h"
#include "sdkconfig.h"
#include "measurements.h"
#include "watering.h"

#define ADC_PIN CONFIG_ADC1_GPIO_PIN
#define ADC_CHANNEL CONFIG_ADC1_GPIO_CHANNEL
#define BUTTON_PIN CONFIG_WATER_BUTTON_GPIO_PIN

#define ADC_POLLING_INTERVALL CONFIG_ADC_POLLING_INTERVALL
#define ANALOGUE_MOISTURE_CONSTANT CONFIG_ANALOGUE_MOISTURE_CONSTANT

#define BUTTON_DEBOUNCE_TICKS pdMS_TO_TICKS(50)

static const char* TAG = "MEASUREMENT";

QueueHandle_t moisture_queue;

#ifdef CONFIG_ENABLE_WATERING_BUTTON
static TaskHandle_t button_task_handle;
static int button_stable_level;

static void button_ISR(void*)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &higher_priority_task_woken);
    if(higher_priority_task_woken == pdTRUE) portYIELD_FROM_ISR();
}

static void button_task(void* pvParameters)
{
    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while(ulTaskNotifyTake(pdTRUE, BUTTON_DEBOUNCE_TICKS) != 0){
            // Restart the quiet period after every raw edge.
        }

        int level = gpio_get_level(BUTTON_PIN);
        if(level == button_stable_level) continue;

        button_stable_level = level;
        ESP_LOGI(TAG, "Button settled %s", level ? "HIGH" : "LOW");

        #ifdef CONFIG_WATERING_BUTTON_MODE_WHILE_PRESSED
        if(level == 0){ // falling edge
            #ifndef CONFIG_WATERING_BUTTON_INVERT
                start_pump();
            #else
                stop_pump();
            #endif
        } else { // rising edge
            #ifndef CONFIG_WATERING_BUTTON_INVERT
                stop_pump();
            #else
                start_pump();
            #endif
        }
        #elifdef CONFIG_WATERING_BUTTON_MODE_SINGLE
        if(level == 0){ // falling edge
            #ifndef CONFIG_WATERING_BUTTON_INVERT
                run_pump(get_pump_on_time());
            #endif
        } else { // rising edge
            #ifdef CONFIG_WATERING_BUTTON_INVERT
                run_pump(get_pump_on_time());
            #endif
        }
        #endif
    }
}
#endif

static void adc_read_task(void* pvParameters)
{
    TaskHandle_t watering_task_handle = (TaskHandle_t)pvParameters;
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t adc_unit_init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
        .clk_src = 0
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_unit_init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t adc_oneshot_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &adc_oneshot_cfg));

    while(1){
        int measurement;
        if(adc_oneshot_read(adc_handle, ADC_CHANNEL, &measurement) == ESP_OK){
            uint16_t adc_measurement = (uint16_t)measurement;
            //ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_2, ADC_CHANNEL, measurement);
            xQueueOverwrite(moisture_queue, &adc_measurement);
            xTaskNotify(watering_task_handle, 0, eNoAction);
        } else{
            ESP_LOGE(TAG, "Could not read from ADC");
        }

        vTaskDelay(pdMS_TO_TICKS(ADC_POLLING_INTERVALL));
    }
}

uint16_t current_moisture()
{
    uint16_t current;
    BaseType_t ret = xQueuePeek(moisture_queue,&current,0);

    if(ret != pdPASS) return UINT16_MAX;
    else return current;
}

float current_moisture_pct()
{
    return analog_to_humidity_pct(current_moisture());
}


void init_measurements()
{
#ifdef CONFIG_ENABLE_WATERING_BUTTON
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
#endif

    moisture_queue = xQueueCreate(1,sizeof(uint16_t));
    uint16_t val = UINT16_MAX;
    xQueueOverwrite(moisture_queue,&val);
}

void start_measurements(TaskHandle_t watering_task_handle)
{
#ifdef CONFIG_ENABLE_WATERING_BUTTON
    button_stable_level = gpio_get_level(BUTTON_PIN);
    ESP_ERROR_CHECK(xTaskCreate(button_task, "Button task", 2048, NULL, 3,
                                &button_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_PIN, button_ISR, NULL));
    if(gpio_get_level(BUTTON_PIN) != button_stable_level){
        xTaskNotifyGive(button_task_handle);
    }
#endif

    xTaskCreate(adc_read_task, "ADC read task", 2024, watering_task_handle, 0, NULL);
}
