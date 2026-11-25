#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_mesh.h"
#include "nvs.h"
#include "web_ui.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

#define ACK_TIMEOUT_MS 3000
#define COMMAND_DEADLINE_MS (ACK_TIMEOUT_MS + 1000)
#define RELAY_POLL_MS 2000
#define RELAY_READ_TIMEOUT_MS 600
#define RELAY_STATE_MAX_AGE_MS (3 * RELAY_POLL_MS)

static const char *TAG = "gateway";
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
static esp_ble_mesh_client_t onoff_client;
ESP_BLE_MESH_MODEL_PUB_DEFINE(client_pub, 2 + 3, ROLE_NODE);
static esp_ble_mesh_model_t models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&client_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health, &health_pub),
};
static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, models, ESP_BLE_MESH_MODEL_NONE),
};
static esp_ble_mesh_comp_t composition = {
    .cid = HOME_CID, .element_count = 1, .elements = elements,
};

typedef enum { IDLE, QUEUED, PENDING, ACK, TIMEOUT, FAILED } command_phase_t;
static struct {
    command_phase_t phase;
    TickType_t started_at;
    uint16_t destination;
    uint16_t key;
    bool on;
} last_command;
static struct {
    TickType_t started_at;
    uint16_t destination;
    bool pending;
} relay_read;
static struct {
    TickType_t updated_at;
    uint16_t source;
    int state;
} relay_status = {.state = -1};
static portMUX_TYPE command_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t switch_address;
static uint8_t transaction_id;
static httpd_handle_t http_server;

static uint16_t bound_key(void)
{
    for (size_t i = 0; i < CONFIG_BLE_MESH_MODEL_KEY_COUNT; ++i) {
        if (models[1].keys[i] != ESP_BLE_MESH_KEY_UNUSED) {
            return models[1].keys[i];
        }
    }
    return ESP_BLE_MESH_KEY_UNUSED;
}

static uint16_t gateway_address(void)
{
    return home_mesh_is_provisioned() ? esp_ble_mesh_get_primary_element_address() : 0;
}

static bool is_unicast(uint16_t address)
{
    return address > 0 && address <= 0x7FFF;
}

typedef enum {
    CONTROL_READY, NEEDS_PROVISIONING, NEEDS_APPKEY, NEEDS_TARGET, TARGET_IS_GATEWAY
} control_status_t;

static control_status_t control_status(uint16_t own, uint16_t key)
{
    if (!own || !onoff_client.model) {
        return NEEDS_PROVISIONING;
    }
    if (!is_unicast(switch_address)) {
        return NEEDS_TARGET;
    }
    if (own == switch_address) {
        return TARGET_IS_GATEWAY;
    }
    return key == ESP_BLE_MESH_KEY_UNUSED ? NEEDS_APPKEY : CONTROL_READY;
}

static const char *const control_messages[] = {
    [CONTROL_READY] = "",
    [NEEDS_PROVISIONING] = "In nRF Mesh, provision SMART_GATEWAY into the same network as SMART_SWITCH.",
    [NEEDS_APPKEY] = "In nRF Mesh, bind the shared AppKey to SMART_GATEWAY’s Generic OnOff Client (0x1001).",
    [NEEDS_TARGET] = "Open Gateway settings, enter the SMART_SWITCH address, then tap Save.",
    [TARGET_IS_GATEWAY] = "The saved address is the gateway’s own. Save the SMART_SWITCH address.",
};

static bool command_busy(command_phase_t phase)
{
    return phase == QUEUED || phase == PENDING;
}

static command_phase_t current_phase(TickType_t now)
{
    if (command_busy(last_command.phase) &&
        now - last_command.started_at >= pdMS_TO_TICKS(COMMAND_DEADLINE_MS)) {
        last_command.phase = TIMEOUT;
    }
    return last_command.phase;
}

