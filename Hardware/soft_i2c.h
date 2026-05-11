#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "stm32f10x.h"

typedef enum
{
	SOFT_I2C_OK = 0,
	SOFT_I2C_ERROR_PARAM,
	SOFT_I2C_ERROR_TIMEOUT,
	SOFT_I2C_ERROR_NACK,
	SOFT_I2C_ERROR_BUS
} SoftI2C_Status_t;

typedef struct
{
	GPIO_TypeDef *scl_gpio;
	uint16_t scl_pin;
	uint32_t scl_rcc;
	GPIO_TypeDef *sda_gpio;
	uint16_t sda_pin;
	uint32_t sda_rcc;
} SoftI2C_Bus_t;

void SoftI2C_InitBus(const SoftI2C_Bus_t *bus);
SoftI2C_Status_t SoftI2C_WriteReg(const SoftI2C_Bus_t *bus,
                                  uint8_t addr_7bit,
                                  uint8_t reg,
                                  uint8_t value);
SoftI2C_Status_t SoftI2C_ReadRegs(const SoftI2C_Bus_t *bus,
                                  uint8_t addr_7bit,
                                  uint8_t reg,
                                  uint8_t *data,
                                  uint8_t len);
SoftI2C_Status_t SoftI2C_RecoverBus(const SoftI2C_Bus_t *bus);

#endif
