/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "mqtt.h"

static const char *TAG = "smartconfig";
static EventGroupHandle_t s_wifi_event_group;
char status_topic[] = "messages/6c9c1971-6d20-41b3-ab49-fcb5f262e2e6/status";
char update_topic[] = "messages/6c9c1971-6d20-41b3-ab49-fcb5f262e2e6/update";
#define BUTTON_PIN GPIO_NUM_0  // Example button pin, change to match your setup


#define LED_PIN 2

TaskHandle_t heart;//heart dung de dinh chi hoac tiep tuc chay task send_heart
TaskHandle_t Led_TASK;//heart dung de dinh chi hoac tiep tuc chay task led_task (task 1)

bool old_button_state = false;
bool button_state = true;
gpio_config_t io_conf;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const int SMART_CONFIG_BIT = BIT3;
static const int Led_TASK_BIT = BIT2;
uint8_t response = 0;//bien response dung de check xem co ban tin {“code”: 1} gui ve khong
static esp_mqtt_client_handle_t client;
int led_state_1 = 0;
void gpio_ouput_init(gpio_config_t *io_conf, uint8_t gpio_num){
    io_conf->mode = GPIO_MODE_OUTPUT;
    io_conf->pin_bit_mask = 1 << gpio_num;
    io_conf->pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf->pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf->intr_type = GPIO_INTR_DISABLE;
    gpio_config(io_conf);
}

esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{   client = event->client;
    char *data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            //khi co ket noi mqtt thi moi tien hanh chay 2 task led_task va task send_heart
            vTaskResume(heart);
            vTaskResume(Led_TASK);
            esp_mqtt_client_subscribe(client,status_topic, 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            //khi mat ket noi mqtt thi tien hanh dinh chi 2 task nay de tranh lam treo chuong trinh
            vTaskSuspend(heart);
            vTaskSuspend(Led_TASK);
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("Hello word\n");
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            int gpio_led = 2;
            uint32_t led_state = 1;
            data = event->data;
            //check xem data co phai la theo dinh dang {“code”: 1} khong.
            if(strstr(data,"code")){
                response = 1;
            }
            else{ 
                //kiem tra data co moi {“led”: {gpio_pin}, “status”: “{status_led}”} khong
                if(strstr(data, "on")){
                    led_state = 1;
                }
                else if(strstr(data,"off")){
                    led_state = 0;
                }
                sscanf(data, "{\"led\": %d", &gpio_led);
                //khoi tao chan gpio cho led
                gpio_ouput_init(&io_conf,gpio_led);
                gpio_set_level((uint8_t)gpio_led,led_state);
                //tien hanh bat tat led nhu yeu cau
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data)
{   
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG,"WiFi disconnect");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}
void button_task(void* parameter) {
    int button_press_count = 0;
    TickType_t last_button_press_time = 0;

    while (1) {
        int button_state = gpio_get_level(BUTTON_PIN);

        if (button_state != old_button_state && button_state == 0) {
            TickType_t current_time = xTaskGetTickCount();

            if (current_time - last_button_press_time > pdMS_TO_TICKS(1000)) {
                // More than 1 second has passed since the last button press
                button_press_count = 1;
            } else {
                button_press_count++;
                if (button_press_count == 2) {
                    printf("2 times\n");
                    led_state_1 = !led_state_1;
                    xEventGroupSetBits(s_wifi_event_group, Led_TASK_BIT);
                } 
                else if (button_press_count == 4) {
                    xEventGroupSetBits(s_wifi_event_group, SMART_CONFIG_BIT);
                    printf("4 times\n");
                    button_press_count = 0;
                }
            }

            last_button_press_time = current_time;
        }
        old_button_state = button_state;

        vTaskDelay(100 / portTICK_PERIOD_MS);  // Adjust the delay as needed
    }
}
void led_task(void* parameter) {
    while (1) {
        xEventGroupWaitBits(s_wifi_event_group, Led_TASK_BIT, true, false, portMAX_DELAY);
            // kiem tra co ket noi toi internet chua, sau do thi moi publish du lieu
        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
        if (bits & ESPTOUCH_DONE_BIT) {
                gpio_set_level(LED_PIN, led_state_1);  // Set LED state based on the received value
                char message[1024];
                int led_pin = 2;
                sprintf(message, "{\"led\": %d,\"status\": \"%d\"}", led_pin, led_state_1);
                int msg_id = esp_mqtt_client_publish(client, update_topic, message, 0, 0, 1);
                ESP_LOGI(TAG, "Sent publish successful, msg_id=%d", msg_id);
                ESP_LOGI(TAG,"Data:%d",led_state_1);
        }
    }
}

int time_out = 3000;
void send_heart(void* parameter) {
    char message[] = "{\"heartbeat\": 1}";
    while (1) {
        esp_mqtt_client_publish(client,update_topic, message, 0, 0, 1);
        vTaskDelay(time_out/ portTICK_PERIOD_MS);
        if(response) {
            vTaskDelay(60000 - time_out/ portTICK_PERIOD_MS);  
        }
        else {
            vTaskDelay(20000 - time_out/ portTICK_PERIOD_MS);
        }
        response = 0;
    }
}
/*
Ham smart_config dung duoc goi khi bam 4 lan nut lan va se vao qua trinh smartconfig
*/
void smart_config(void* parameter) {
    while (1) {
        xEventGroupWaitBits(s_wifi_event_group, SMART_CONFIG_BIT, true, false, portMAX_DELAY);
            EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
            //Kiem tra neu co truoc noi tu truoc do thi se ngat ket noi mqtt
            if (bits & ESPTOUCH_DONE_BIT) {
                esp_mqtt_client_stop(client);
                xEventGroupClearBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
            }
            //ngat ket noi wifi, ngat smartconfig neu gia su chuong trinh truoc do da ket noi wifi hoac dang chay smart config
            esp_wifi_stop();
            esp_smartconfig_stop();
            ESP_ERROR_CHECK( esp_wifi_start() );
            ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
            smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
            ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
            //Doi ket noi thanh cong bang smart config sau do se tien hanh ket noi mqtt
            xEventGroupWaitBits(s_wifi_event_group, ESPTOUCH_DONE_BIT, false, false, portMAX_DELAY);
            esp_smartconfig_stop();
            mqtt_app_start();
    }
}
//Ham init_wifi() khoi tao cac event loop cho wifi, khoi tao cau hinh cho wifi
void init_wifi(){
    ESP_ERROR_CHECK(esp_netif_init());
    //tao event_group
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
}

void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    //Khoi tao thong tin wifi ban dau
    init_wifi();
    gpio_ouput_init(&io_conf,LED_PIN);
    //task button_task la task xu ly nut bam
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
    //task led_task la task xu ly task2 trong de bai
    xTaskCreate(led_task, "led_task", 4096, NULL, 10, &Led_TASK);
    //task smart_config dung de xu ly smart config trong task 2
    xTaskCreate(smart_config, "smart_config", 2048, NULL, 10, NULL);
    //task send_heart dung de xu ly task3 trong de bai
    xTaskCreate(send_heart, "send_heart", 2048, NULL, 11, &heart);
    //tam thoi dinh chi task send_heart cho toi khi co ket noi mqtt thanh cong
    vTaskSuspend(heart);
    //set ham call back xu ly mqtt event
    set_mqtt_callback_event(mqtt_event_handler_cb);
}

