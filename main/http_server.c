#include "http_server.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include "mdns.h"
#include "measurements.h"
#include "watering.h"

#define MDNS_HOST_NAME CONFIG_MDNS_HOST_NAME

#define WATER_TIME_CONSTANT CONFIG_WATER_TIME_CONSTANT
#define ANALOGUE_MOISTURE_CONSTANT CONFIG_ANALOGUE_MOISTURE_CONSTANT

static const char* TAG = "HTTP SERVER";

// HTML web pages to serve 
extern const char html_page[] asm("_binary_index_html_start"); 

extern const char config_page[] asm("_binary_config_html_start"); 

// API handlers
static esp_err_t api_get_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "humidity_pct", current_moisture_pct());
    cJSON_AddBoolToObject(j, "watering", is_watering());
    char *s = cJSON_Print(j);

    esp_err_t ret = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(j);
    free(s);
    return ret;
}

static esp_err_t api_post_water_handler(httpd_req_t *req)
{
    if(true){
        httpd_resp_set_status(req, "202 Accepted");
        run_pump(get_pump_on_time());
    } else {
        httpd_resp_set_status(req, "409 Conflict");
    }

    return httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_get_config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "water_amount_ml", get_pump_amount());
    cJSON_AddNumberToObject(j, "trigger_humidity_pct", (int)get_trigger_humidity_pct());
    cJSON_AddNumberToObject(j, "rearm_humidity_pct", (int)get_rearm_humidity_pct());
    char *s = cJSON_Print(j);

    esp_err_t ret = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(j);
    free(s);
    return ret;
}

static esp_err_t api_post_config_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    char* buffer = malloc(len);    //  check if too big maybe
    httpd_req_recv(req, buffer, len);

    bool req_ok = true;
    cJSON* j = cJSON_Parse(buffer);
    cJSON* jwater_amount_ml = cJSON_GetObjectItemCaseSensitive(j, "water_amount_ml");
    if(!cJSON_IsNumber(jwater_amount_ml)){
        req_ok = false;
    }

    cJSON* jtrigger_humidity_pct = cJSON_GetObjectItemCaseSensitive(j, "trigger_humidity_pct");
    if(!cJSON_IsNumber(jtrigger_humidity_pct)){
        req_ok = false;
    }

    cJSON* jrearm_humidity_pct = cJSON_GetObjectItemCaseSensitive(j, "rearm_humidity_pct");
    if(!cJSON_IsNumber(jrearm_humidity_pct)){
        req_ok = false;
    }

    int water_amount_ml = jwater_amount_ml->valueint;
    float trigger_humidity_pct = jtrigger_humidity_pct->valuedouble;
    float rearm_humidity_pct = jrearm_humidity_pct->valuedouble;

    if(req_ok){
        httpd_resp_set_status(req, "204 No Content");
        save_config(water_amount_ml * (1/WATER_TIME_CONSTANT), 
            trigger_humidity_pct * (1/ANALOGUE_MOISTURE_CONSTANT),
            rearm_humidity_pct * (1/ANALOGUE_MOISTURE_CONSTANT));
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    esp_err_t ret = httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(j);
    free(buffer);
    return ret;
}

// HTTP GET page handlers
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, config_page, HTTPD_RESP_USE_STRLEN);
}

static httpd_handle_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
        
        httpd_uri_t uri_root_get = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_root_get);

        httpd_uri_t uri_config_get = {
            .uri       = "/config",
            .method    = HTTP_GET,
            .handler   = config_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_config_get);

        httpd_uri_t uri_api_status_get = {
            .uri       = "/api/status",
            .method    = HTTP_GET,
            .handler   = api_get_status_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_api_status_get);

        httpd_uri_t uri_api_water_post = {
            .uri       = "/api/water",
            .method    = HTTP_POST,
            .handler   = api_post_water_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_api_water_post);

        httpd_uri_t uri_api_config_get = {
            .uri       = "/api/config",
            .method    = HTTP_GET,
            .handler   = api_get_config_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_api_config_get);

        httpd_uri_t uri_api_config_post = {
            .uri       = "/api/config",
            .method    = HTTP_POST,
            .handler   = api_post_config_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_api_config_post);
        
        return server;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started. Connecting to %s...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected. Retrying connection...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Web Server ready! Access at http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

void init_mdns()
{
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    //set hostname
    mdns_hostname_set(MDNS_HOST_NAME);
    //set default instance
    mdns_instance_name_set("Smart Watering Web Interface");
}

void start_http_server()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    #ifdef CONFIG_MDNS_ENABLE
        init_mdns();
    #endif

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server = start_web_server();
    if (server) {
        ESP_LOGI(TAG, "Web Server initialized. Waiting for Wi-Fi connection...");
    }
}