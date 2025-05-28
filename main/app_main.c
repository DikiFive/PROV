#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include "prov.h"

static const char *TAG = "app";                     // 用于日志输出的标签，方便识别日志来源
static const char *RAM_MONITOR_TAG = "RAM_MONITOR"; // 用于RAM监控的日志标签

void print_memory_usage(const char *stage)
{
    // 打印当前阶段
    ESP_LOGI(RAM_MONITOR_TAG, "Stage: %s", stage);

    // 打印当前可用堆大小
    uint32_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(RAM_MONITOR_TAG, "Free heap size: %d bytes", free_heap);

    // 打印当前任务栈的水位标记
    UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(RAM_MONITOR_TAG, "Task stack high water mark: %d bytes", stack_high_water_mark * sizeof(StackType_t));
}

/**
 * @brief 系统事件处理程序
 * 捕获并处理来自Wi-Fi配网、Wi-Fi和IP事件的通知。
 *
 * @param arg 用户自定义数据，此处为NULL
 * @param event_base 事件的基础类型（如WIFI_PROV_EVENT, WIFI_EVENT, IP_EVENT）
 * @param event_id 事件的具体ID
 * @param event_data 事件相关的数据
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    // 处理Wi-Fi配网事件
    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id)
        {
        case WIFI_PROV_START:
            // 配网过程开始
            ESP_LOGI(TAG, "配网开始");
            break;
        case WIFI_PROV_CRED_RECV:
        {
            // 接收到Wi-Fi凭据（SSID和密码）
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "接收到 Wi-Fi 凭据:\n\tSSID: %s\n\t密码: %s",
                     (const char *)wifi_sta_cfg->ssid,
                     (const char *)wifi_sta_cfg->password);
            print_memory_usage("Wi-Fi Credentials Received");
            break;
        }
        case WIFI_PROV_CRED_FAIL:
        {
            // 配网凭据验证失败
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "配网失败!\n\t原因: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi 认证失败" : "未找到 Wi-Fi 接入点");
            print_memory_usage("Wi-Fi Connection Failed");
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            // 配网凭据验证成功
            ESP_LOGI(TAG, "配网成功");
            print_memory_usage("Wi-Fi Connected Successfully");
            break;
        case WIFI_PROV_END:
            // 配网过程结束
            vTaskDelay(pdMS_TO_TICKS(500));               // 增加延迟以确保蓝牙资源释放，避免资源冲突
            wifi_prov_mgr_deinit();                       // 去初始化配网管理器，释放相关资源
            print_memory_usage("After Provisioning End"); // 监控配网结束后的内存使用情况
            break;
        default:
            break;
        }
    }
    // 处理Wi-Fi事件
    else if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            // Wi-Fi STA模式启动，尝试连接到AP
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            // Wi-Fi STA模式断开连接，尝试重新连接
            ESP_LOGI(TAG, "已断开连接. 重新连接到AP...");
            esp_wifi_connect();
        }
    }
    // 处理IP事件，特别是获取到IP地址的事件
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        // 设备成功获取到IP地址
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "已连接, IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        // 设置事件组中的WIFI_CONNECTED_EVENT位，通知其他任务Wi-Fi已连接
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
}

/**
 * @brief 应用程序主函数
 * 初始化系统，配置Wi-Fi配网，并进入主循环。
 */
void app_main(void)
{
    // 初始化前的 RAM 使用量
    print_memory_usage("Before Initialization");

    // 初始化NVS（非易失性存储），用于存储Wi-Fi凭据等配置信息
    esp_err_t ret = nvs_flash_init();
    // 检查NVS初始化是否成功，如果NVS分区没有空闲页或版本不匹配，则擦除并重新初始化
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase()); // 擦除NVS分区
        ESP_ERROR_CHECK(nvs_flash_init());  // 重新初始化NVS
    }

    // 初始化网络协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认事件循环，用于处理系统事件
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 注册事件处理程序
    // 注册Wi-Fi配网事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    // 注册Wi-Fi事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    // 注册IP事件处理程序，特别是获取到IP地址的事件
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // 初始化Wi-Fi和配网
    esp_netif_create_default_wifi_sta();                 // 创建默认的Wi-Fi STA网络接口
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // 获取默认的Wi-Fi初始化配置
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                // 初始化Wi-Fi驱动

    // 初始化配网模块
    init_provisioning();

    print_memory_usage("After Initialization");

    // 等待Wi-Fi连接成功事件，直到WIFI_CONNECTED_EVENT位被设置
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

    // 主应用程序循环
    while (1)
    {
        ESP_LOGI(TAG, "系统运行正常");   // 打印系统正常运行日志
        vTaskDelay(pdMS_TO_TICKS(1000)); // 延迟1秒
    }
}
