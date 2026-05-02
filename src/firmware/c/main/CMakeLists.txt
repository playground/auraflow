idf_component_register(
    SRCS
        "main.c"
        "modbus.c"
        "tuf2000m.c"
        "ring_buffer.c"
        "nvs_config.c"
        "nvs_config_esp.c"
        "provisioning.c"
        "provisioning_esp.c"
        "wifi_mgr.c"
        "wifi_mgr_esp.c"
        "uplink.c"
        "uplink_esp.c"
        "http_config_esp.c"
        "captive_portal_esp.c"
        "ota_esp.c"
        "cJSON.c"        # vendored — ESP-IDF v6.0 dropped the bundled `json` component
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_driver_uart  # UART driver for the provisioning listener (v6.0+ split)
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_http_client  # POST readings to HomeHub
        esp_http_server  # on-device status page at GET /
        esp_https_ota    # OTA fetch + apply via POST /ota
        app_update       # esp_ota_get_running_partition for OTA logging
)
# Common components (freertos, log, esp_system, etc.) are auto-included by
# ESP-IDF on top of REQUIRES.
