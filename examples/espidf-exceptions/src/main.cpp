#include "esp_log.h"
#define tag "ExceptionsTest"

extern "C" void app_main(void)
{
	ESP_LOGI(tag, "Before try");
	try {
		ESP_LOGI(tag, "Before throw");
		throw false;
		ESP_LOGI(tag, "After throw");
	} catch ( bool val ){
		ESP_LOGI(tag, "catch");
	}
	ESP_LOGI(tag, "After try");
}