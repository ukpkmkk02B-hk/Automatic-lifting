#include "soft_i2c.h"

#define SOFT_I2C_DELAY_CYCLES            24U
#define SOFT_I2C_WAIT_TIMEOUT            200U
#define SOFT_I2C_RECOVERY_CLOCKS         9U

static uint8_t SoftI2C_IsBusValid(const SoftI2C_Bus_t *bus)
{
	return (bus != 0) &&
	       (bus->scl_gpio != 0) &&
	       (bus->sda_gpio != 0) &&
	       (bus->scl_pin != 0U) &&
	       (bus->sda_pin != 0U);
}

static void SoftI2C_Pause(void)
{
	volatile uint16_t i;

	for (i = 0U; i < SOFT_I2C_DELAY_CYCLES; i++)
	{
		__NOP();
	}
}

static void SoftI2C_SetSCL(const SoftI2C_Bus_t *bus, BitAction level)
{
	GPIO_WriteBit(bus->scl_gpio, bus->scl_pin, level);
}

static void SoftI2C_SetSDA(const SoftI2C_Bus_t *bus, BitAction level)
{
	GPIO_WriteBit(bus->sda_gpio, bus->sda_pin, level);
}

static BitAction SoftI2C_ReadSCL(const SoftI2C_Bus_t *bus)
{
	return (BitAction)GPIO_ReadInputDataBit(bus->scl_gpio, bus->scl_pin);
}

static BitAction SoftI2C_ReadSDA(const SoftI2C_Bus_t *bus)
{
	return (BitAction)GPIO_ReadInputDataBit(bus->sda_gpio, bus->sda_pin);
}

static SoftI2C_Status_t SoftI2C_WaitSCL(const SoftI2C_Bus_t *bus, BitAction level)
{
	uint16_t timeout;

	for (timeout = 0U; timeout < SOFT_I2C_WAIT_TIMEOUT; timeout++)
	{
		if (SoftI2C_ReadSCL(bus) == level)
		{
			return SOFT_I2C_OK;
		}
		SoftI2C_Pause();
	}

	return SOFT_I2C_ERROR_TIMEOUT;
}

static SoftI2C_Status_t SoftI2C_WaitSDA(const SoftI2C_Bus_t *bus, BitAction level)
{
	uint16_t timeout;

	for (timeout = 0U; timeout < SOFT_I2C_WAIT_TIMEOUT; timeout++)
	{
		if (SoftI2C_ReadSDA(bus) == level)
		{
			return SOFT_I2C_OK;
		}
		SoftI2C_Pause();
	}

	return SOFT_I2C_ERROR_TIMEOUT;
}

static SoftI2C_Status_t SoftI2C_ClockHigh(const SoftI2C_Bus_t *bus)
{
	SoftI2C_SetSCL(bus, Bit_SET);
	if (SoftI2C_WaitSCL(bus, Bit_SET) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}
	SoftI2C_Pause();
	return SOFT_I2C_OK;
}

static void SoftI2C_ClockLow(const SoftI2C_Bus_t *bus)
{
	SoftI2C_SetSCL(bus, Bit_RESET);
	SoftI2C_Pause();
}

static SoftI2C_Status_t SoftI2C_Start(const SoftI2C_Bus_t *bus)
{
	SoftI2C_SetSDA(bus, Bit_SET);
	SoftI2C_SetSCL(bus, Bit_SET);

	if (SoftI2C_WaitSCL(bus, Bit_SET) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}
	if (SoftI2C_WaitSDA(bus, Bit_SET) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_BUS;
	}

	SoftI2C_Pause();
	SoftI2C_SetSDA(bus, Bit_RESET);
	SoftI2C_Pause();
	SoftI2C_ClockLow(bus);

	return SOFT_I2C_OK;
}

