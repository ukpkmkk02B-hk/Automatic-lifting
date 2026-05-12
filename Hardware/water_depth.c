#include "water_depth.h"
#include "wf5805f.h"
#include "board_config.h"
#include "error_manager.h"

typedef struct
{
	int32_t samples[BOARD_WATER_FILTER_SAMPLES];
	uint8_t index;
	uint8_t count;
	uint32_t last_timestamp_ms;
} WaterDepth_Filter_t;

static WaterDepth_Filter_t s_filters[WF5805F_SENSOR_COUNT];
static WaterDepth_State_t s_state;
static uint8_t s_jump_ref_valid;
static uint32_t s_jump_ref_ms;
static int32_t s_jump_ref_basket_mm_x10;
static int32_t s_jump_ref_tank_mm_x10;

static uint8_t WaterDepth_IsSensorValid(WF5805F_Sensor_t sensor)
{
	return ((uint8_t)sensor < (uint8_t)WF5805F_SENSOR_COUNT);
}

static void WaterDepth_FilterReset(WaterDepth_Filter_t *filter)
{
	uint8_t i;

	for (i = 0U; i < BOARD_WATER_FILTER_SAMPLES; i++)
	{
		filter->samples[i] = 0L;
	}

	filter->index = 0U;
	filter->count = 0U;
	filter->last_timestamp_ms = 0U;
}

static void WaterDepth_FilterAdd(WaterDepth_Filter_t *filter,
                                 int32_t pressure_hpa_x100,
                                 uint32_t timestamp_ms)
{
	if (timestamp_ms == filter->last_timestamp_ms)
	{
		return;
	}

	filter->samples[filter->index] = pressure_hpa_x100;
	filter->index++;
	if (filter->index >= BOARD_WATER_FILTER_SAMPLES)
	{
		filter->index = 0U;
	}

	if (filter->count < BOARD_WATER_FILTER_SAMPLES)
	{
		filter->count++;
	}

	filter->last_timestamp_ms = timestamp_ms;
}

static uint8_t WaterDepth_FilterAverage(const WaterDepth_Filter_t *filter,
                                        int32_t *pressure_hpa_x100)
{
	uint8_t i;
	int64_t sum;

	if ((filter == 0) || (pressure_hpa_x100 == 0) || (filter->count == 0U))
	{
		return 0U;
	}

	sum = 0LL;
	for (i = 0U; i < filter->count; i++)
	{
		sum += filter->samples[i];
	}

	*pressure_hpa_x100 = (int32_t)(sum / (int64_t)filter->count);
	return 1U;
}

static int32_t WaterDepth_Abs(int32_t value)
{
	return (value < 0L) ? -value : value;
}

static void WaterDepth_CheckSensorFailures(void)
{
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_AIR) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		ErrorManager_Set(ERROR_CODE_E_SENSOR_AIR_FAIL);
	}
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_TANK) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		ErrorManager_Set(ERROR_CODE_E_SENSOR_TANK_FAIL);
	}
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_BASKET) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		ErrorManager_Set(ERROR_CODE_E_SENSOR_BASKET_FAIL);
	}

	if (WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_AIR) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT)
	{
		ErrorManager_Set(ERROR_CODE_E_I2C_A_FAIL);
	}
	if (WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_BASKET) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT)
	{
		ErrorManager_Set(ERROR_CODE_E_I2C_B_FAIL);
	}
	if (WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_TANK) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT)
	{
		ErrorManager_Set(ERROR_CODE_E_I2C_C_FAIL);
	}
}

static void WaterDepth_UpdateFilterFromSensor(WF5805F_Sensor_t sensor)
{
	WF5805F_Reading_t reading;

	if (!WaterDepth_IsSensorValid(sensor))
	{
		return;
	}

	if ((WF5805F_GetReading(sensor, &reading) == WF5805F_OK) &&
	    (reading.valid != 0U))
	{
		WaterDepth_FilterAdd(&s_filters[sensor],
		                     reading.pressure_hpa_x100,
		                     reading.timestamp_ms);
	}
}

static void WaterDepth_CheckRanges(void)
{
	if (s_state.tank_depth_mm_x10 < ((int32_t)BOARD_TANK_MIN_DEPTH_MM * 10L))
	{
		ErrorManager_Set(ERROR_CODE_E_TANK_LOW);
	}
	if (s_state.tank_depth_mm_x10 > ((int32_t)BOARD_TANK_MAX_DEPTH_MM * 10L))
	{
		ErrorManager_Set(ERROR_CODE_E_TANK_HIGH);
	}
	if (s_state.basket_depth_mm_x10 < ((int32_t)BOARD_BASKET_MIN_SAFE_DEPTH_MM * 10L))
	{
		ErrorManager_Set(ERROR_CODE_E_BASKET_LOW);
	}
	if (s_state.basket_depth_mm_x10 > ((int32_t)BOARD_BASKET_MAX_SAFE_DEPTH_MM * 10L))
	{
		ErrorManager_Set(ERROR_CODE_E_BASKET_HIGH);
	}
}

