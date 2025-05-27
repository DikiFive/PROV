/* Wi-Fi 配网管理器示例
 *
 * 此示例代码属于公共领域（或根据您的选择，采用 CC0 许可）。
 *
 * 除非法律要求或书面同意，本软件按"原样"分发，不附带任何明示或暗示的保证或条件。
 */

#include <stdio.h>  // 标准输入输出库，用于printf等函数
#include <string.h> // 字符串操作库，用于strlen, strdup等函数

#include <freertos/FreeRTOS.h>     // FreeRTOS 操作系统核心头文件
#include <freertos/task.h>         // FreeRTOS 任务管理头文件，用于vTaskDelay等
#include <freertos/event_groups.h> // FreeRTOS 事件组头文件，用于事件同步

#include <esp_system.h> // ESP-IDF 系统头文件，用于esp_get_free_heap_size等
#include <esp_log.h>    // ESP-IDF 日志系统头文件，用于ESP_LOGI, ESP_LOGE等
#include <esp_wifi.h>   // ESP-IDF Wi-Fi 驱动头文件，用于Wi-Fi相关操作
#include <esp_event.h>  // ESP-IDF 事件循环头文件，用于事件注册和处理
#include <nvs_flash.h>  // ESP-IDF NVS（非易失性存储）闪存头文件，用于存储Wi-Fi凭据等
#include <esp_netif.h>  // ESP-IDF 网络接口头文件，用于网络接口初始化

#include <wifi_provisioning/manager.h>    // Wi-Fi 配网管理器头文件，提供配网管理功能
#include <wifi_provisioning/scheme_ble.h> // BLE 配网方案头文件，实现基于BLE的配网

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

// 在此事件组上发出 Wi-Fi 事件信号
const int WIFI_CONNECTED_EVENT = BIT0;      // Wi-Fi 连接成功事件位，当设备成功连接到AP并获取IP时设置
static EventGroupHandle_t wifi_event_group; // Wi-Fi 事件组句柄，用于在不同任务间同步Wi-Fi连接状态

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
            break;
        }
        case WIFI_PROV_CRED_FAIL:
        {
            // 配网凭据验证失败
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "配网失败!\n\t原因: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi 认证失败" : "未找到 Wi-Fi 接入点");
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            // 配网凭据验证成功
            ESP_LOGI(TAG, "配网成功");
            break;
        case WIFI_PROV_END:
            // 配网过程结束
            vTaskDelay(pdMS_TO_TICKS(500)); // 增加延迟以确保蓝牙资源释放，避免资源冲突
            wifi_prov_mgr_deinit();         // 去初始化配网管理器，释放相关资源
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
 * @brief 初始化Wi-Fi为STA模式
 * 设置Wi-Fi模式为站点(STA)模式并启动Wi-Fi。
 */
static void wifi_init_sta(void)
{
    // 设置Wi-Fi模式为STA（站点）模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 启动Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief 获取设备的BLE服务名称
 * 根据设备的MAC地址生成一个唯一的服务名称，用于BLE配网。
 *
 * @param service_name 用于存储生成的服务名称的缓冲区
 * @param max 缓冲区service_name的最大长度
 */
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];                // 用于存储MAC地址的数组
    const char *ssid_prefix = "PROV_"; // 服务名称前缀
    // 获取STA接口的MAC地址
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    // 格式化生成服务名称，使用MAC地址的后三字节作为后缀
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

/**
 * @brief 自定义配网数据处理程序
 * 这是一个回调函数，用于处理通过配网通道接收到的自定义数据。
 *
 * @param session_id 当前会话ID
 * @param inbuf 接收到的输入数据缓冲区
 * @param inlen 输入数据长度
 * @param outbuf 用于返回输出数据的缓冲区指针
 * @param outlen 输出数据长度指针
 * @param priv_data 私有数据，此处为NULL
 * @return esp_err_t ESP_OK表示成功，其他值表示错误
 */
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

/**
 * @brief 应用程序主函数
 * 初始化系统，配置Wi-Fi配网，并进入主循环。
 */
void app_main(void)
{
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
    // 创建Wi-Fi事件组，用于同步Wi-Fi连接状态
    wifi_event_group = xEventGroupCreate();

    // 注册事件处理程序
    // 注册Wi-Fi配网事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    // 注册Wi-Fi事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    // 注册IP事件处理程序，特别是获取到IP地址的事件
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // 初始化Wi-Fi
    esp_netif_create_default_wifi_sta();                 // 创建默认的Wi-Fi STA网络接口
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // 获取默认的Wi-Fi初始化配置
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                // 初始化Wi-Fi驱动

    // 配置配网管理器
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,                                      // 使用BLE作为配网方案
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM // BLE方案的事件处理程序
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config)); // 初始化Wi-Fi配网管理器

    bool provisioned = false;
    // 检查设备是否已经配网
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    // if (!provisioned) // 如果设备未配网，则启动配网过程
    //{
    ESP_LOGI(TAG, "开始配网"); // 打印日志，表示开始配网

    char service_name[12];                                       // 用于存储BLE服务名称的缓冲区
    get_device_service_name(service_name, sizeof(service_name)); // 获取设备的BLE服务名称
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;        // 设置配网安全级别为1（PoP）
    const char *pop = "abcd1234";                                // 设置Proof-of-Possession (PoP) 字符串

    // 定义自定义服务UUID，用于BLE配网
    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c,
        0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03,
        0x04, 0x90, 0x1a, 0x02};
    // 设置BLE配网方案的自定义服务UUID
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    // 创建一个名为"custom-data"的自定义端点，用于接收额外数据
    wifi_prov_mgr_endpoint_create("custom-data");
    // 启动配网过程，指定安全级别、PoP、服务名称和自定义数据
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, NULL));
    // 注册自定义数据处理程序到"custom-data"端点
    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
    //}
    // else // 如果设备已配网，则直接启动Wi-Fi连接
    // {
    //     ESP_LOGI(TAG, "已配网，启动 Wi-Fi");
    //     wifi_prov_mgr_deinit(); // 去初始化配网管理器
    //     wifi_init_sta();        // 初始化Wi-Fi为STA模式并连接
    // }

    // 等待Wi-Fi连接成功事件，直到WIFI_CONNECTED_EVENT位被设置
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true, portMAX_DELAY);

    // 主应用程序循环
    while (1)
    {
        ESP_LOGI(TAG, "系统运行正常");   // 打印系统正常运行日志
        vTaskDelay(pdMS_TO_TICKS(1000)); // 延迟1秒
    }
}
