#include "driver/gpio.h"
#include "esp_log.h"
#include "home_mesh.h"
#include "relay_logic.h"

#define RELAY_PIN GPIO_NUM_4

static const char *TAG = "switch";
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
static esp_ble_mesh_gen_onoff_srv_t relay_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};
static esp_ble_mesh_client_t sensor_client;
ESP_BLE_MESH_MODEL_PUB_DEFINE(relay_pub, 2 + 3, ROLE_NODE);
ESP_BLE_MESH_MODEL_PUB_DEFINE(client_pub, 2 + 2, ROLE_NODE);
static esp_ble_mesh_model_t models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&relay_pub, &relay_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&client_pub, &sensor_client),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health, &health_pub),
};
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, models, ESP_BLE_MESH_MODEL_NONE),
};
static esp_ble_mesh_comp_t composition = {
    .cid = HOME_CID, .element_count = 1, .elements = elements,
};

static void relay_set(bool on)
{
    const int level = relay_output_level(on);
    relay_server.state.onoff = on;
    gpio_set_level(RELAY_PIN, level);
    ESP_LOGI(TAG, "Relay logical=%s; GPIO4=%d", on ? "ON" : "OFF", level);
}

static void server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                      esp_ble_mesh_generic_server_cb_param_t *p)
{
    if (event == ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT &&
        (p->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
         p->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK)) {
        relay_set(p->value.state_change.onoff_set.onoff != 0);
    }
}

static void client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                      esp_ble_mesh_generic_client_cb_param_t *p)
{
    if (event == ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT && p->params &&
        p->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS && p->error_code == 0) {
        const uint8_t state = p->status_cb.onoff_status.present_onoff;
        if (state <= 1) {
            relay_set(state != 0);
            ESP_LOGI(TAG, "Sensor notification: %u", state);
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(RELAY_PIN));
    ESP_ERROR_CHECK(gpio_set_level(RELAY_PIN, relay_output_level(false)));
    ESP_ERROR_CHECK(gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT));
    relay_set(false);
    ESP_ERROR_CHECK(home_mesh_start(&prov, &composition, "SMART_SWITCH", server_cb, client_cb));
}
