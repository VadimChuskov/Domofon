#include "gpio_int.h"
//-------------------------------------------------------------
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_intr_types.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include <esp_log.h>
//-------------------------------------------------------------

static const char *TAG = "GPIO";

#define GPIO_INPUT_PIN_SEL (1ULL << INTERRUPT_GPIO)
#define ESP_INTR_FLAG_DEFAULT 0

//-------------------------------------------------------------
static SemaphoreHandle_t gpio_event_sem = NULL;
//-------------------------------------------------------------

static void gpio_interrupt_off(void);

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(gpio_event_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void gpio_int_task(void *arg)
{
    GpioInterruptCallback callback = (GpioInterruptCallback)arg;
    for (;;)
    {
        if (xSemaphoreTake(gpio_event_sem, portMAX_DELAY) == pdTRUE)
        {
            int level = gpio_get_level(INTERRUPT_GPIO);
            ESP_LOGW(TAG, "GPIO interrupt: level = %d", level);
            gpio_interrupt_off(); // Отключаем прерывания, чтобы избежать повторных вызовов 
            callback(level);
        }
    }
}

void initialize_gpio_interrupt(GpioInterruptCallback callback)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE; // Прерывание по спаду
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    gpio_event_sem = xSemaphoreCreateBinary();
    xTaskCreate(gpio_int_task, "gpio_int_task", 4096, (void *)callback, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(INTERRUPT_GPIO, gpio_isr_handler, (void *)INTERRUPT_GPIO);
}

void gpio_interrupt_on(void)
{
    // Сбросить защёлкнутый аппаратный флаг прерывания
    gpio_ll_clear_intr_status(&GPIO, 1ULL << INTERRUPT_GPIO);
    // Вычитать семафор, если ISR успел его отдать
    xSemaphoreTake(gpio_event_sem, 0);
    gpio_intr_enable(INTERRUPT_GPIO);
}

static void gpio_interrupt_off(void)
{
    gpio_intr_disable(INTERRUPT_GPIO);
}