static void WaterDepth_CheckPhysical(void)
{
	if ((s_state.basket_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10) ||
	    (s_state.tank_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10))
	{
		ErrorManager_Set(ERROR_CODE_E_PRESSURE_PHYSICAL);
	}
}

static void WaterDepth_CheckJump(uint32_t now_ms)
{
	int32_t basket_delta;
	int32_t tank_delta;
	int32_t threshold_x10;

	threshold_x10 = (int32_t)BOARD_WATER_JUMP_MM_PER_MIN * 10L;

	if (s_jump_ref_valid == 0U)
	{
		s_jump_ref_valid = 1U;
		s_jump_ref_ms = now_ms;
		s_jump_ref_basket_mm_x10 = s_state.basket_depth_mm_x10;
		s_jump_ref_tank_mm_x10 = s_state.tank_depth_mm_x10;
		return;
	}

	if ((uint32_t)(now_ms - s_jump_ref_ms) < 60000U)
	{
		return;
	}

	basket_delta = WaterDepth_Abs(s_state.basket_depth_mm_x10 - s_jump_ref_basket_mm_x10);
	tank_delta = WaterDepth_Abs(s_state.tank_depth_mm_x10 - s_jump_ref_tank_mm_x10);

	if ((basket_delta > threshold_x10) || (tank_delta > threshold_x10))
	{
		ErrorManager_Set(ERROR_CODE_E_WATER_JUMP);
	}

	s_jump_ref_ms = now_ms;
	s_jump_ref_basket_mm_x10 = s_state.basket_depth_mm_x10;
	s_jump_ref_tank_mm_x10 = s_state.tank_depth_mm_x10;
}

int32_t WaterDepth_ConvertPressureDiffToMmX10(int32_t diff_hpa_x100)
{
	return (int32_t)(((int64_t)diff_hpa_x100 * 10197LL) / 10000LL);
}

void WaterDepth_Init(void)
{
	uint8_t i;

	for (i = 0U; i < (uint8_t)WF5805F_SENSOR_COUNT; i++)
	{
		WaterDepth_FilterReset(&s_filters[i]);
	}

	s_state.air_pressure_hpa_x100 = 0L;
	s_state.basket_pressure_hpa_x100 = 0L;
	s_state.tank_pressure_hpa_x100 = 0L;
	s_state.basket_depth_mm_x10 = 0L;
	s_state.tank_depth_mm_x10 = 0L;
	s_state.valid = 0U;
	s_state.timestamp_ms = 0U;
	s_jump_ref_valid = 0U;
	s_jump_ref_ms = 0U;
	s_jump_ref_basket_mm_x10 = 0L;
	s_jump_ref_tank_mm_x10 = 0L;
}

void WaterDepth_Update(uint32_t now_ms)
{
	int32_t air;
	int32_t basket;
	int32_t tank;

	WaterDepth_CheckSensorFailures();

	WaterDepth_UpdateFilterFromSensor(WF5805F_SENSOR_AIR);
	WaterDepth_UpdateFilterFromSensor(WF5805F_SENSOR_BASKET);
	WaterDepth_UpdateFilterFromSensor(WF5805F_SENSOR_TANK);

	if ((WaterDepth_FilterAverage(&s_filters[WF5805F_SENSOR_AIR], &air) == 0U) ||
	    (WaterDepth_FilterAverage(&s_filters[WF5805F_SENSOR_BASKET], &basket) == 0U) ||
	    (WaterDepth_FilterAverage(&s_filters[WF5805F_SENSOR_TANK], &tank) == 0U))
	{
		return;
	}

	s_state.air_pressure_hpa_x100 = air;
	s_state.basket_pressure_hpa_x100 = basket;
	s_state.tank_pressure_hpa_x100 = tank;
	s_state.basket_depth_mm_x10 = WaterDepth_ConvertPressureDiffToMmX10(basket - air);
	s_state.tank_depth_mm_x10 = WaterDepth_ConvertPressureDiffToMmX10(tank - air);
	s_state.valid = 1U;
	s_state.timestamp_ms = now_ms;

	WaterDepth_CheckPhysical();
	WaterDepth_CheckRanges();
	WaterDepth_CheckJump(now_ms);
}

WaterDepth_Status_t WaterDepth_GetState(WaterDepth_State_t *state)
{
	if (state == 0)
	{
		return WATER_DEPTH_ERROR_PARAM;
	}

	*state = s_state;
	return (s_state.valid != 0U) ? WATER_DEPTH_OK : WATER_DEPTH_PENDING;
}
