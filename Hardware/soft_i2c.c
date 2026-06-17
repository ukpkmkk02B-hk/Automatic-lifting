#include "soft_i2c.h"

// 软件延时只用于 I2C 位时序，不用于业务等待；所有总线等待另有超时保护。
#define SOFT_I2C_DELAY_CYCLES            24U
// 等待 SCL/SDA 目标电平的循环上限，避免传感器或线缆故障时死等。
#define SOFT_I2C_WAIT_TIMEOUT            200U
// I2C 规范常用 9 个 SCL 脉冲释放被从机保持的 SDA。
#define SOFT_I2C_RECOVERY_CLOCKS         9U

// 函    数：SoftI2C_IsBusValid
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：1 表示配置可用，0 表示 GPIO 或引脚配置缺失。
// 注意事项：公共接口先检查参数，避免空指针导致寄存器访问异常。
static uint8_t SoftI2C_IsBusValid(const SoftI2C_Bus_t *bus)
{
	return (bus != 0) &&
	       (bus->scl_gpio != 0) &&
	       (bus->sda_gpio != 0) &&
	       (bus->scl_pin != 0U) &&
	       (bus->sda_pin != 0U);
}

// 函    数：SoftI2C_Pause
// 参    数：无
// 返 回 值：无
// 注意事项：仅用于 I2C 位级时序延时，不用于传感器转换或业务等待。
static void SoftI2C_Pause(void)
{
	volatile uint16_t i;

	for (i = 0U; i < SOFT_I2C_DELAY_CYCLES; i++)
	{
		__NOP();
	}
}

// 函    数：SoftI2C_SetSCL
// 参    数：bus 软件 I2C 总线配置；level 要写入 SCL 的电平。
// 返 回 值：无
// 注意事项：开漏模式下写 1 表示释放 SCL，由上拉电阻拉高。
static void SoftI2C_SetSCL(const SoftI2C_Bus_t *bus, BitAction level)
{
	GPIO_WriteBit(bus->scl_gpio, bus->scl_pin, level);
}

// 函    数：SoftI2C_SetSDA
// 参    数：bus 软件 I2C 总线配置；level 要写入 SDA 的电平。
// 返 回 值：无
// 注意事项：开漏模式下写 1 表示释放 SDA，由上拉电阻拉高。
static void SoftI2C_SetSDA(const SoftI2C_Bus_t *bus, BitAction level)
{
	GPIO_WriteBit(bus->sda_gpio, bus->sda_pin, level);
}

// 函    数：SoftI2C_ReadSCL
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：当前 SCL 输入电平。
// 注意事项：读取输入电平可发现时钟拉伸或线路被外设拉低。
static BitAction SoftI2C_ReadSCL(const SoftI2C_Bus_t *bus)
{
	return (BitAction)GPIO_ReadInputDataBit(bus->scl_gpio, bus->scl_pin);
}

// 函    数：SoftI2C_ReadSDA
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：当前 SDA 输入电平。
// 注意事项：读取输入电平用于采样数据位、ACK 位和总线释放状态。
static BitAction SoftI2C_ReadSDA(const SoftI2C_Bus_t *bus)
{
	return (BitAction)GPIO_ReadInputDataBit(bus->sda_gpio, bus->sda_pin);
}

