#ifndef GPIO_INT_H_
#define GPIO_INT_H_
//-------------------------------------------------------------
#define INTERRUPT_GPIO GPIO_NUM_21
//-------------------------------------------------------------
typedef void (*GpioInterruptCallback)(int level); // Определяем тип указателя на функцию
void initialize_gpio_interrupt(GpioInterruptCallback callback);
void gpio_interrupt_on(void);
//-------------------------------------------------------------
#endif /* GPIO_INT_H_ */