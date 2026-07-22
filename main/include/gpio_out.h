#ifndef GPIO_OUT_H_
#define GPIO_OUT_H_
//-------------------------------------------------------------
#include "driver/gpio.h"
//-------------------------------------------------------------
void initialize_gpio_output(gpio_num_t gpio);
void gpio_output_on(gpio_num_t gpio);
void gpio_output_off(gpio_num_t gpio);
//-------------------------------------------------------------
#endif /* GPIO_OUT_H_ */