static bool parse_address(const char *text, uint16_t *address)
{
    unsigned value = 0;
    size_t digits = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }
    for (; *text; ++text) {
        const char *hex = "0123456789ABCDEFabcdef";
        const char *match = strchr(hex, *text);
        if (!match || ++digits > 4) {
            return false;
        }
        unsigned digit = (unsigned)(match - hex);
        value = (value << 4) | (digit >= 16 ? digit - 6 : digit);
    }
    if (!is_unicast((uint16_t)value) || value == gateway_address()) {
        return false;
    }
    *address = (uint16_t)value;
    return true;
}

static esp_ble_mesh_client_common_param_t onoff_params(uint32_t opcode, uint16_t address,
                                                        uint16_t key, int timeout_ms)
{
    return (esp_ble_mesh_client_common_param_t){
        .opcode = opcode,
        .model = onoff_client.model,
        .ctx = {.net_idx = home_mesh_net_idx(), .app_idx = key, .addr = address, .send_ttl = 3},
        .msg_timeout = timeout_ms,
        .msg_role = ROLE_NODE,
    };
}

static esp_err_t send_queued_command(void)
{
    const TickType_t now = xTaskGetTickCount();
    portENTER_CRITICAL(&command_lock);
    // ESP-IDF allows only one acknowledged request per destination on this model.
    if (current_phase(now) != QUEUED || relay_read.pending) {
        portEXIT_CRITICAL(&command_lock);
        return ESP_OK;
    }
    last_command.phase = PENDING;
    const uint16_t address = last_command.destination;
    const uint16_t key = last_command.key;
    const bool on = last_command.on;
    const uint8_t tid = transaction_id++;
    portEXIT_CRITICAL(&command_lock);

    esp_ble_mesh_client_common_param_t params =
        onoff_params(ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, address, key, ACK_TIMEOUT_MS);
    esp_ble_mesh_generic_client_set_state_t command = {
        .onoff_set = {.onoff = on, .tid = tid, .op_en = true},
    };
    const esp_err_t result = esp_ble_mesh_generic_client_set_state(&params, &command);
    if (result != ESP_OK) {
        portENTER_CRITICAL(&command_lock);
        last_command.phase = FAILED;
        portEXIT_CRITICAL(&command_lock);
        ESP_LOGE(TAG, "Mesh send failed: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "Requested %s from switch 0x%04X", on ? "on" : "off", address);
    }
    return result;
}

static void read_relay_state(uint16_t address, uint16_t key)
{
    esp_ble_mesh_client_common_param_t params =
        onoff_params(ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, address, key, RELAY_READ_TIMEOUT_MS);
    const esp_err_t result = esp_ble_mesh_generic_client_get_state(&params, NULL);
    if (result != ESP_OK) {
        portENTER_CRITICAL(&command_lock);
        relay_read.pending = false;
        portEXIT_CRITICAL(&command_lock);
        ESP_LOGW(TAG, "Mesh status read failed: %s", esp_err_to_name(result));
    }
}

static void mesh_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                           esp_ble_mesh_generic_client_cb_param_t *p)
{
    if (!p->params) {
        return;
    }
    const bool is_read = p->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET;
    if (!is_read && p->params->opcode != ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
        return;
    }
    command_phase_t phase;
    int state = -1;
    if (event == (is_read ? ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT
                          : ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT)) {
        if (p->error_code == 0 && p->status_cb.onoff_status.present_onoff <= 1) {
            state = p->status_cb.onoff_status.present_onoff;
        }
        phase = state >= 0 ? ACK : FAILED;
    } else if (event == ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT) {
        phase = TIMEOUT;
    } else {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    const uint16_t address = p->params->ctx.addr;
    TickType_t elapsed = 0;
    portENTER_CRITICAL(&command_lock);
    const bool match = is_read
        ? relay_read.pending && relay_read.destination == address
        : last_command.phase == PENDING && last_command.destination == address;
    if (match) {
        if (state >= 0) {
            relay_status.state = state;
            relay_status.source = address;
            relay_status.updated_at = now;
        }
        if (is_read) {
            relay_read.pending = false;
        } else {
            if (phase == ACK && state != last_command.on) {
                phase = FAILED;
            }
            last_command.phase = phase;
            elapsed = now - last_command.started_at;
        }
    }
    portEXIT_CRITICAL(&command_lock);
    if (!match) {
        return;
    }
    if (is_read) {
        send_queued_command();
    } else {
        ESP_LOGI(TAG, "Switch 0x%04X response: phase=%d, state=%d, elapsed=%lu ms",
                 address, phase, state, (unsigned long)pdTICKS_TO_MS(elapsed));
    }
}

static void load_target(void)
{
    nvs_handle_t storage;
    if (nvs_open("webctrl", NVS_READONLY, &storage) == ESP_OK) {
        uint16_t value = 0;
        if (nvs_get_u16(storage, "switch", &value) == ESP_OK && is_unicast(value)) {
            switch_address = value;
        }
        nvs_close(storage);
    }
}

static esp_err_t save_target(uint16_t value)
{
    if (value == switch_address) {
        return ESP_OK;
    }
    nvs_handle_t storage;
    esp_err_t result = nvs_open("webctrl", NVS_READWRITE, &storage);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_u16(storage, "switch", value);
    if (result == ESP_OK) {
        result = nvs_commit(storage);
    }
    nvs_close(storage);
    if (result == ESP_OK) {
        switch_address = value;
        portENTER_CRITICAL(&command_lock);
        relay_status.source = 0;
        last_command.phase = IDLE;
        portEXIT_CRITICAL(&command_lock);
    }
    return result;
}

static esp_err_t json_reply(httpd_req_t *req, const char *status, const char *json)
{
    if (status) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static bool read_form(httpd_req_t *req, char *body, size_t capacity)
{
    size_t size = req->content_len;
    if (size == 0 || size >= capacity) {
        return false;
    }
    size_t used = 0;
    while (used < size) {
        int count = httpd_req_recv(req, body + used, size - used);
        if (count <= 0) {
            return false;
        }
        used += (size_t)count;
    }
    body[used] = 0;
    return true;
}

static esp_err_t show_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, GATEWAY_UI);
}

