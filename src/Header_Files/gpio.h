#ifndef GPIO_HG
#define GPIO_HG

//***********************************************************************************
// included files
//***********************************************************************************
// system included files


// Silicon Labs included files
#include "em_assert.h"
#include "em_cmu.h"
#include "em_gpio.h"

// developer included files
#include "app.h"
#include "brd_config.h"
#include "scheduler.h"


//***********************************************************************************
// defined macros
//***********************************************************************************


//***********************************************************************************
// enums
//***********************************************************************************


//***********************************************************************************
// structs
//***********************************************************************************


//***********************************************************************************
// function prototypes
//***********************************************************************************
void gpio_open(void);
void drive_leds(uint16_t humidity, GPIO_Port_TypeDef led_port, uint8_t led_pin);
#endif
