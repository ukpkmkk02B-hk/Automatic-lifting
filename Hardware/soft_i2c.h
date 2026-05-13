#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#include "stm32f10x.h"

// 模    块：可配置引脚的软件 I2C 主机
// 硬件假设：SCL/SDA 采用开漏输出，并由外部或模块上拉到 3.3V。
// 安全约束：所有等待 SCL/SDA 的步骤都必须有超时，错误返回交给上层处理；
//           传感器或线缆异常时不能卡死主循环。

typedef enum
{
	// 操作成功。
	SOFT_I2C_OK = 0,
	// 参数非法，例如总线结构体为空或数据缓冲区为空。
	SOFT_I2C_ERROR_PARAM,
	// 等待 SCL/SDA 释放或目标电平时超时。
	SOFT_I2C_ERROR_TIMEOUT,
	// 从机未应答地址、寄存器或数据字节。
	SOFT_I2C_ERROR_NACK,
	// 总线被拉低或启动条件前总线不空闲。
	SOFT_I2C_ERROR_BUS
} SoftI2C_Status_t;

typedef struct
{
	// SCL/SDA 的 GPIO、引脚和 RCC 时钟；每条总线必须独立配置。
	GPIO_TypeDef *scl_gpio;
	uint16_t scl_pin;
	uint32_t scl_rcc;
	GPIO_TypeDef *sda_gpio;
	uint16_t sda_pin;
	uint32_t sda_rcc;
} SoftI2C_Bus_t;

// 函    数：SoftI2C_InitBus
// 参    数：bus 软件 I2C 总线配置，包含 SCL/SDA GPIO、引脚和 RCC 时钟。
// 返 回 值：无
// 注意事项：初始化为开漏输出，并释放 SCL/SDA 为高电平；参数非法时直接返回。
void SoftI2C_InitBus(const SoftI2C_Bus_t *bus);

// 函    数：SoftI2C_WriteReg
// 参    数：bus 软件 I2C 总线配置。
// 参    数：addr_7bit 从机 7-bit 地址，不包含读写位。
// 参    数：reg 寄存器地址，范围 0x00-0xFF。
// 参    数：value 要写入寄存器的数据，范围 0x00-0xFF。
// 返 回 值：SOFT_I2C_OK 表示成功，其它值表示参数、超时、NACK 或总线错误。
// 注意事项：START 前若总线不空闲，会先尝试 9 个 SCL 脉冲恢复再重试。
SoftI2C_Status_t SoftI2C_WriteReg(const SoftI2C_Bus_t *bus,
                                  uint8_t addr_7bit,
                                  uint8_t reg,
                                  uint8_t value);

// 函    数：SoftI2C_ReadRegs
// 参    数：bus 软件 I2C 总线配置。
// 参    数：addr_7bit 从机 7-bit 地址，不包含读写位。
// 参    数：reg 起始寄存器地址，范围 0x00-0xFF。
// 参    数：data 读取数据输出缓冲区。
// 参    数：len 要连续读取的字节数，必须大于 0。
// 返 回 值：SOFT_I2C_OK 表示成功，其它值表示参数、超时、NACK 或总线错误。
// 注意事项：写寄存器地址后发送重复起始，最后一个字节发送 NACK 结束读操作。
SoftI2C_Status_t SoftI2C_ReadRegs(const SoftI2C_Bus_t *bus,
                                  uint8_t addr_7bit,
                                  uint8_t reg,
                                  uint8_t *data,
                                  uint8_t len);

// 函    数：SoftI2C_RecoverBus
// 参    数：bus 软件 I2C 总线配置。
// 返 回 值：SOFT_I2C_OK 表示恢复成功，其它值表示参数、超时或总线仍被拉低。
// 注意事项：发送 9 个 SCL 恢复脉冲并产生 STOP，用于释放 SDA 被从机拉低的死锁总线。
SoftI2C_Status_t SoftI2C_RecoverBus(const SoftI2C_Bus_t *bus);

#endif
