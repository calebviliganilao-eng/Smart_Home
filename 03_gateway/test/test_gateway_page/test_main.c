#include "unity.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "../../src/web_ui.h"

void setUp(void) {}
void tearDown(void) {}

void test_gateway_shows_only_living_room(void) {
    TEST_ASSERT_NOT_NULL(strstr(GATEWAY_UI, "Living Room"));
    TEST_ASSERT_NULL(strstr(GATEWAY_UI, "Kitchen"));
    TEST_ASSERT_NULL(strstr(GATEWAY_UI, "Bedroom"));
    TEST_ASSERT_NULL(strstr(GATEWAY_UI, "Garage"));
}

void test_gateway_ui_has_smart_light(void) {
    TEST_ASSERT_NOT_NULL(strstr(GATEWAY_UI, "Smart light"));
}

void test_gateway_ui_has_status_endpoint(void) {
    TEST_ASSERT_NOT_NULL(strstr(GATEWAY_UI, "/api/status"));
}

void test_gateway_ui_contains_on_off_buttons(void) {
    TEST_ASSERT_NOT_NULL(strstr(GATEWAY_UI, "Turn ON"));
    TEST_ASSERT_NOT_NULL(strstr(GATEWAY_UI, "Turn OFF"));
}

static int run_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gateway_shows_only_living_room);
    RUN_TEST(test_gateway_ui_has_smart_light);
    RUN_TEST(test_gateway_ui_has_status_endpoint);
    RUN_TEST(test_gateway_ui_contains_on_off_buttons);
    return UNITY_END();
}

#ifdef ESP_PLATFORM
void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    run_tests();
}
#else
int main(void) {
    return run_tests();
}
#endif
