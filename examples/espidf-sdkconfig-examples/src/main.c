/*
 * SDKCONFIG_DEFAULTS layering integration test
 *
 * Verifies that SDKCONFIG_DEFAULTS layering works correctly when passing
 * multiple sdkconfig files via board_build.cmake_extra_args.
 *
 * sdkconfig.base:     CONFIG_LOG_DEFAULT_LEVEL=2  (WARN)
 * sdkconfig.override: CONFIG_LOG_DEFAULT_LEVEL=3  (INFO)
 *
 * The _Static_assert below causes a compile error if the override did not
 * take effect — making this a self-verifying integration test.
 */
#include "sdkconfig.h"
#include "esp_log.h"

_Static_assert(CONFIG_LOG_DEFAULT_LEVEL == 3,
    "SDKCONFIG_DEFAULTS layering failed: expected LOG_DEFAULT_LEVEL=3 (INFO) "
    "from sdkconfig.override, got a different value");

static const char *TAG = "sdkconfig_test";

void app_main(void) {
    ESP_LOGI(TAG, "SDKCONFIG_DEFAULTS layering verified: LOG_DEFAULT_LEVEL=%d",
             CONFIG_LOG_DEFAULT_LEVEL);
}