static esp_err_t show_status(httpd_req_t *req)
{
    static const char *const names[] = {"idle", "pending", "pending", "ack", "timeout", "failed"};
    const TickType_t now = xTaskGetTickCount();
    const uint16_t own = gateway_address();
    const uint16_t key = own ? bound_key() : ESP_BLE_MESH_KEY_UNUSED;
    const control_status_t readiness = control_status(own, key);

    portENTER_CRITICAL(&command_lock);
    command_phase_t phase = current_phase(now);
    const int state = readiness == CONTROL_READY && relay_status.source == switch_address &&
                      now - relay_status.updated_at < pdMS_TO_TICKS(RELAY_STATE_MAX_AGE_MS)
        ? relay_status.state : -1;
    const bool start_read = readiness == CONTROL_READY && !command_busy(phase) &&
        !relay_read.pending && (relay_read.destination != switch_address ||
                                 now - relay_read.started_at >= pdMS_TO_TICKS(RELAY_POLL_MS));
    if (start_read) {
        relay_read.started_at = now;
        relay_read.destination = switch_address;
        relay_read.pending = true;
    }
    const bool reading = relay_read.pending && relay_read.destination == switch_address;
    portEXIT_CRITICAL(&command_lock);
    if (start_read) {
        read_relay_state(switch_address, key);
    }
    char json[384];
    snprintf(json, sizeof(json),
             "{\"gateway\":\"%04X\",\"target\":\"%04X\",\"bound\":%s,"
             "\"phase\":\"%s\",\"relay\":%d,\"reading\":%s,\"blocked\":\"%s\"}",
             own, switch_address, key != ESP_BLE_MESH_KEY_UNUSED ? "true" : "false",
             names[phase], state, reading ? "true" : "false", control_messages[readiness]);
    return json_reply(req, NULL, json);
}

