#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_mesh.h"
#include "sensor_logic.h"

#define SENSOR_DO GPIO_NUM_27
#define SAMPLE_MS 20
#define STABLE_SAMPLES 4
#define LOG_INTERVAL_MS 1000
#define RETRY_MS 4000

static const char *TAG = "sensor";
static uint8_t device_uuid[16] = {0xdd, 0xdd};
static esp_ble_mesh_prov_t prov = {.uuid = device_uuid};
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl = 7,
};
static const uint8_t health_test_ids[] = {0x00};
static esp_ble_mesh_health_srv_t health = {
    .health_test = {.id_count = 1, .test_ids = health_test_ids, .company_id = HOME_CID},
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(health_pub, 2 + 11, ROLE_NODE);
static esp_ble_mesh_gen_onoff_srv_t sensor_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_pub, 2 + 1, ROLE_NODE);
static esp_ble_mesh_model_t models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&sensor_pub, &sensor_server),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health, &health_pub),
};
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, models, ESP_BLE_MESH_MODEL_NONE),
};
static esp_ble_mesh_comp_t composition = {
    .cid = HOME_CID, .element_count = 1, .elements = elements,
};

static bool publish_state(uint8_t onoff)
{
    if (!home_mesh_is_provisioned() ||
        sensor_pub.publish_addr == ESP_BLE_MESH_ADDR_UNASSIGNED ||
        sensor_pub.app_idx == ESP_BLE_MESH_KEY_UNUSED) {
        ESP_LOGW(TAG, "Publish pending. Check provisioning, model AppKey and publication address.");
        return false;
    }
    esp_err_t result = esp_ble_mesh_model_publish(&models[1],
        ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS, sizeof(onoff), &onoff, ROLE_NODE);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Publish failed: %s", esp_err_to_name(result));
        return false;
    }
    ESP_LOGI(TAG, "Published %s (OnOff=%u) to 0x%04X",
             onoff ? "DARK" : "BRIGHT", onoff, sensor_pub.publish_addr);
    return true;
}

void app_main(void)
{
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << SENSOR_DO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    ESP_ERROR_CHECK(home_mesh_start(&prov, &composition, "SMART_SENSOR", NULL, NULL));

    int previous_raw = -1;
    int stable_state = -1;
    unsigned samples = 0;
    bool pending = false;
    TickType_t log_at = xTaskGetTickCount() - pdMS_TO_TICKS(LOG_INTERVAL_MS);
    TickType_t attempted_at = xTaskGetTickCount() - pdMS_TO_TICKS(RETRY_MS);

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        const int raw = gpio_get_level(SENSOR_DO);
        const uint8_t onoff = sensor_onoff_from_do(raw);
        if (now - log_at >= pdMS_TO_TICKS(LOG_INTERVAL_MS)) {
            ESP_LOGI(TAG, "LIVE GPIO27 DO=%d => %s", raw, onoff ? "DARK" : "BRIGHT");
            log_at = now;
        }
        if (raw != previous_raw) {
            previous_raw = raw;
            samples = 1;
        } else if (samples < STABLE_SAMPLES) {
            ++samples;
        }
        if (samples == STABLE_SAMPLES && stable_state != onoff) {
            stable_state = onoff;
            sensor_server.state.onoff = onoff;
            pending = true;
            attempted_at = now - pdMS_TO_TICKS(RETRY_MS);
        }
        if (pending && now - attempted_at >= pdMS_TO_TICKS(RETRY_MS)) {
            attempted_at = now;
            pending = !publish_state((uint8_t)stable_state);
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}
