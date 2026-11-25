#include "unity.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "../../src/sensor_logic.h"

void setUp(void) {}
void tearDown(void) {}

void test_flashlight_is_bright(void) {
    // Observed module: DO=0 with a flashlight.
    TEST_ASSERT_FALSE(sensor_is_dark(0));
}

void test_bright_commands_relay_off(void) {
    TEST_ASSERT_EQUAL_UINT8(0, sensor_onoff_from_do(0));
}

void test_covered_is_dark(void) {
    // Observed module: DO=1 when covered.
    TEST_ASSERT_TRUE(sensor_is_dark(1));
}

void test_dark_commands_relay_on(void) {
    TEST_ASSERT_EQUAL_UINT8(1, sensor_onoff_from_do(1));
}

static int run_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_flashlight_is_bright);
    RUN_TEST(test_bright_commands_relay_off);
    RUN_TEST(test_covered_is_dark);
    RUN_TEST(test_dark_commands_relay_on);
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
