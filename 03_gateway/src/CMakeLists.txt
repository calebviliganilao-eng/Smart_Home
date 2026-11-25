idf_component_register(SRCS "main.c" "home_mesh.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash driver esp_wifi esp_event esp_netif esp_http_server)
