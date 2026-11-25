#include <stdatomic.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "home_mesh.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
// GPIO19 is USB D- on the ESP32-S3.
#define STATUS_LED_PIN GPIO_NUM_18
#else
#define STATUS_LED_PIN GPIO_NUM_19
#endif

static const char *TAG = "home_mesh";
static const char *device_name;
static atomic_bool initialized;
static atomic_uint_least16_t net_idx = ESP_BLE_MESH_KEY_PRIMARY;

bool home_mesh_is_provisioned(void)
{
    return initialized && esp_ble_mesh_node_is_provisioned();
}

uint16_t home_mesh_net_idx(void)
{
    return net_idx;
}

static void start_advertising(void)
{
    esp_err_t result = esp_ble_mesh_node_prov_enable(
        ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Provisioning request failed: %s", esp_err_to_name(result));
    }
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *p)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "BLE Mesh init complete (err %d)", p->prov_register_comp.err_code);
        if (p->prov_register_comp.err_code != 0) {
            break;
        }
        initialized = true;
        if (esp_ble_mesh_node_is_provisioned()) {
            ESP_LOGI(TAG, "%s CURRENT PRIMARY UNICAST ADDRESS: 0x%04X", device_name,
                     esp_ble_mesh_get_primary_element_address());
        } else {
            start_advertising();
        }
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioning advertising result: %d", p->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        net_idx = p->node_prov_complete.net_idx;
        ESP_LOGI(TAG, "%s CURRENT PRIMARY UNICAST ADDRESS: 0x%04X", device_name,
                 p->node_prov_complete.addr);
        gpio_set_level(STATUS_LED_PIN, 0);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        gpio_set_level(STATUS_LED_PIN, 1);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        gpio_set_level(STATUS_LED_PIN, 0);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        start_advertising();
        break;
    default:
        break;
    }
}

static void health_cb(esp_ble_mesh_health_server_cb_event_t event,
                      esp_ble_mesh_health_server_cb_param_t *param)
{
    (void)param;
    if (event == ESP_BLE_MESH_HEALTH_SERVER_ATTENTION_ON_EVT) {
        gpio_set_level(STATUS_LED_PIN, 1);
    } else if (event == ESP_BLE_MESH_HEALTH_SERVER_ATTENTION_OFF_EVT) {
        gpio_set_level(STATUS_LED_PIN, 0);
    }
}

esp_err_t home_mesh_start(esp_ble_mesh_prov_t *prov, esp_ble_mesh_comp_t *composition,
                          const char *name, esp_ble_mesh_generic_server_cb_t server_cb,
                          esp_ble_mesh_generic_client_cb_t client_cb)
{
    device_name = name;
    ESP_ERROR_CHECK(gpio_reset_pin(STATUS_LED_PIN));
    ESP_ERROR_CHECK(gpio_set_direction(STATUS_LED_PIN, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(STATUS_LED_PIN, 0));
    // Preserve the saved mesh database if NVS cannot open.
    ESP_ERROR_CHECK(nvs_flash_init());
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BT));
    uint8_t *uuid = (uint8_t *)prov->uuid;
    memset(uuid, 0, 16);
    uuid[0] = 0xDD;
    uuid[1] = 0xDD;
    memcpy(uuid + 2, mac, sizeof(mac));
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(provisioning_cb));
    ESP_ERROR_CHECK(esp_ble_mesh_register_health_server_callback(health_cb));
    if (server_cb) {
        ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(server_cb));
    }
#if CONFIG_BLE_MESH_GENERIC_ONOFF_CLI
    if (client_cb) {
        ESP_ERROR_CHECK(esp_ble_mesh_register_generic_client_callback(client_cb));
    }
#else
    (void)client_cb;
#endif
    ESP_ERROR_CHECK(esp_ble_mesh_init(prov, composition));
    return esp_ble_mesh_set_unprovisioned_device_name(name);
}
