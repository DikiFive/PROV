#include "prov.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include <string.h>

static const char *TAG = "prov"; // 日志标签

// 全局事件组实例化
EventGroupHandle_t wifi_event_group;

void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];                // 用于存储MAC地址的数组
    const char *ssid_prefix = "PROV_"; // 服务名称前缀
    // 获取STA接口的MAC地址
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    // 格式化生成服务名称，使用MAC地址的后三字节作为后缀
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    // 如果接收到数据，则打印出来
    if (inbuf)
    {
        ESP_LOGI(TAG, "收到数据: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS"; // 定义响应字符串
    // 复制响应字符串到输出缓冲区
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL)
    {
        ESP_LOGE(TAG, "内存不足");
        return ESP_ERR_NO_MEM; // 内存分配失败
    }
    *outlen = strlen(response) + 1; // 设置输出数据长度（包括null终止符）
    return ESP_OK;                  // 成功返回
}

void init_provisioning(void)
{
    // 创建Wi-Fi事件组
    wifi_event_group = xEventGroupCreate();

    // 配置配网管理器
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,                                      // 使用BLE作为配网方案
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM // BLE方案的事件处理程序
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config)); // 初始化Wi-Fi配网管理器

    // 定义自定义服务UUID，用于BLE配网
    static uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c,
        0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03,
        0x04, 0x90, 0x1a, 0x02};

    char service_name[12];                                       // 用于存储BLE服务名称的缓冲区
    get_device_service_name(service_name, sizeof(service_name)); // 获取设备的BLE服务名称
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;        // 设置配网安全级别为1（PoP）
    const char *pop = "abcd1234";                                // 设置Proof-of-Possession (PoP) 字符串

    // 设置BLE配网方案的自定义服务UUID
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    // 创建一个名为"custom-data"的自定义端点，用于接收额外数据
    wifi_prov_mgr_endpoint_create("custom-data");
    // 启动配网过程，指定安全级别、PoP、服务名称
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, NULL));
    // 注册自定义数据处理程序到"custom-data"端点
    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
}