static SoftI2C_Status_t SoftI2C_RepeatedStart(const SoftI2C_Bus_t *bus)
{
	SoftI2C_SetSDA(bus, Bit_SET);
	if (SoftI2C_ClockHigh(bus) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}
	SoftI2C_SetSDA(bus, Bit_RESET);
	SoftI2C_Pause();
	SoftI2C_ClockLow(bus);

	return SOFT_I2C_OK;
}

static SoftI2C_Status_t SoftI2C_Stop(const SoftI2C_Bus_t *bus)
{
	SoftI2C_SetSDA(bus, Bit_RESET);
	SoftI2C_Pause();

	if (SoftI2C_ClockHigh(bus) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}

	SoftI2C_SetSDA(bus, Bit_SET);
	if (SoftI2C_WaitSDA(bus, Bit_SET) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}
	SoftI2C_Pause();

	return SOFT_I2C_OK;
}

static SoftI2C_Status_t SoftI2C_WriteByte(const SoftI2C_Bus_t *bus, uint8_t byte)
{
	uint8_t i;

	for (i = 0U; i < 8U; i++)
	{
		if ((byte & 0x80U) != 0U)
		{
			SoftI2C_SetSDA(bus, Bit_SET);
		}
		else
		{
			SoftI2C_SetSDA(bus, Bit_RESET);
		}
		SoftI2C_Pause();

		if (SoftI2C_ClockHigh(bus) != SOFT_I2C_OK)
		{
			return SOFT_I2C_ERROR_TIMEOUT;
		}
		SoftI2C_ClockLow(bus);
		byte <<= 1;
	}

	SoftI2C_SetSDA(bus, Bit_SET);
	SoftI2C_Pause();
	if (SoftI2C_ClockHigh(bus) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}

	if (SoftI2C_ReadSDA(bus) != Bit_RESET)
	{
		SoftI2C_ClockLow(bus);
		return SOFT_I2C_ERROR_NACK;
	}

	SoftI2C_ClockLow(bus);
	return SOFT_I2C_OK;
}

static SoftI2C_Status_t SoftI2C_ReadByte(const SoftI2C_Bus_t *bus,
                                         uint8_t *byte,
                                         uint8_t ack)
{
	uint8_t i;
	uint8_t value;

	if (byte == 0)
	{
		return SOFT_I2C_ERROR_PARAM;
	}

	value = 0U;
	SoftI2C_SetSDA(bus, Bit_SET);

	for (i = 0U; i < 8U; i++)
	{
		value <<= 1;
		if (SoftI2C_ClockHigh(bus) != SOFT_I2C_OK)
		{
			return SOFT_I2C_ERROR_TIMEOUT;
		}
		if (SoftI2C_ReadSDA(bus) == Bit_SET)
		{
			value |= 0x01U;
		}
		SoftI2C_ClockLow(bus);
	}

	if (ack != 0U)
	{
		SoftI2C_SetSDA(bus, Bit_RESET);
	}
	else
	{
		SoftI2C_SetSDA(bus, Bit_SET);
	}
	SoftI2C_Pause();

	if (SoftI2C_ClockHigh(bus) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_TIMEOUT;
	}
	SoftI2C_ClockLow(bus);
	SoftI2C_SetSDA(bus, Bit_SET);

	*byte = value;
	return SOFT_I2C_OK;
}

void SoftI2C_InitBus(const SoftI2C_Bus_t *bus)
{
	GPIO_InitTypeDef gpio_init;

	if (!SoftI2C_IsBusValid(bus))
	{
		return;
	}

	RCC_APB2PeriphClockCmd(bus->scl_rcc | bus->sda_rcc, ENABLE);

	gpio_init.GPIO_Mode = GPIO_Mode_Out_OD;
	gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
	gpio_init.GPIO_Pin = bus->scl_pin;
	GPIO_Init(bus->scl_gpio, &gpio_init);

	gpio_init.GPIO_Pin = bus->sda_pin;
	GPIO_Init(bus->sda_gpio, &gpio_init);

	SoftI2C_SetSCL(bus, Bit_SET);
	SoftI2C_SetSDA(bus, Bit_SET);
	SoftI2C_Pause();
}

