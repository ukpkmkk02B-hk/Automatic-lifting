#include "wf5805f.h"
#include "soft_i2c.h"
#include "board_config.h"

// WF5805F 官方参考流程：0x30 <- 0x0A 启动转换，读 0x02 状态，读 0x06 连续 5 字节数据。
#define WF5805F_REG_STATUS               0x02U
#define WF5805F_REG_DATA                 0x06U
#define WF5805F_REG_CMD                  0x30U
#define WF5805F_CMD_GROUP_CONVERT        0x0AU

// 状态寄存器：DRDY 表示数据就绪，高半字节非 0 视为传感器内部错误。
#define WF5805F_STATUS_DRDY              0x01U
#define WF5805F_STATUS_ERROR_MASK        0xF0U

// 采样和转换等待时间单位 ms；转换等待必须有上限，防止传感器异常卡住主循环。
#define WF5805F_SAMPLE_PERIOD_MS         1000U
#define WF5805F_STATUS_POLL_MS           2U
#define WF5805F_CONVERT_TIMEOUT_MS       60U

typedef enum
{
	// 空闲状态：等待下一次采样周期到达并启动转换。
	WF5805F_STATE_IDLE = 0,
	// 等待数据就绪：周期性读取状态寄存器，不阻塞主循环。
	WF5805F_STATE_WAIT_READY
} WF5805F_State_t;

typedef struct
{
	SoftI2C_Bus_t bus;
	WF5805F_State_t state;
	WF5805F_Reading_t reading;
	WF5805F_Status_t last_error;
	uint16_t failure_count;
	uint16_t recovery_failure_count;
	uint32_t next_action_ms;
	uint32_t convert_start_ms;
} WF5805F_Context_t;

static WF5805F_Context_t g_wf5805f[WF5805F_SENSOR_COUNT];

// 函    数：WF5805F_IsSensorValid
// 参    数：sensor 传感器编号。
// 返 回 值：1 表示编号有效，0 表示越界。
// 注意事项：公共接口先检查编号，避免访问传感器上下文数组越界。
static uint8_t WF5805F_IsSensorValid(WF5805F_Sensor_t sensor)
{
	return ((uint8_t)sensor < (uint8_t)WF5805F_SENSOR_COUNT);
}

// 函    数：WF5805F_ConvertRawPressure
// 参    数：msb/csb/lsb 压力原始 24-bit 数据，高字节在前。
// 返 回 值：带符号压力原始值。
// 注意事项：WF5805F 压力原始值为 24-bit 补码，后续再按官方公式换算压力。
static int32_t WF5805F_ConvertRawPressure(uint8_t msb, uint8_t csb, uint8_t lsb)
{
	int32_t value;

	// 压力原始值为 24-bit 有符号数，高字节在前。
	value = (int32_t)msb;
	value <<= 8;
	value |= (int32_t)csb;
	value <<= 8;
	value |= (int32_t)lsb;

	if (value > 8388608L)
	{
		// 转换为带符号补码值，后续再按官方公式换算压力。
		value -= 16777216L;
	}

	return value;
}

// 函    数：WF5805F_ConvertRawTemperature
// 参    数：msb/lsb 温度原始 16-bit 数据，高字节在前。
// 返 回 值：无符号温度原始值。
// 注意事项：这里只拼接原始值，摄氏度换算在 WF5805F_CalcTemperatureCX100 中完成。
static uint16_t WF5805F_ConvertRawTemperature(uint8_t msb, uint8_t lsb)
{
	uint16_t value;

	value = (uint16_t)msb;
	value <<= 8;
	value |= (uint16_t)lsb;

	return value;
}

// 函    数：WF5805F_CalcPressureHpaX100
// 参    数：raw 24-bit 补码转换后的压力原始值。
// 返 回 值：绝对压力，单位 hPa_x100。
// 注意事项：按官方公式整数化计算，避免在 Keil C 中引入浮点运算。
static int32_t WF5805F_CalcPressureHpaX100(int32_t raw)
{
	int64_t numerator;

	// 官方公式：kPa = (500 * raw / 8388608 + 750) / 6；本驱动输出 hPa_x100。
	numerator = ((int64_t)raw * 50000LL) + (750LL * 8388608LL);
	return (int32_t)(numerator / (6LL * 8388608LL));
}

// 函    数：WF5805F_CalcTemperatureCX100
// 参    数：raw 16-bit 温度原始值。
// 返 回 值：温度，单位 摄氏度_x100。
// 注意事项：按官方参考驱动的偏移修正方式换算，主要用于诊断传感器状态。
static int16_t WF5805F_CalcTemperatureCX100(uint16_t raw)
{
	int32_t adjusted;

	if (raw > 32768U)
	{
		// 按官方示例修正温度原始偏移，输出单位为 摄氏度_x100。
		adjusted = (int32_t)raw - 65844L;
	}
	else
	{
		adjusted = (int32_t)raw - 308L;
	}

	return (int16_t)((adjusted * 100L) / 256L);
}