// 函    数：SoftI2C_WaitSCL
// 参    数：bus 软件 I2C 总线配置；level 目标 SCL 电平。
// 返 回 值：SOFT_I2C_OK 表示到达目标电平，SOFT_I2C_ERROR_TIMEOUT 表示超时。
// 注意事项：等待 SCL 可支持从机时钟拉伸，但必须有上限。
static SoftI2C_Status_t SoftI2C_WaitSCL(const SoftI2C_Bus_t *bus, BitAction level)
{
	uint16_t timeout;

	// 等待 SCL 可支持时钟拉伸，但必须超时退出。
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

// 函    数：SoftI2C_WaitSDA
// 参    数：bus 软件 I2C 总线配置；level 目标 SDA 电平。
// 返 回 值：SOFT_I2C_OK 表示到达目标电平，SOFT_I2C_ERROR_TIMEOUT 表示超时。
// 注意事项：SDA 被外设或线缆故障拉低时返回超时，由上层触发恢复流程。
static SoftI2C_Status_t SoftI2C_WaitSDA(const SoftI2C_Bus_t *bus, BitAction level)
{
	uint16_t timeout;

	// SDA 被外设或线缆故障拉低时返回超时，由上层触发恢复流程。
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

// 函    数：SoftI2C_ClockHigh
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：SOFT_I2C_OK 表示 SCL 已释放为高电平，超时则返回错误。
// 注意事项：I2C 数据采样和 ACK 采样都在 SCL 高电平期间完成。
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

// 函    数：SoftI2C_ClockLow
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：无
// 注意事项：拉低 SCL 后从机允许改变 SDA，主机也准备下一个数据位。
static void SoftI2C_ClockLow(const SoftI2C_Bus_t *bus)
{
	SoftI2C_SetSCL(bus, Bit_RESET);
	SoftI2C_Pause();
}

// 函    数：SoftI2C_Start
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：SOFT_I2C_OK 表示 START 成功，其它值表示总线未释放或超时。
// 注意事项：START 条件是在 SCL 高电平期间 SDA 从高变低。
static SoftI2C_Status_t SoftI2C_Start(const SoftI2C_Bus_t *bus)
{
	// START 前总线应释放为高；若 SDA 仍低，视为总线死锁而非继续抢总线。
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

// 函    数：SoftI2C_RepeatedStart
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：SOFT_I2C_OK 表示重复起始成功，超时则返回错误。
// 注意事项：读寄存器时先写寄存器地址，再用重复起始切换到读方向。
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

// 函    数：SoftI2C_Stop
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：SOFT_I2C_OK 表示 STOP 成功，其它值表示 SCL/SDA 未释放。
// 注意事项：STOP 条件是在 SCL 高电平期间 SDA 从低释放为高。
static SoftI2C_Status_t SoftI2C_Stop(const SoftI2C_Bus_t *bus)
{
	// STOP 需要在 SCL 高电平期间释放 SDA。
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

// 函    数：SoftI2C_WriteByte
// 参    数：bus 软件 I2C 总线配置；byte 要发送的 8-bit 数据。
// 返 回 值：SOFT_I2C_OK 表示发送并收到 ACK，其它值表示超时或 NACK。
// 注意事项：数据按 MSB first 发送，第 9 个时钟由从机拉低 SDA 作为 ACK。
static SoftI2C_Status_t SoftI2C_WriteByte(const SoftI2C_Bus_t *bus, uint8_t byte)
{
	uint8_t i;

	for (i = 0U; i < 8U; i++)
	{
		// MSB first，SDA 在 SCL 上升沿前稳定。
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
		// ACK 位应由从机拉低；未拉低说明无应答。
		SoftI2C_ClockLow(bus);
		return SOFT_I2C_ERROR_NACK;
	}

	SoftI2C_ClockLow(bus);
	return SOFT_I2C_OK;
}

// 函    数：SoftI2C_ReadByte
// 参    数：bus 软件 I2C 总线配置；byte 输出接收到的数据；ack 非 0 发送 ACK，0 发送 NACK。
// 返 回 值：SOFT_I2C_OK 表示读取成功，其它值表示参数或超时错误。
// 注意事项：主机释放 SDA 后由从机驱动数据；最后一个字节应发送 NACK 结束读取。
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
	// 释放 SDA 后由从机驱动数据位。
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
		// 多字节读取时，非最后一个字节发送 ACK 请求继续。
		SoftI2C_SetSDA(bus, Bit_RESET);
	}
	else
	{
		// 最后一个字节发送 NACK，随后 STOP 结束读操作。
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

// 函    数：SoftI2C_InitBus
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：无
// 注意事项：开漏输出配合 3.3V 上拉模拟 I2C，写 1 表示释放总线。
void SoftI2C_InitBus(const SoftI2C_Bus_t *bus)
{
	GPIO_InitTypeDef gpio_init;

	if (!SoftI2C_IsBusValid(bus))
	{
		return;
	}

	RCC_APB2PeriphClockCmd(bus->scl_rcc | bus->sda_rcc, ENABLE);

	// 开漏输出配合 3.3V 上拉模拟 I2C，写 1 表示释放总线。
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

// 函    数：SoftI2C_RecoverBus
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：SOFT_I2C_OK 表示恢复成功，其它值表示参数、超时或总线仍异常。
// 注意事项：通过 9 个 SCL 脉冲让从机移出未完成字节，再发送 STOP 释放总线。
SoftI2C_Status_t SoftI2C_RecoverBus(const SoftI2C_Bus_t *bus)
{
	uint8_t i;

	if (!SoftI2C_IsBusValid(bus))
	{
		return SOFT_I2C_ERROR_PARAM;
	}

	SoftI2C_InitBus(bus);
	SoftI2C_SetSDA(bus, Bit_SET);

	// 尝试通过 9 个 SCL 脉冲让从机移出未完成字节，释放 SDA。
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
		// 恢复后 SDA 仍不能释放，报告总线错误给上层累计故障。
		return SOFT_I2C_ERROR_BUS;
	}

	return SOFT_I2C_OK;
}

// 函    数：SoftI2C_WriteReg
// 参    数：bus 软件 I2C 总线配置；addr_7bit 从机 7-bit 地址；reg 寄存器地址；value 写入值。
// 返 回 值：SOFT_I2C_OK 表示成功，其它值表示参数、超时、NACK 或总线错误。
// 注意事项：addr_7bit 左移后最低位为 0，表示写方向。
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
		// START 前发现总线不空闲，先执行一次恢复再重试。
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
		// addr_7bit 左移后最低位为 0，表示写方向。
		status = SoftI2C_WriteByte(bus, reg);
	}
	if (status == SOFT_I2C_OK)
	{
		status = SoftI2C_WriteByte(bus, value);
	}

	(void)SoftI2C_Stop(bus);
	return status;
}

// 函    数：SoftI2C_ReadRegs
// 参    数：bus 软件 I2C 总线配置；addr_7bit 从机 7-bit 地址；reg 起始寄存器；
//           data 输出缓冲区；len 连续读取字节数。
// 返 回 值：SOFT_I2C_OK 表示成功，其它值表示参数、超时、NACK 或总线错误。
// 注意事项：读取流程为写寄存器地址 -> 重复起始 -> 读地址 -> 连续读取。
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
		// 读操作同样先尝试恢复死锁总线，再重新发 START。
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
		// 重复起始后发送读地址，最低位为 1。
		status = SoftI2C_WriteByte(bus, (uint8_t)((addr_7bit << 1) | 0x01U));
	}

	for (i = 0U; (status == SOFT_I2C_OK) && (i < len); i++)
	{
		status = SoftI2C_ReadByte(bus, &data[i], (uint8_t)(i < (uint8_t)(len - 1U)));
	}

	(void)SoftI2C_Stop(bus);
	return status;
}
