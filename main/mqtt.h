#ifndef MQTT_H
#define MQTT_H
#include "driver/gpio.h"
#include "mqtt_client.h"

typedef void (*mqtt_callback_event_t)(esp_mqtt_event_handle_t event);
void mqtt_app_start(void);
void set_mqtt_callback_event(void *cb);
#endif