// 函    数：WF5805F_RecordFailure
// 参    数：ctx 传感器上下文；status 本次失败状态；now_ms 当前毫秒时间戳。
// 返 回 值：无
// 注意事项：连续失败次数由水深模块按阈值升级为传感器故障。
static void WF5805F_RecordFailure(WF5805F_Context_t *ctx,
                                  WF5805F_Status_t status,
                                  uint32_t now_ms)
{
	// 本次读数标记为无效，避免上层把旧压力值当作新读数使用。
	ctx->failure_count++;
	ctx->last_error = status;
	ctx->reading.valid = 0U;
	ctx->state = WF5805F_STATE_IDLE;
	ctx->next_action_ms = now_ms + WF5805F_SAMPLE_PERIOD_MS;
}

// 函    数：WF5805F_MapI2CStatus
// 参    数：status 软件 I2C 返回状态。
// 返 回 值：映射到 WF5805F 驱动层的错误码。
// 注意事项：调用方只暴露传感器驱动错误，不把底层 I2C 枚举泄漏给上层。
static WF5805F_Status_t WF5805F_MapI2CStatus(SoftI2C_Status_t status)
{
	if (status == SOFT_I2C_OK)
	{
		return WF5805F_OK;
	}
	if (status == SOFT_I2C_ERROR_TIMEOUT)
	{
		return WF5805F_ERROR_TIMEOUT;
	}
	if (status == SOFT_I2C_ERROR_PARAM)
	{
		return WF5805F_ERROR_PARAM;
	}
	return WF5805F_ERROR_I2C;
}

// 函    数：WF5805F_RecoverAfterI2CFailure
// 参    数：ctx 传感器上下文。
// 返 回 值：无
// 注意事项：I2C 失败后立即尝试 9 脉冲恢复；恢复失败次数单独累计。
static void WF5805F_RecoverAfterI2CFailure(WF5805F_Context_t *ctx)
{
	// 恢复成功清零恢复失败计数，失败则累加供水深模块升级为 I2C 故障。
	if (SoftI2C_RecoverBus(&ctx->bus) == SOFT_I2C_OK)
	{
		ctx->recovery_failure_count = 0U;
	}
	else if (ctx->recovery_failure_count < 0xFFFFU)
	{
		ctx->recovery_failure_count++;
	}
}

// 函    数：WF5805F_StartConversion
// 参    数：ctx 传感器上下文；now_ms 当前毫秒时间戳。
// 返 回 值：无
// 注意事项：向 0x30 写 0x0A 启动转换，失败时先恢复总线再记录失败。
static void WF5805F_StartConversion(WF5805F_Context_t *ctx, uint32_t now_ms)
{
	SoftI2C_Status_t i2c_status;
	WF5805F_Status_t status;

	i2c_status = SoftI2C_WriteReg(&ctx->bus,
	                              BOARD_WF5805F_ADDR_7BIT,
	                              WF5805F_REG_CMD,
	                              WF5805F_CMD_GROUP_CONVERT);
	status = WF5805F_MapI2CStatus(i2c_status);
	if (status != WF5805F_OK)
	{
		// 启动转换失败通常是总线或地址阶段异常，先恢复总线再记录失败。
		WF5805F_RecoverAfterI2CFailure(ctx);
		WF5805F_RecordFailure(ctx, status, now_ms);
		return;
	}

	ctx->state = WF5805F_STATE_WAIT_READY;
	// 转换开始后轮询状态寄存器，不在这里阻塞等待。
	ctx->convert_start_ms = now_ms;
	ctx->next_action_ms = now_ms + WF5805F_STATUS_POLL_MS;
	ctx->last_error = WF5805F_PENDING;
}

