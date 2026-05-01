/**
 * AuraFlow ESP32 firmware — entry point.
 *
 * Phase 1 of the C port: Hello-World skeleton. This exists to verify the
 * toolchain end-to-end (build, flash, run) before porting any real logic.
 * Once `idf.py flash monitor` shows the periodic "tick" log line, the C
 * build pipeline is proven and module ports can begin.
 *
 * The TypeScript source under ../../ts/ is the executable spec — every C
 * module is a port of behavior already validated by the Node-runnable
 * tests there.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "auraflow";

void app_main(void)
{
    ESP_LOGI(TAG, "AuraFlow firmware starting (skeleton build, no sensor logic yet)");

    int counter = 0;
    while (1) {
        ESP_LOGI(TAG, "tick %d — toolchain alive, awaiting module ports", counter++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
