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
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        driver           # UART driver for the provisioning listener
        json             # cJSON for parsing PROVISION:{...} payloads
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_http_client  # POST readings to HomeHub
)
# Common components (freertos, log, esp_system, etc.) are auto-included by
# ESP-IDF on top of REQUIRES.