// 函    数：WF5805F_ReadWhenReady
// 参    数：ctx 传感器上下文；now_ms 当前毫秒时间戳。
// 返 回 值：无
// 注意事项：读取状态寄存器确认 DRDY，再从 0x06 连续读取 5 字节压力/温度数据。
static void WF5805F_ReadWhenReady(WF5805F_Context_t *ctx, uint32_t now_ms)
{
	SoftI2C_Status_t i2c_status;
	WF5805F_Status_t status;
	uint8_t sensor_status;
	uint8_t data[5];
	uint8_t i;
	int32_t raw_pressure;
	uint16_t raw_temperature;

	i2c_status = SoftI2C_ReadRegs(&ctx->bus,
	                              BOARD_WF5805F_ADDR_7BIT,
	                              WF5805F_REG_STATUS,
	                              &sensor_status,
	                              1U);
	status = WF5805F_MapI2CStatus(i2c_status);
	if (status != WF5805F_OK)
	{
		WF5805F_RecoverAfterI2CFailure(ctx);
		WF5805F_RecordFailure(ctx, status, now_ms);
		return;
	}

	ctx->reading.status = sensor_status;

	if ((sensor_status & WF5805F_STATUS_ERROR_MASK) != 0U)
	{
		// 传感器报告内部错误时不返回伪造有效压力值。
		WF5805F_RecordFailure(ctx, WF5805F_ERROR_STATUS, now_ms);
		return;
	}

	if ((sensor_status & WF5805F_STATUS_DRDY) == 0U)
	{
		if ((uint32_t)(now_ms - ctx->convert_start_ms) >= WF5805F_CONVERT_TIMEOUT_MS)
		{
			// 转换超时后放弃本次读数，系统保持非阻塞轮询。
			WF5805F_RecordFailure(ctx, WF5805F_ERROR_TIMEOUT, now_ms);
		}
		else
		{
			ctx->next_action_ms = now_ms + WF5805F_STATUS_POLL_MS;
		}
		return;
	}

	i2c_status = SoftI2C_ReadRegs(&ctx->bus,
	                              BOARD_WF5805F_ADDR_7BIT,
	                              WF5805F_REG_DATA,
	                              data,
	                              5U);
	status = WF5805F_MapI2CStatus(i2c_status);
	if (status != WF5805F_OK)
	{
		WF5805F_RecoverAfterI2CFailure(ctx);
		WF5805F_RecordFailure(ctx, status, now_ms);
		return;
	}

	for (i = 0U; i < 5U; i++)
	{
		// 保留原始 5 字节，便于调试压力/温度换算和状态异常。
		ctx->reading.raw[i] = data[i];
	}

	raw_pressure = WF5805F_ConvertRawPressure(data[0], data[1], data[2]);
	raw_temperature = WF5805F_ConvertRawTemperature(data[3], data[4]);

	ctx->reading.pressure_hpa_x100 = WF5805F_CalcPressureHpaX100(raw_pressure);
	ctx->reading.temperature_c_x100 = WF5805F_CalcTemperatureCX100(raw_temperature);
	ctx->reading.valid = 1U;
	ctx->reading.timestamp_ms = now_ms;
	ctx->failure_count = 0U;
	ctx->last_error = WF5805F_OK;
	ctx->state = WF5805F_STATE_IDLE;
	ctx->next_action_ms = now_ms + WF5805F_SAMPLE_PERIOD_MS;
}

// 函    数：WF5805F_InitAll
// 参    数：无
// 返 回 值：无
// 注意事项：把三颗同地址 WF5805F 分别绑定到 I2C-A/B/C，并初始化为 PENDING。
void WF5805F_InitAll(void)
{
	SoftI2C_Bus_t bus_air;
	SoftI2C_Bus_t bus_basket;
	SoftI2C_Bus_t bus_tank;
	uint8_t i;

	bus_air.scl_gpio = BOARD_I2C_A_SCL_GPIO;
	// I2C-A 只接 P_air，作为大气压参考。
	bus_air.scl_pin = BOARD_I2C_A_SCL_PIN;
	bus_air.scl_rcc = BOARD_RCC_GPIOA;
	bus_air.sda_gpio = BOARD_I2C_A_SDA_GPIO;
	bus_air.sda_pin = BOARD_I2C_A_SDA_PIN;
	bus_air.sda_rcc = BOARD_RCC_GPIOA;

	bus_basket.scl_gpio = BOARD_I2C_B_SCL_GPIO;
	// I2C-B 只接 P_basket，避免与同地址 WF5805F 冲突。
	bus_basket.scl_pin = BOARD_I2C_B_SCL_PIN;
	bus_basket.scl_rcc = BOARD_RCC_GPIOB;
	bus_basket.sda_gpio = BOARD_I2C_B_SDA_GPIO;
	bus_basket.sda_pin = BOARD_I2C_B_SDA_PIN;
	bus_basket.sda_rcc = BOARD_RCC_GPIOB;

	bus_tank.scl_gpio = BOARD_I2C_C_SCL_GPIO;
	// I2C-C 只接 P_tank，用于鱼缸整体水深安全判断。
	bus_tank.scl_pin = BOARD_I2C_C_SCL_PIN;
	bus_tank.scl_rcc = BOARD_RCC_GPIOA;
	bus_tank.sda_gpio = BOARD_I2C_C_SDA_GPIO;
	bus_tank.sda_pin = BOARD_I2C_C_SDA_PIN;
	bus_tank.sda_rcc = BOARD_RCC_GPIOA;

	g_wf5805f[WF5805F_SENSOR_AIR].bus = bus_air;
	g_wf5805f[WF5805F_SENSOR_BASKET].bus = bus_basket;
	g_wf5805f[WF5805F_SENSOR_TANK].bus = bus_tank;

	for (i = 0U; i < (uint8_t)WF5805F_SENSOR_COUNT; i++)
	{
		// 初始化后读数尚未有效，上层必须看到 PENDING 或错误再决定是否进入故障。
		g_wf5805f[i].state = WF5805F_STATE_IDLE;
		g_wf5805f[i].reading.valid = 0U;
		g_wf5805f[i].reading.status = 0U;
		g_wf5805f[i].last_error = WF5805F_PENDING;
		g_wf5805f[i].failure_count = 0U;
		g_wf5805f[i].recovery_failure_count = 0U;
		g_wf5805f[i].next_action_ms = 0U;
		g_wf5805f[i].convert_start_ms = 0U;
		SoftI2C_InitBus(&g_wf5805f[i].bus);
	}
}

