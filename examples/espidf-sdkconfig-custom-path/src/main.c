/*
 * Custom sdkconfig path integration test
 *
 * Verifies that board_build.esp-idf.sdkconfig_path correctly uses a
 * custom-named sdkconfig file instead of the default "sdkconfig.<env>".
 *
 * sdkconfig.custom: CONFIG_LOG_DEFAULT_LEVEL=4 (DEBUG)
 *
 * The _Static_assert below causes a compile error if the custom sdkconfig
 * was not applied — making this a self-verifying integration test.
 */
#include "sdkconfig.h"
#include "esp_log.h"

_Static_assert(CONFIG_LOG_DEFAULT_LEVEL == 4,
    "Custom sdkconfig_path failed: expected LOG_DEFAULT_LEVEL=4 (DEBUG) "
    "from sdkconfig.custom, got a different value");

static const char *TAG = "sdkconfig_test";

void app_main(void) {
    ESP_LOGI(TAG, "Custom sdkconfig_path verified: LOG_DEFAULT_LEVEL=%d",
             CONFIG_LOG_DEFAULT_LEVEL);
}
