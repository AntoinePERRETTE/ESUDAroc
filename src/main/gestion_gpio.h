#ifndef GESTION_GPIO_H_
#define GESTION_GPIO_H_
#include <stdint.h>

//fonction read :
int8_t gpio_read_input_value(int8_t GPIO_LINE_IN );

//fonction write
int8_t gpio_write_output_value(int8_t GPIO_LINE_OUT , int8_t value );

#endif
