#include "wf5805f.h"
#include "soft_i2c.h"
#include "board_config.h"

#define WF5805F_REG_STATUS               0x02U
#define WF5805F_REG_DATA                 0x06U
#define WF5805F_REG_CMD                  0x30U
#define WF5805F_CMD_GROUP_CONVERT        0x0AU

#define WF5805F_STATUS_DRDY              0x01U
#define WF5805F_STATUS_ERROR_MASK        0xF0U

#define WF5805F_SAMPLE_PERIOD_MS         1000U
#define WF5805F_STATUS_POLL_MS           2U
#define WF5805F_CONVERT_TIMEOUT_MS       60U

typedef enum
{
	WF5805F_STATE_IDLE = 0,
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

static uint8_t WF5805F_IsSensorValid(WF5805F_Sensor_t sensor)
{
	return ((uint8_t)sensor < (uint8_t)WF5805F_SENSOR_COUNT);
}

static int32_t WF5805F_ConvertRawPressure(uint8_t msb, uint8_t csb, uint8_t lsb)
{
	int32_t value;

	value = (int32_t)msb;
	value <<= 8;
	value |= (int32_t)csb;
	value <<= 8;
	value |= (int32_t)lsb;

	if (value > 8388608L)
	{
		value -= 16777216L;
	}

	return value;
}

static uint16_t WF5805F_ConvertRawTemperature(uint8_t msb, uint8_t lsb)
{
	uint16_t value;

	value = (uint16_t)msb;
	value <<= 8;
	value |= (uint16_t)lsb;

	return value;
}

static int32_t WF5805F_CalcPressureHpaX100(int32_t raw)
{
	int64_t numerator;

	/* Official formula: kPa = (500 * raw / 8388608 + 750) / 6. */
	numerator = ((int64_t)raw * 50000LL) + (750LL * 8388608LL);
	return (int32_t)(numerator / (6LL * 8388608LL));
}

static int16_t WF5805F_CalcTemperatureCX100(uint16_t raw)
{
	int32_t adjusted;

	if (raw > 32768U)
	{
		adjusted = (int32_t)raw - 65844L;
	}
	else
	{
		adjusted = (int32_t)raw - 308L;
	}

	return (int16_t)((adjusted * 100L) / 256L);
}

static void WF5805F_RecordFailure(WF5805F_Context_t *ctx,
                                  WF5805F_Status_t status,
                                  uint32_t now_ms)
{
	ctx->failure_count++;
	ctx->last_error = status;
	ctx->reading.valid = 0U;
	ctx->state = WF5805F_STATE_IDLE;
	ctx->next_action_ms = now_ms + WF5805F_SAMPLE_PERIOD_MS;
}

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

static void WF5805F_RecoverAfterI2CFailure(WF5805F_Context_t *ctx)
{
	if (SoftI2C_RecoverBus(&ctx->bus) == SOFT_I2C_OK)
	{
		ctx->recovery_failure_count = 0U;
	}
	else if (ctx->recovery_failure_count < 0xFFFFU)
	{
		ctx->recovery_failure_count++;
	}
}

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
		WF5805F_RecoverAfterI2CFailure(ctx);
		WF5805F_RecordFailure(ctx, status, now_ms);
		return;
	}

	ctx->state = WF5805F_STATE_WAIT_READY;
	ctx->convert_start_ms = now_ms;
	ctx->next_action_ms = now_ms + WF5805F_STATUS_POLL_MS;
	ctx->last_error = WF5805F_PENDING;
}

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
		WF5805F_RecordFailure(ctx, WF5805F_ERROR_STATUS, now_ms);
		return;
	}

	if ((sensor_status & WF5805F_STATUS_DRDY) == 0U)
	{
		if ((uint32_t)(now_ms - ctx->convert_start_ms) >= WF5805F_CONVERT_TIMEOUT_MS)
		{
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

void WF5805F_InitAll(void)
{
	SoftI2C_Bus_t bus_air;
	SoftI2C_Bus_t bus_basket;
	SoftI2C_Bus_t bus_tank;
	uint8_t i;

	bus_air.scl_gpio = BOARD_I2C_A_SCL_GPIO;
	bus_air.scl_pin = BOARD_I2C_A_SCL_PIN;
	bus_air.scl_rcc = BOARD_RCC_GPIOA;
	bus_air.sda_gpio = BOARD_I2C_A_SDA_GPIO;
	bus_air.sda_pin = BOARD_I2C_A_SDA_PIN;
	bus_air.sda_rcc = BOARD_RCC_GPIOA;

	bus_basket.scl_gpio = BOARD_I2C_B_SCL_GPIO;
	bus_basket.scl_pin = BOARD_I2C_B_SCL_PIN;
	bus_basket.scl_rcc = BOARD_RCC_GPIOB;
	bus_basket.sda_gpio = BOARD_I2C_B_SDA_GPIO;
	bus_basket.sda_pin = BOARD_I2C_B_SDA_PIN;
	bus_basket.sda_rcc = BOARD_RCC_GPIOB;

	bus_tank.scl_gpio = BOARD_I2C_C_SCL_GPIO;
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

void WF5805F_Update(uint32_t now_ms)
{
	uint8_t i;
	WF5805F_Context_t *ctx;

	for (i = 0U; i < (uint8_t)WF5805F_SENSOR_COUNT; i++)
	{
		ctx = &g_wf5805f[i];
		if ((int32_t)(now_ms - ctx->next_action_ms) < 0)
		{
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

WF5805F_Status_t WF5805F_GetReading(WF5805F_Sensor_t sensor, WF5805F_Reading_t *reading)
{
	if (!WF5805F_IsSensorValid(sensor) || (reading == 0))
	{
		return WF5805F_ERROR_PARAM;
	}

	*reading = g_wf5805f[sensor].reading;
	if (g_wf5805f[sensor].reading.valid == 0U)
	{
		return g_wf5805f[sensor].last_error;
	}

	return WF5805F_OK;
}

uint16_t WF5805F_GetFailureCount(WF5805F_Sensor_t sensor)
{
	if (!WF5805F_IsSensorValid(sensor))
	{
		return 0U;
	}

	return g_wf5805f[sensor].failure_count;
}

uint16_t WF5805F_GetRecoveryFailureCount(WF5805F_Sensor_t sensor)
{
	if (!WF5805F_IsSensorValid(sensor))
	{
		return 0U;
	}

	return g_wf5805f[sensor].recovery_failure_count;
}

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
