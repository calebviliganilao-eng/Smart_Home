#include "unity.h"
#include "../../src/relay_logic.h"
#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

void setUp(void) {}
void tearDown(void) {}

void test_relay_on_drives_gpio_high(void) {
    TEST_ASSERT_EQUAL_INT(1, relay_output_level(true));
}

void test_relay_off_drives_gpio_low(void) {
    TEST_ASSERT_EQUAL_INT(0, relay_output_level(false));
}

#ifdef ESP_PLATFORM
void test_relay_gpio4_can_be_output(void) {
    TEST_ASSERT_TRUE(GPIO_IS_VALID_OUTPUT_GPIO(GPIO_NUM_4));
}

void test_provisioning_led_gpio19_can_be_output(void) {
    TEST_ASSERT_TRUE(GPIO_IS_VALID_OUTPUT_GPIO(GPIO_NUM_19));
}
#endif

static int run_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_relay_on_drives_gpio_high);
    RUN_TEST(test_relay_off_drives_gpio_low);
#ifdef ESP_PLATFORM
    RUN_TEST(test_relay_gpio4_can_be_output);
    RUN_TEST(test_provisioning_led_gpio19_can_be_output);
#endif
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
