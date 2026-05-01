idf_component_register(
    SRCS
        "main.c"
        "modbus.c"
        "tuf2000m.c"
        "ring_buffer.c"
        "nvs_config.c"
        "nvs_config_esp.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
)
# Common components (freertos, log, esp_system, etc.) are auto-included by
# ESP-IDF on top of REQUIRES.
