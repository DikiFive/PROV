/* Wi-Fi 配网管理器示例
 *
 * 此示例代码属于公共领域（或根据您的选择，采用 CC0 许可）。
 *
 * 除非法律要求或书面同意，本软件按"原样"分发，不附带任何明示或暗示的保证或条件。
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>     // FreeRTOS 操作系统头文件
#include <freertos/task.h>         // FreeRTOS 任务管理头文件
#include <freertos/event_groups.h> // FreeRTOS 事件组头文件

#include <esp_log.h>   // ESP-IDF 日志系统头文件
#include <esp_wifi.h>  // ESP-IDF Wi-Fi 驱动头文件
#include <esp_event.h> // ESP-IDF 事件循环头文件
#include <nvs_flash.h> // ESP-IDF NVS（非易失性存储）闪存头文件
#include <esp_netif.h> // ESP-IDF 网络接口头文件

#include <wifi_provisioning/manager.h>    // Wi-Fi 配网管理器头文件
#include <wifi_provisioning/scheme_ble.h> // BLE 配网方案头文件

static const char *TAG = "app"; // 用于日志输出的标签

// 在此事件组上发出 Wi-Fi 事件信号
const int WIFI_CONNECTED_EVENT = BIT0;      // Wi-Fi 连接成功事件位
static EventGroupHandle_t wifi_event_group; // Wi-Fi 事件组句柄

// 用于捕获系统事件的事件处理程序
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id)
        {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "配网开始");
            break;
        case WIFI_PROV_CRED_RECV:
        {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "接收到 Wi-Fi 凭据:\n\tSSID: %s\n\t密码: %s",
                     (const char *)wifi_sta_cfg->ssid,
                     (const char *)wifi_sta_cfg->password);
            break;
        }
        case WIFI_PROV_CRED_FAIL:
        {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "配网失败!\n\t原因: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi 认证失败" : "未找到 Wi-Fi 接入点");
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "配网成功");
            break;
        case WIFI_PROV_END:
            vTaskDelay(pdMS_TO_TICKS(500)); // 增加延迟以确保蓝牙资源释放
            wifi_prov_mgr_deinit();
            break;
        default:
            break;
        }
    }
    else if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            ESP_LOGI(TAG, "已断开连接. 重新连接到AP...");
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "已连接, IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf)
    {
        ESP_LOGI(TAG, "收到数据: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL)
    {
        ESP_LOGE(TAG, "内存不足");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;
    return ESP_OK;
}

void app_main(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 初始化网络协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // 初始化 Wi-Fi
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 配置配网管理器
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    // if (!provisioned)
    //{
    ESP_LOGI(TAG, "开始配网");

    char service_name[12];
    get_device_service_name(service_name, sizeof(service_name));
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = "abcd1234";

    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c,
        0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03,
        0x04, 0x90, 0x1a, 0x02};
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    wifi_prov_mgr_endpoint_create("custom-data");
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, NULL));
    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
    //}
    // else
    // {
    //     ESP_LOGI(TAG, "已配网，启动 Wi-Fi");
    //     wifi_prov_mgr_deinit();
    //     wifi_init_sta();
    // }

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

    while (1)
    {
        ESP_LOGI(TAG, "系统运行正常");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
