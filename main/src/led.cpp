#include "led.h"
//-------------------------------------------------------------
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
//-------------------------------------------------------------

static const char *TAG = "LED";
static TimerHandle_t blink_timer = NULL;
static bool led_state = false;
//-------------------------------------------------------------
static void blink_timer_callback(TimerHandle_t xTimer)
{
    led_state = !led_state;
    gpio_set_level(LED_GPIO, led_state ? 1 : 0);
}
//-------------------------------------------------------------
static void stop_blink(void)
{
    if (blink_timer != NULL) {
        xTimerStop(blink_timer, 0);
        xTimerDelete(blink_timer, 0);
        blink_timer = NULL;
    }
    led_state = false;
}
//-------------------------------------------------------------
void initialize_led(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << LED_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "LED initialized on GPIO %d", LED_GPIO);
}

void led_on(void)
{
    stop_blink();
    gpio_set_level(LED_GPIO, 1);
}

void led_off(void)
{
    stop_blink();
    gpio_set_level(LED_GPIO, 0);
}

void led_blink(int period_ms)
{
    stop_blink();
    TickType_t half_period = pdMS_TO_TICKS(period_ms / 2);
    if (half_period == 0) half_period = 1;
    blink_timer = xTimerCreate("blink", half_period, pdTRUE, NULL, blink_timer_callback);
    if (blink_timer != NULL) {
        xTimerStart(blink_timer, 0);
        ESP_LOGI(TAG, "LED blinking with period %d ms", period_ms);
    }
}