static esp_err_t update_target(httpd_req_t *req)
{
    char body[64], address[12];
    uint16_t value;
    if (!read_form(req, body, sizeof(body)) ||
        httpd_query_key_value(body, "addr", address, sizeof(address)) != ESP_OK ||
        !parse_address(address, &value)) {
        return json_reply(req, "400 Bad Request",
                          "{\"message\":\"Enter SMART_SWITCH’s distinct unicast address.\"}");
    }
    const TickType_t now = xTaskGetTickCount();
    portENTER_CRITICAL(&command_lock);
    bool waiting = command_busy(current_phase(now));
    portEXIT_CRITICAL(&command_lock);
    if (waiting) {
        return json_reply(req, "409 Conflict", "{\"message\":\"Wait for the current command.\"}");
    }
    if (save_target(value) != ESP_OK) {
        return json_reply(req, "500 Internal Server Error", "{\"message\":\"Save failed.\"}");
    }
    ESP_LOGI(TAG, "Switch target saved: 0x%04X", value);
    return json_reply(req, NULL, "{\"message\":\"Switch address saved.\"}");
}

static esp_err_t send_command(httpd_req_t *req)
{
    char body[32], state[8];
    if (!read_form(req, body, sizeof(body)) ||
        httpd_query_key_value(body, "state", state, sizeof(state)) != ESP_OK ||
        (strcmp(state, "on") && strcmp(state, "off"))) {
        return json_reply(req, "400 Bad Request", "{\"message\":\"Select ON or OFF.\"}");
    }
    const uint16_t own = gateway_address();
    const uint16_t key = own ? bound_key() : ESP_BLE_MESH_KEY_UNUSED;
    const control_status_t readiness = control_status(own, key);
    if (readiness != CONTROL_READY) {
        char json[192];
        snprintf(json, sizeof(json), "{\"message\":\"%s\"}", control_messages[readiness]);
        return json_reply(req, "409 Conflict", json);
    }
    const TickType_t now = xTaskGetTickCount();
    portENTER_CRITICAL(&command_lock);
    bool waiting = command_busy(current_phase(now));
    if (!waiting) {
        last_command.phase = QUEUED;
        last_command.started_at = now;
        last_command.destination = switch_address;
        last_command.key = key;
        last_command.on = strcmp(state, "on") == 0;
    }
    portEXIT_CRITICAL(&command_lock);
    if (waiting) {
        return json_reply(req, "409 Conflict", "{\"message\":\"Wait for the current command.\"}");
    }

    if (send_queued_command() != ESP_OK) {
        return json_reply(req, "503 Service Unavailable", "{\"message\":\"Mesh send failed.\"}");
    }
    return json_reply(req, NULL, "{\"message\":\"Command queued.\"}");
}

static void start_http(void)
{
    if (http_server) {
        return;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&http_server, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(result));
        return;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = show_page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = show_status},
        {.uri = "/api/target", .method = HTTP_POST, .handler = update_target},
        {.uri = "/api/switch", .method = HTTP_POST, .handler = send_command},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &routes[i]));
    }
    ESP_LOGI(TAG, "Gateway control page ready");
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && (event == WIFI_EVENT_STA_START ||
                               event == WIFI_EVENT_STA_DISCONNECTED)) {
        if (event == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t *info = data;
            ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u). Reconnecting.", info->reason);
        }
        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi connect: %s", esp_err_to_name(result));
        }
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *info = data;
        ESP_LOGI(TAG, "Open http://" IPSTR "/", IP2STR(&info->ip_info.ip));
        start_http();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(home_mesh_start(&prov, &composition, "SMART_GATEWAY", NULL, mesh_client_cb));
    load_target();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGE(TAG, "Wi-Fi network interface creation failed");
        return;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t config = {0};
#if defined(WIFI_SSID) && defined(WIFI_PASS)
    _Static_assert(sizeof(WIFI_SSID) - 1 <= sizeof(config.sta.ssid), "Wi-Fi SSID too long");
    _Static_assert(sizeof(WIFI_PASS) - 1 <= sizeof(config.sta.password), "Wi-Fi password too long");
    memcpy(config.sta.ssid, WIFI_SSID, sizeof(WIFI_SSID) - 1);
    memcpy(config.sta.password, WIFI_PASS, sizeof(WIFI_PASS) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
#else
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &config));
    if (!config.sta.ssid[0]) {
        ESP_LOGW(TAG, "Add src/wifi_secrets.h from the example to enable Wi-Fi.");
        return;
    }
#endif
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}