SoftI2C_Status_t SoftI2C_RecoverBus(const SoftI2C_Bus_t *bus)
{
	uint8_t i;

	if (!SoftI2C_IsBusValid(bus))
	{
		return SOFT_I2C_ERROR_PARAM;
	}

	SoftI2C_InitBus(bus);
	SoftI2C_SetSDA(bus, Bit_SET);

	for (i = 0U; i < SOFT_I2C_RECOVERY_CLOCKS; i++)
	{
		SoftI2C_SetSCL(bus, Bit_RESET);
		SoftI2C_Pause();
		SoftI2C_SetSCL(bus, Bit_SET);
		if (SoftI2C_WaitSCL(bus, Bit_SET) != SOFT_I2C_OK)
		{
			return SOFT_I2C_ERROR_TIMEOUT;
		}
		SoftI2C_Pause();
	}

	(void)SoftI2C_Stop(bus);

	if (SoftI2C_WaitSDA(bus, Bit_SET) != SOFT_I2C_OK)
	{
		return SOFT_I2C_ERROR_BUS;
	}

	return SOFT_I2C_OK;
}

SoftI2C_Status_t SoftI2C_WriteReg(const SoftI2C_Bus_t *bus,
                                  uint8_t addr_7bit,
                                  uint8_t reg,
                                  uint8_t value)
{
	SoftI2C_Status_t status;

	if (!SoftI2C_IsBusValid(bus))
	{
		return SOFT_I2C_ERROR_PARAM;
	}

	status = SoftI2C_Start(bus);
	if (status == SOFT_I2C_ERROR_BUS)
	{
		status = SoftI2C_RecoverBus(bus);
		if (status == SOFT_I2C_OK)
		{
			status = SoftI2C_Start(bus);
		}
	}
	if (status != SOFT_I2C_OK)
	{
		(void)SoftI2C_Stop(bus);
		return status;
	}

	status = SoftI2C_WriteByte(bus, (uint8_t)(addr_7bit << 1));
	if (status == SOFT_I2C_OK)
	{
		status = SoftI2C_WriteByte(bus, reg);
	}
	if (status == SOFT_I2C_OK)
	{
		status = SoftI2C_WriteByte(bus, value);
	}

	(void)SoftI2C_Stop(bus);
	return status;
}

SoftI2C_Status_t SoftI2C_ReadRegs(const SoftI2C_Bus_t *bus,
                                  uint8_t addr_7bit,
                                  uint8_t reg,
                                  uint8_t *data,
                                  uint8_t len)
{
	SoftI2C_Status_t status;
	uint8_t i;

	if (!SoftI2C_IsBusValid(bus) || (data == 0) || (len == 0U))
	{
		return SOFT_I2C_ERROR_PARAM;
	}

	status = SoftI2C_Start(bus);
	if (status == SOFT_I2C_ERROR_BUS)
	{
		status = SoftI2C_RecoverBus(bus);
		if (status == SOFT_I2C_OK)
		{
			status = SoftI2C_Start(bus);
		}
	}
	if (status != SOFT_I2C_OK)
	{
		(void)SoftI2C_Stop(bus);
		return status;
	}

	status = SoftI2C_WriteByte(bus, (uint8_t)(addr_7bit << 1));
	if (status == SOFT_I2C_OK)
	{
		status = SoftI2C_WriteByte(bus, reg);
	}
	if (status == SOFT_I2C_OK)
	{
		status = SoftI2C_RepeatedStart(bus);
	}
	if (status == SOFT_I2C_OK)
	{
		status = SoftI2C_WriteByte(bus, (uint8_t)((addr_7bit << 1) | 0x01U));
	}

	for (i = 0U; (status == SOFT_I2C_OK) && (i < len); i++)
	{
		status = SoftI2C_ReadByte(bus, &data[i], (uint8_t)(i < (uint8_t)(len - 1U)));
	}

	(void)SoftI2C_Stop(bus);
	return status;
}
