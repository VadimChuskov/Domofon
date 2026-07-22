#include "gpio_out.h"
//-------------------------------------------------------------
#include "driver/gpio.h"
//-------------------------------------------------------------

void initialize_gpio_output(gpio_num_t gpio)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << gpio);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(gpio, 0);
}
//-------------------------------------------------------------
void gpio_output_on(gpio_num_t gpio)
{
    gpio_set_level(gpio, 1);
}
//-------------------------------------------------------------
void gpio_output_off(gpio_num_t gpio)
{
    gpio_set_level(gpio, 0);
}
//-------------------------------------------------------------