// 函    数：WF5805F_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：非阻塞推进三颗传感器采样，禁止用 Delay 等待转换完成。
void WF5805F_Update(uint32_t now_ms)
{
	uint8_t i;
	WF5805F_Context_t *ctx;

	for (i = 0U; i < (uint8_t)WF5805F_SENSOR_COUNT; i++)
	{
		ctx = &g_wf5805f[i];
		if ((int32_t)(now_ms - ctx->next_action_ms) < 0)
		{
			// 使用时间戳差值调度采样，避免 Delay 或忙等。
			continue;
		}

		if (ctx->state == WF5805F_STATE_IDLE)
		{
			WF5805F_StartConversion(ctx, now_ms);
		}
		else
		{
			WF5805F_ReadWhenReady(ctx, now_ms);
		}
	}
}

// 函    数：WF5805F_GetReading
// 参    数：sensor 传感器编号；reading 输出最近读数。
// 返 回 值：WF5805F_OK 表示读数有效；读数无效时返回最近错误码。
// 注意事项：无有效读数时调用方不能使用旧值伪装成功。
WF5805F_Status_t WF5805F_GetReading(WF5805F_Sensor_t sensor, WF5805F_Reading_t *reading)
{
	if (!WF5805F_IsSensorValid(sensor) || (reading == 0))
	{
		return WF5805F_ERROR_PARAM;
	}

	*reading = g_wf5805f[sensor].reading;
	if (g_wf5805f[sensor].reading.valid == 0U)
	{
		// 无有效读数时返回最近错误码，调用方不能使用旧值伪装成功。
		return g_wf5805f[sensor].last_error;
	}

	return WF5805F_OK;
}

// 函    数：WF5805F_GetFailureCount
// 参    数：sensor 传感器编号。
// 返 回 值：连续读数失败次数；非法编号返回 0。
// 注意事项：水深模块使用该计数触发 P_air/P_basket/P_tank 传感器故障。
uint16_t WF5805F_GetFailureCount(WF5805F_Sensor_t sensor)
{
	if (!WF5805F_IsSensorValid(sensor))
	{
		return 0U;
	}

	return g_wf5805f[sensor].failure_count;
}

// 函    数：WF5805F_GetRecoveryFailureCount
// 参    数：sensor 传感器编号。
// 返 回 值：I2C 总线恢复失败次数；非法编号返回 0。
// 注意事项：水深模块使用该计数触发对应 I2C-A/B/C 故障。
uint16_t WF5805F_GetRecoveryFailureCount(WF5805F_Sensor_t sensor)
{
	if (!WF5805F_IsSensorValid(sensor))
	{
		return 0U;
	}

	return g_wf5805f[sensor].recovery_failure_count;
}

// 函    数：WF5805F_ResetBus
// 参    数：sensor 传感器编号。
// 返 回 值：WF5805F_OK 表示恢复成功，其它值表示参数或 I2C 恢复失败。
// 注意事项：恢复成功后重新回到空闲采样状态；恢复失败计入总线健康状态。
WF5805F_Status_t WF5805F_ResetBus(WF5805F_Sensor_t sensor)
{
	SoftI2C_Status_t status;

	if (!WF5805F_IsSensorValid(sensor))
	{
		return WF5805F_ERROR_PARAM;
	}

	status = SoftI2C_RecoverBus(&g_wf5805f[sensor].bus);
	if (status != SOFT_I2C_OK)
	{
		// 恢复失败计入总线健康状态，连续失败会升级为 I2C 故障。
		if (g_wf5805f[sensor].recovery_failure_count < 0xFFFFU)
		{
			g_wf5805f[sensor].recovery_failure_count++;
		}
		return WF5805F_MapI2CStatus(status);
	}

	g_wf5805f[sensor].recovery_failure_count = 0U;
	g_wf5805f[sensor].state = WF5805F_STATE_IDLE;
	g_wf5805f[sensor].next_action_ms = 0U;
	return WF5805F_OK;
}
