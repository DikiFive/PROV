#ifndef __PROV_H__
#define __PROV_H__

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

// Wi-Fi连接事件位定义
#define WIFI_CONNECTED_EVENT BIT0

// 全局事件组声明
extern EventGroupHandle_t wifi_event_group;

// 函数声明
void get_device_service_name(char *service_name, size_t max);
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data);
void init_provisioning(void);

#endif // __PROV_H__
