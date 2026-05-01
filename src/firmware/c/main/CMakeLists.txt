idf_component_register(
    SRCS
        "main.c"
        "modbus.c"
        "tuf2000m.c"
    INCLUDE_DIRS "."
)
# Common components (freertos, log, esp_system, etc.) are auto-included by
# ESP-IDF when REQUIRES is omitted — keep this minimal until we add deps
# that aren't in the common set (e.g. esp_wifi, esp_http_client, nvs_flash).
