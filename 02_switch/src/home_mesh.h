#pragma once

#include <stdbool.h>
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_err.h"

#define HOME_CID 0x02E5

esp_err_t home_mesh_start(esp_ble_mesh_prov_t *prov, esp_ble_mesh_comp_t *composition,
                          const char *name, esp_ble_mesh_generic_server_cb_t server_cb,
                          esp_ble_mesh_generic_client_cb_t client_cb);
bool home_mesh_is_provisioned(void);
uint16_t home_mesh_net_idx(void);
