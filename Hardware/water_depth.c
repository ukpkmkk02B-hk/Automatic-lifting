#include "water_depth.h"
#include "wf5805f.h"
#include "board_config.h"
#include "error_manager.h"

#define WATER_DEPTH_ZERO_PACK_MARKER      0x50000000UL
#define WATER_DEPTH_ZERO_PACK_MARKER_MASK 0xF0000000UL
#define WATER_DEPTH_ZERO_PACK_BITS        14U
#define WATER_DEPTH_ZERO_PACK_MASK        0x00003FFFUL
#define WATER_DEPTH_ZERO_SIGN_BIT         0x00002000UL
#define WATER_DEPTH_ZERO_SIGN_EXTEND      0x00004000L
#define WATER_DEPTH_ZERO_OFFSET_MIN       (-8192L)
#define WATER_DEPTH_ZERO_OFFSET_MAX       8191L
#define WATER_DEPTH_TANK_LOW_SAMPLE_PENDING  0U
#define WATER_DEPTH_TANK_LOW_SAMPLE_READY    1U
#define WATER_DEPTH_TANK_LOW_SAMPLE_REJECTED 2U

#if (BOARD_TANK_LOW_TRIM_SAMPLES < 3U)
#error "BOARD_TANK_LOW_TRIM_SAMPLES must be at least 3"
#endif

typedef struct
{
	// 最近有效压力样本，单位 hPa_x100。
	int32_t samples[BOARD_WATER_FILTER_SAMPLES];
	uint8_t index;
	uint8_t count;
	// 用时间戳去重，避免同一笔传感器读数重复进入滤波。
	uint32_t last_timestamp_ms;
} WaterDepth_Filter_t;

typedef struct
{
	int32_t basket_depth_mm_x10;
	int32_t tank_depth_mm_x10;
	uint32_t timestamp_ms;
} WaterDepth_TrendSample_t;

static WaterDepth_Filter_t s_filters[WF5805F_SENSOR_COUNT];
static WaterDepth_State_t s_state;
static WaterDepth_TrendSample_t s_trend_samples[BOARD_WATER_TREND_SAMPLES];
static uint8_t s_trend_index;
static uint8_t s_trend_count;
static uint32_t s_last_trend_sample_ms;
static uint8_t s_jump_ref_valid;
static uint32_t s_jump_ref_ms;
static int32_t s_jump_ref_basket_mm_x10;
static int32_t s_jump_ref_tank_mm_x10;
static int32_t s_basket_zero_offset_hpa_x100;
static int32_t s_tank_zero_offset_hpa_x100;
static uint8_t s_basket_motion_active;
static uint8_t s_tank_low_active;
static uint8_t s_tank_low_confirm_count;
static uint8_t s_tank_low_release_count;
static int32_t s_tank_low_samples[BOARD_TANK_LOW_TRIM_SAMPLES];
static uint8_t s_tank_low_sample_index;
static uint8_t s_tank_low_sample_count;
static uint8_t s_tank_low_last_valid;
static int32_t s_tank_low_last_mm_x10;
static uint8_t s_tank_low_last_fault_valid;
static uint32_t s_tank_low_last_fault_ms;

// 函    数：WaterDepth_IsSensorValid
// 参    数：sensor WF5805F 传感器编号。
// 返 回 值：1 表示编号有效，0 表示越界。
// 注意事项：防止访问 s_filters 数组越界。
static uint8_t WaterDepth_IsSensorValid(WF5805F_Sensor_t sensor)
{
	return ((uint8_t)sensor < (uint8_t)WF5805F_SENSOR_COUNT);
}

// 函    数：WaterDepth_FilterReset
// 参    数：filter 需要复位的 5 点压力滤波器。
// 返 回 值：无
// 注意事项：清空样本计数和时间戳，后续必须重新收集有效压力读数。
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

// 函    数：WaterDepth_FilterAdd
// 参    数：filter 压力滤波器；pressure_hpa_x100 有效压力，单位 hPa_x100；
//           timestamp_ms 该压力读数的采样时间戳。
// 返 回 值：无
// 注意事项：同一时间戳样本只入队一次，避免主循环多次读取同一笔传感器数据污染平均值。
static uint8_t WaterDepth_FilterAdd(WaterDepth_Filter_t *filter,
                                    int32_t pressure_hpa_x100,
                                    uint32_t timestamp_ms)
{
	if (timestamp_ms == filter->last_timestamp_ms)
	{
		// 同一传感器样本只允许进入滤波一次。
		return 0U;
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
	return 1U;
}

// 函    数：WaterDepth_FilterAverage
// 参    数：filter 压力滤波器；pressure_hpa_x100 输出平均压力，单位 hPa_x100。
// 返 回 值：1 表示已有至少一个有效样本，0 表示无有效平均值。
// 注意事项：当前实现使用最近最多 5 个有效样本滑动平均，失败读数不会进入样本表。
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

// 函    数：WaterDepth_Abs
// 参    数：value 有符号整数。
// 返 回 值：绝对值。
// 注意事项：用于水位突变比较，输入来自 mm_x10 差值。
static int32_t WaterDepth_Abs(int32_t value)
{
	return (value < 0L) ? -value : value;
}

// 重置低水位专用决策窗口；该窗口只服务 E_TANK_LOW，不改变对外显示的 TNK 水深。
static void WaterDepth_ResetTankLowSamples(void)
{
	uint8_t i;

	for (i = 0U; i < BOARD_TANK_LOW_TRIM_SAMPLES; i++)
	{
		s_tank_low_samples[i] = 0L;
	}
	s_tank_low_sample_index = 0U;
	s_tank_low_sample_count = 0U;
	s_tank_low_last_valid = 0U;
	s_tank_low_last_mm_x10 = 0L;
}

// 记录 P_tank/I2C-C 最近异常；异常恢复冷却期内不推进低水位触发或恢复计数。
static void WaterDepth_RecordTankLowSensorFault(uint32_t now_ms)
{
	s_tank_low_last_fault_ms = now_ms;
	s_tank_low_last_fault_valid = 1U;
	s_tank_low_confirm_count = 0U;
	s_tank_low_release_count = 0U;
	WaterDepth_ResetTankLowSamples();
}

// 判断 P_tank 最近异常后的冷却期是否仍在；使用差值写法兼容 uint32_t 毫秒回绕。
static uint8_t WaterDepth_IsTankLowI2CGraceActive(uint32_t now_ms)
{
	if (s_tank_low_last_fault_valid == 0U)
	{
		return 0U;
	}

	return ((uint32_t)(now_ms - s_tank_low_last_fault_ms) <
	        BOARD_TANK_LOW_I2C_GRACE_MS) ? 1U : 0U;
}

// 加入一个低水位决策样本；明显跳变样本丢弃并重建窗口，下一笔稳定样本作为新基线。
static uint8_t WaterDepth_AddTankLowSample(int32_t tank_depth_mm_x10)
{
	int32_t step_limit_mm_x10;

	step_limit_mm_x10 = (int32_t)BOARD_TANK_LOW_MAX_STEP_MM * 10L;
	if ((s_tank_low_last_valid != 0U) &&
	    (WaterDepth_Abs(tank_depth_mm_x10 - s_tank_low_last_mm_x10) > step_limit_mm_x10))
	{
		WaterDepth_ResetTankLowSamples();
		return WATER_DEPTH_TANK_LOW_SAMPLE_REJECTED;
	}

	s_tank_low_samples[s_tank_low_sample_index] = tank_depth_mm_x10;
	s_tank_low_sample_index++;
	if (s_tank_low_sample_index >= BOARD_TANK_LOW_TRIM_SAMPLES)
	{
		s_tank_low_sample_index = 0U;
	}
	if (s_tank_low_sample_count < BOARD_TANK_LOW_TRIM_SAMPLES)
	{
		s_tank_low_sample_count++;
	}
	s_tank_low_last_valid = 1U;
	s_tank_low_last_mm_x10 = tank_depth_mm_x10;

	return (s_tank_low_sample_count >= BOARD_TANK_LOW_TRIM_SAMPLES) ?
	       WATER_DEPTH_TANK_LOW_SAMPLE_READY :
	       WATER_DEPTH_TANK_LOW_SAMPLE_PENDING;
}

// 最近 5 个可接受 TNK 样本去掉最大/最小值后取平均，避免单个极端点推进严重故障。
static uint8_t WaterDepth_GetTankLowDecisionDepth(int32_t *depth_mm_x10)
{
	uint8_t i;
	int32_t min_value;
	int32_t max_value;
	int64_t sum;

	if ((depth_mm_x10 == 0) ||
	    (s_tank_low_sample_count < BOARD_TANK_LOW_TRIM_SAMPLES))
	{
		return 0U;
	}

	min_value = s_tank_low_samples[0];
	max_value = s_tank_low_samples[0];
	sum = 0LL;
	for (i = 0U; i < BOARD_TANK_LOW_TRIM_SAMPLES; i++)
	{
		if (s_tank_low_samples[i] < min_value)
		{
			min_value = s_tank_low_samples[i];
		}
		if (s_tank_low_samples[i] > max_value)
		{
			max_value = s_tank_low_samples[i];
		}
		sum += s_tank_low_samples[i];
	}

	sum -= min_value;
	sum -= max_value;
	*depth_mm_x10 = (int32_t)(sum / (int64_t)(BOARD_TANK_LOW_TRIM_SAMPLES - 2U));
	return 1U;
}

// 判断低水位专用窗口是否全部低于阈值；用于让首次完整窗口按实际累计样本数折算确认进度。
static uint8_t WaterDepth_AreTankLowSamplesBelow(int32_t threshold_mm_x10)
{
	uint8_t i;

	if (s_tank_low_sample_count < BOARD_TANK_LOW_TRIM_SAMPLES)
	{
		return 0U;
	}

	for (i = 0U; i < BOARD_TANK_LOW_TRIM_SAMPLES; i++)
	{
		if (s_tank_low_samples[i] >= threshold_mm_x10)
		{
			return 0U;
		}
	}

	return 1U;
}

// 将 -8192..8191 的有符号空气零点偏移编码为 14-bit 补码，便于两个偏移共用一个 Flash 字段。
static uint32_t WaterDepth_EncodeS14(int32_t value)
{
	if (value < 0L)
	{
		return (uint32_t)(value + WATER_DEPTH_ZERO_SIGN_EXTEND) & WATER_DEPTH_ZERO_PACK_MASK;
	}

	return (uint32_t)value & WATER_DEPTH_ZERO_PACK_MASK;
}

// 将 14-bit 补码还原为 hPa_x100 偏移值；最高有效位为符号位。
static int32_t WaterDepth_DecodeS14(uint32_t value)
{
	value &= WATER_DEPTH_ZERO_PACK_MASK;
	if ((value & WATER_DEPTH_ZERO_SIGN_BIT) != 0UL)
	{
		return (int32_t)value - WATER_DEPTH_ZERO_SIGN_EXTEND;
	}

	return (int32_t)value;
}

// 按窗口首末水深计算下降速度，单位 mm_x10/min；水位上升时返回负值。
static int32_t WaterDepth_ComputeDropRateMmX10PerMin(int32_t oldest_mm_x10,
                                                     int32_t newest_mm_x10,
                                                     uint32_t elapsed_ms)
{
	int32_t delta_drop_mm_x10;

	if (elapsed_ms == 0UL)
	{
		return 0L;
	}

	// 水深下降时 newest 小于 oldest，下降速度用正数表示，便于快速掉水阈值比较。
	delta_drop_mm_x10 = oldest_mm_x10 - newest_mm_x10;
	return (int32_t)(((int64_t)delta_drop_mm_x10 * 60000LL) /
	                 (int64_t)elapsed_ms);
}

// 按固定采样周期记录趋势环形缓冲，避免主循环高频重复样本放大趋势权重。
static void WaterDepth_RecordTrendSample(uint32_t now_ms)
{
	if ((s_trend_count != 0U) &&
	    ((uint32_t)(now_ms - s_last_trend_sample_ms) < BOARD_WATER_TREND_SAMPLE_MS))
	{
		return;
	}

	s_trend_samples[s_trend_index].basket_depth_mm_x10 = s_state.basket_depth_mm_x10;
	s_trend_samples[s_trend_index].tank_depth_mm_x10 = s_state.tank_depth_mm_x10;
	s_trend_samples[s_trend_index].timestamp_ms = now_ms;
	s_last_trend_sample_ms = now_ms;

	s_trend_index++;
	if (s_trend_index >= BOARD_WATER_TREND_SAMPLES)
	{
		s_trend_index = 0U;
	}
	if (s_trend_count < BOARD_WATER_TREND_SAMPLES)
	{
		s_trend_count++;
	}
}

// 函    数：WaterDepth_CheckSensorFailures
// 参    数：无
// 返 回 值：无
// 注意事项：连续失败达到阈值后置位错误管理器，严重故障由上层停止自动运动。
static void WaterDepth_CheckSensorFailures(void)
{
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_AIR) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		// 空气参考失效会让所有差压水深失去基准，必须停机报警。
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
		// I2C 恢复失败达到阈值后，不能继续等待总线自动恢复。
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

// 函    数：WaterDepth_UpdateFilterFromSensor
// 参    数：sensor WF5805F 传感器编号。
// 参    数：now_ms 当前系统毫秒时间戳，用于记录 P_tank 异常后的低水位确认冷却窗口。
// 返 回 值：无
// 注意事项：只把 WF5805F_OK 且 valid=1 的压力加入滤波，失败读数不参与水深计算。
static uint8_t WaterDepth_UpdateFilterFromSensor(WF5805F_Sensor_t sensor, uint32_t now_ms)
{
	WF5805F_Reading_t reading;
	WF5805F_Status_t status;

	if (!WaterDepth_IsSensorValid(sensor))
	{
		return 0U;
	}

	status = WF5805F_GetReading(sensor, &reading);
	if ((status == WF5805F_OK) && (reading.valid != 0U))
	{
		// 只把有效压力值加入滑动平均，失败读数不会污染水深计算。
		return WaterDepth_FilterAdd(&s_filters[sensor],
		                            reading.pressure_hpa_x100,
		                            reading.timestamp_ms);
	}

	if ((sensor == WF5805F_SENSOR_TANK) &&
	    (s_filters[WF5805F_SENSOR_TANK].count != 0U) &&
	    (status != WF5805F_PENDING))
	{
		// P_tank/I2C-C 最近失败后，低水位严重故障确认暂停一个短冷却窗口。
		WaterDepth_RecordTankLowSensorFault(now_ms);
	}
	return 0U;
}

// 鱼缸低水位使用新有效 P_tank 样本连续确认，避免 250mm 阈值附近的单次跳变直接锁存严重故障。
static void WaterDepth_UpdateTankLowDebounce(uint8_t tank_sample_updated, uint32_t now_ms)
{
	int32_t low_threshold_mm_x10;
	int32_t release_threshold_mm_x10;
	int32_t decision_depth_mm_x10;
	uint8_t sample_status;

	if (tank_sample_updated == 0U)
	{
		return;
	}
	if (WaterDepth_IsTankLowI2CGraceActive(now_ms) != 0U)
	{
		s_tank_low_confirm_count = 0U;
		s_tank_low_release_count = 0U;
		return;
	}

	sample_status = WaterDepth_AddTankLowSample(s_state.tank_depth_mm_x10);
	if (sample_status == WATER_DEPTH_TANK_LOW_SAMPLE_REJECTED)
	{
		s_tank_low_confirm_count = 0U;
		s_tank_low_release_count = 0U;
		return;
	}
	low_threshold_mm_x10 = (int32_t)BOARD_TANK_MIN_DEPTH_MM * 10L;
	release_threshold_mm_x10 =
		((int32_t)BOARD_TANK_MIN_DEPTH_MM + (int32_t)BOARD_TANK_LOW_RELEASE_MARGIN_MM) * 10L;

	if (s_tank_low_active != 0U)
	{
		if (s_state.tank_depth_mm_x10 >= release_threshold_mm_x10)
		{
			if (s_tank_low_release_count < BOARD_TANK_LOW_RELEASE_SAMPLES)
			{
				s_tank_low_release_count++;
			}
			if (s_tank_low_release_count >= BOARD_TANK_LOW_RELEASE_SAMPLES)
			{
				s_tank_low_active = 0U;
				s_tank_low_confirm_count = 0U;
				s_tank_low_release_count = BOARD_TANK_LOW_RELEASE_SAMPLES;
			}
		}
		else
		{
			s_tank_low_release_count = 0U;
		}
		return;
	}

	if (sample_status != WATER_DEPTH_TANK_LOW_SAMPLE_READY)
	{
		return;
	}
	if (WaterDepth_GetTankLowDecisionDepth(&decision_depth_mm_x10) == 0U)
	{
		return;
	}

	if (decision_depth_mm_x10 < low_threshold_mm_x10)
	{
		if (s_tank_low_confirm_count == 0U)
		{
			s_tank_low_confirm_count =
				(WaterDepth_AreTankLowSamplesBelow(low_threshold_mm_x10) != 0U) ?
				BOARD_TANK_LOW_TRIM_SAMPLES :
				1U;
		}
		else if (s_tank_low_confirm_count < BOARD_TANK_LOW_CONFIRM_SAMPLES)
		{
			s_tank_low_confirm_count++;
		}
		if (s_tank_low_confirm_count >= BOARD_TANK_LOW_CONFIRM_SAMPLES)
		{
			s_tank_low_active = 1U;
			s_tank_low_confirm_count = BOARD_TANK_LOW_CONFIRM_SAMPLES;
			s_tank_low_release_count = 0U;
		}
	}
	else
	{
		s_tank_low_confirm_count = 0U;
	}
}

// 函    数：WaterDepth_CheckRanges
// 参    数：无
// 返 回 值：无
// 注意事项：水深单位为 mm_x10，阈值宏为 mm，因此比较前统一乘以 10。
static void WaterDepth_CheckRanges(void)
{
	if (s_tank_low_active != 0U)
	{
		// 鱼缸整体水深低于 250mm，自动运动需要停止。
		// 这里的 active 已经过连续 P_tank 新样本确认，并会保持到 260mm 以上连续恢复。
		ErrorManager_Set(ERROR_CODE_E_TANK_LOW);
	}
	if (s_state.basket_depth_mm_x10 < ((int32_t)BOARD_BASKET_MIN_SAFE_DEPTH_MM * 10L))
	{
		// 框篮水深低于 5mm 会影响鱼的最低安全活动水深。
		ErrorManager_Set(ERROR_CODE_E_BASKET_LOW);
	}
	if (s_state.basket_depth_mm_x10 > ((int32_t)BOARD_BASKET_MAX_SAFE_DEPTH_MM * 10L))
	{
		ErrorManager_Set(ERROR_CODE_E_BASKET_HIGH);
	}
}

// 函    数：WaterDepth_CheckPhysical
// 参    数：无
// 返 回 值：无
// 注意事项：差压换算为明显负水深时，优先认为传感器、封装或安装存在异常。
static void WaterDepth_CheckPhysical(void)
{
	if ((s_state.basket_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10) ||
	    (s_state.tank_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10))
	{
		// 差压换算为明显负水深时，优先认为传感器或安装存在异常。
		ErrorManager_Set(ERROR_CODE_E_PRESSURE_PHYSICAL);
	}
}

// 函    数：WaterDepth_CheckJump
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：以 1 分钟为窗口检查水位突变，阈值单位为 mm/min，内部比较使用 mm_x10。
static void WaterDepth_CheckJump(uint32_t now_ms)
{
	int32_t basket_delta;
	int32_t tank_delta;
	int32_t tank_drop_rate_x10;
	int32_t threshold_x10;
	int32_t danger_rate_x10;
	uint32_t elapsed_ms;

	threshold_x10 = (int32_t)BOARD_WATER_JUMP_MM_PER_MIN * 10L;
	danger_rate_x10 = (int32_t)BOARD_DROP_DANGER_RATE_MM_PER_MIN * 10L;

	if (s_jump_ref_valid == 0U)
	{
		// 首次有效水深作为 1 分钟突变检测基准。
		s_jump_ref_valid = 1U;
		s_jump_ref_ms = now_ms;
		s_jump_ref_basket_mm_x10 = s_state.basket_depth_mm_x10;
		s_jump_ref_tank_mm_x10 = s_state.tank_depth_mm_x10;
		return;
	}

	elapsed_ms = now_ms - s_jump_ref_ms;
	if (s_basket_motion_active != 0U)
	{
		// 回零/手动/自动命令运动会让框篮水深按 0.5..1mm/s 改变。
		// 这里只刷新框篮基准，避免命令运动本身被当作水位突变；鱼缸水位仍按原窗口监测。
		s_jump_ref_basket_mm_x10 = s_state.basket_depth_mm_x10;
	}
	if (elapsed_ms < 60000U)
	{
		// 水位突变阈值单位为 mm/min，未满 1 分钟不做判断。
		return;
	}

	basket_delta = WaterDepth_Abs(s_state.basket_depth_mm_x10 - s_jump_ref_basket_mm_x10);
	tank_delta = WaterDepth_Abs(s_state.tank_depth_mm_x10 - s_jump_ref_tank_mm_x10);
	tank_drop_rate_x10 = WaterDepth_ComputeDropRateMmX10PerMin(s_jump_ref_tank_mm_x10,
	                                                           s_state.tank_depth_mm_x10,
	                                                           elapsed_ms);

	if (tank_drop_rate_x10 > danger_rate_x10)
	{
		// 鱼缸水位下降超过危险阈值时不继续跟随，直接按水位突变故障处理。
		ErrorManager_Set(ERROR_CODE_E_WATER_JUMP);
	}
	else if ((tank_drop_rate_x10 < ((int32_t)BOARD_DROP_ENTRY_RATE_MM_PER_MIN * 10L)) &&
	         ((tank_delta > threshold_x10) ||
	          ((s_basket_motion_active == 0U) && (basket_delta > threshold_x10))))
	{
		// 非快速掉水形态的异常突变仍按故障处理，避免传感器松动或进水被当作可跟随事件。
		ErrorManager_Set(ERROR_CODE_E_WATER_JUMP);
	}

	s_jump_ref_ms = now_ms;
	s_jump_ref_basket_mm_x10 = s_state.basket_depth_mm_x10;
	s_jump_ref_tank_mm_x10 = s_state.tank_depth_mm_x10;
}

// 函    数：WaterDepth_ConvertPressureDiffToMmX10
// 参    数：diff_hpa_x100 压力差，单位 hPa_x100。
// 返 回 值：水深，单位 mm_x10。
// 注意事项：diff_hpa_x100 * 10.197mm/hPa，再输出 mm_x10，避免浮点运算。
int32_t WaterDepth_ConvertPressureDiffToMmX10(int32_t diff_hpa_x100)
{
	return (int32_t)(((int64_t)diff_hpa_x100 * 10197LL) / 10000LL);
}

// 函    数：WaterDepth_SetZeroOffsets
// 参    数：basket_offset_hpa_x100 框篮传感器空气零点偏移，单位 hPa_x100。
// 参    数：tank_offset_hpa_x100 鱼缸传感器空气零点偏移，单位 hPa_x100。
// 返 回 值：无
// 注意事项：只保存 RAM 中的运行校准值；Flash 持久化由 app_state/param_store 负责。
void WaterDepth_SetZeroOffsets(int32_t basket_offset_hpa_x100,
                               int32_t tank_offset_hpa_x100)
{
	s_basket_zero_offset_hpa_x100 = basket_offset_hpa_x100;
	s_tank_zero_offset_hpa_x100 = tank_offset_hpa_x100;
	// 空气零点改变后，旧 TNK 决策样本不能继续参与低水位确认。
	WaterDepth_ResetTankLowSamples();
	s_tank_low_confirm_count = 0U;
	s_tank_low_release_count = 0U;
}

// 函    数：WaterDepth_SetBasketMotionActive
// 参    数：active 非 0 表示 STEP 有限脉冲正在执行，0 表示框篮静止。
// 返 回 值：无
// 注意事项：该标志只影响框篮水深突变误报，不影响 tank_depth 的危险掉水报警。
void WaterDepth_SetBasketMotionActive(uint8_t active)
{
	s_basket_motion_active = (active != 0U) ? 1U : 0U;
}

// 函    数：WaterDepth_PackZeroOffsets
// 参    数：basket_offset_hpa_x100/tank_offset_hpa_x100 空气零点偏移，单位 hPa_x100。
// 参    数：packed 输出带标记的打包值。
// 返 回 值：1 成功，0 失败。
// 注意事项：使用 0x5 高半字节标记，避免把旧版保存的空气绝对压力误当作双零点偏移。
uint8_t WaterDepth_PackZeroOffsets(int32_t basket_offset_hpa_x100,
                                   int32_t tank_offset_hpa_x100,
                                   int32_t *packed)
{
	uint32_t value;

	if (packed == 0)
	{
		return 0U;
	}
	if ((basket_offset_hpa_x100 < WATER_DEPTH_ZERO_OFFSET_MIN) ||
	    (basket_offset_hpa_x100 > WATER_DEPTH_ZERO_OFFSET_MAX) ||
	    (tank_offset_hpa_x100 < WATER_DEPTH_ZERO_OFFSET_MIN) ||
	    (tank_offset_hpa_x100 > WATER_DEPTH_ZERO_OFFSET_MAX))
	{
		return 0U;
	}

	value = WATER_DEPTH_ZERO_PACK_MARKER;
	value |= WaterDepth_EncodeS14(basket_offset_hpa_x100) << WATER_DEPTH_ZERO_PACK_BITS;
	value |= WaterDepth_EncodeS14(tank_offset_hpa_x100);
	*packed = (int32_t)value;
	return 1U;
}

// 函    数：WaterDepth_UnpackZeroOffsets
// 参    数：packed Flash 中保存的打包值。
// 参    数：basket_offset_hpa_x100/tank_offset_hpa_x100 输出空气零点偏移，单位 hPa_x100。
// 返 回 值：1 表示打包校准有效，0 表示旧记录或未校准。
// 注意事项：无有效标记时输出归零，系统仍可运行但同空气偏差不会被补偿。
uint8_t WaterDepth_UnpackZeroOffsets(int32_t packed,
                                     int32_t *basket_offset_hpa_x100,
                                     int32_t *tank_offset_hpa_x100)
{
	uint32_t value;

	if ((basket_offset_hpa_x100 == 0) || (tank_offset_hpa_x100 == 0))
	{
		return 0U;
	}

	value = (uint32_t)packed;
	if ((value & WATER_DEPTH_ZERO_PACK_MARKER_MASK) != WATER_DEPTH_ZERO_PACK_MARKER)
	{
		*basket_offset_hpa_x100 = 0L;
		*tank_offset_hpa_x100 = 0L;
		return 0U;
	}

	*basket_offset_hpa_x100 =
		WaterDepth_DecodeS14((value >> WATER_DEPTH_ZERO_PACK_BITS) & WATER_DEPTH_ZERO_PACK_MASK);
	*tank_offset_hpa_x100 = WaterDepth_DecodeS14(value & WATER_DEPTH_ZERO_PACK_MASK);
	return 1U;
}

// 函    数：WaterDepth_ConvertBasketDiffToMmX10
// 参    数：diff_hpa_x100 实时框篮传感器与空气参考的差压，单位 hPa_x100。
// 返 回 值：扣除维护空气零点后的框篮水深，单位 mm_x10。
// 注意事项：安全控制统一使用该接口，避免固定传感器零偏造成 ±1mm 控制目标失真。
int32_t WaterDepth_ConvertBasketDiffToMmX10(int32_t diff_hpa_x100)
{
	return WaterDepth_ConvertPressureDiffToMmX10(diff_hpa_x100 -
	                                            s_basket_zero_offset_hpa_x100);
}

// 函    数：WaterDepth_ConvertTankDiffToMmX10
// 参    数：diff_hpa_x100 实时鱼缸传感器与空气参考的差压，单位 hPa_x100。
// 返 回 值：扣除维护空气零点后的鱼缸水深，单位 mm_x10。
// 注意事项：只修正固定零点偏移，不改变水深比例和水位安全阈值。
int32_t WaterDepth_ConvertTankDiffToMmX10(int32_t diff_hpa_x100)
{
	return WaterDepth_ConvertPressureDiffToMmX10(diff_hpa_x100 -
	                                            s_tank_zero_offset_hpa_x100);
}

// 函    数：WaterDepth_Init
// 参    数：无
// 返 回 值：无
// 注意事项：复位所有滤波器和状态，初始化后 WaterDepth_GetState 返回 PENDING。
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
	for (i = 0U; i < BOARD_WATER_TREND_SAMPLES; i++)
	{
		s_trend_samples[i].basket_depth_mm_x10 = 0L;
		s_trend_samples[i].tank_depth_mm_x10 = 0L;
		s_trend_samples[i].timestamp_ms = 0UL;
	}
	s_trend_index = 0U;
	s_trend_count = 0U;
	s_last_trend_sample_ms = 0UL;
	s_jump_ref_valid = 0U;
	s_jump_ref_ms = 0U;
	s_jump_ref_basket_mm_x10 = 0L;
	s_jump_ref_tank_mm_x10 = 0L;
	s_basket_zero_offset_hpa_x100 = 0L;
	s_tank_zero_offset_hpa_x100 = 0L;
	s_basket_motion_active = 0U;
	s_tank_low_active = 0U;
	s_tank_low_confirm_count = 0U;
	s_tank_low_release_count = 0U;
	s_tank_low_last_fault_valid = 0U;
	s_tank_low_last_fault_ms = 0UL;
	WaterDepth_ResetTankLowSamples();
}

// 函    数：WaterDepth_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：先检查传感器/I2C 健康，再更新滤波和水深；三路未齐全时不输出半成品水深。
void WaterDepth_Update(uint32_t now_ms)
{
	int32_t air;
	int32_t basket;
	int32_t tank;
	uint8_t tank_sample_updated;

	WaterDepth_CheckSensorFailures();

	WaterDepth_UpdateFilterFromSensor(WF5805F_SENSOR_AIR, now_ms);
	WaterDepth_UpdateFilterFromSensor(WF5805F_SENSOR_BASKET, now_ms);
	tank_sample_updated = WaterDepth_UpdateFilterFromSensor(WF5805F_SENSOR_TANK, now_ms);

	if ((WaterDepth_FilterAverage(&s_filters[WF5805F_SENSOR_AIR], &air) == 0U) ||
	    (WaterDepth_FilterAverage(&s_filters[WF5805F_SENSOR_BASKET], &basket) == 0U) ||
	    (WaterDepth_FilterAverage(&s_filters[WF5805F_SENSOR_TANK], &tank) == 0U))
	{
		// 三路传感器未全部有有效平均值前，不输出半成品水深。
		return;
	}

	s_state.air_pressure_hpa_x100 = air;
	s_state.basket_pressure_hpa_x100 = basket;
	s_state.tank_pressure_hpa_x100 = tank;
	// 维护模式 CAL AIR 会记录三颗同空气时的固定零偏；实时水深先做传感器与空气参考差压，再扣除该零偏。
	s_state.basket_depth_mm_x10 = WaterDepth_ConvertBasketDiffToMmX10(basket - air);
	s_state.tank_depth_mm_x10 = WaterDepth_ConvertTankDiffToMmX10(tank - air);
	s_state.valid = 1U;
	s_state.timestamp_ms = now_ms;

	WaterDepth_UpdateTankLowDebounce(tank_sample_updated, now_ms);
	WaterDepth_RecordTrendSample(now_ms);
	WaterDepth_CheckPhysical();
	WaterDepth_CheckRanges();
	WaterDepth_CheckJump(now_ms);
}

// 函    数：WaterDepth_GetState
// 参    数：state 输出最近一次水深状态。
// 返 回 值：WATER_DEPTH_OK 表示状态有效；WATER_DEPTH_PENDING 表示尚无完整水深；参数非法返回错误。
// 注意事项：调用方必须检查返回值，不能在 PENDING 时使用默认 0 值做运动控制。
WaterDepth_Status_t WaterDepth_GetState(WaterDepth_State_t *state)
{
	if (state == 0)
	{
		return WATER_DEPTH_ERROR_PARAM;
	}

	*state = s_state;
	return (s_state.valid != 0U) ? WATER_DEPTH_OK : WATER_DEPTH_PENDING;
}

// 函    数：WaterDepth_IsTankLowActive
// 参    数：无
// 返 回 值：1 表示当前鱼缸低水位根因仍存在；0 表示已按迟滞和连续样本确认恢复。
// 注意事项：该接口只描述当前根因，不清除 error_manager 中已经锁存的 E_TANK_LOW。
uint8_t WaterDepth_IsTankLowActive(void)
{
	return (s_tank_low_active != 0U) ? 1U : 0U;
}

// 函    数：WaterDepth_GetTrend
// 参    数：now_ms 当前系统毫秒时间戳；window_ms 趋势窗口；min_samples 最小样本数；trend 输出趋势。
// 返 回 值：趋势有效返回 OK；样本不足返回 PENDING；参数非法返回 ERROR_PARAM。
// 注意事项：在固定 1s 趋势样本上寻找窗口内最早样本，避免主循环高速重复读数放大权重。
WaterDepth_Status_t WaterDepth_GetTrend(uint32_t now_ms,
                                        uint32_t window_ms,
                                        uint8_t min_samples,
                                        WaterDepth_Trend_t *trend)
{
	WaterDepth_TrendSample_t newest;
	WaterDepth_TrendSample_t oldest;
	uint8_t newest_index;
	uint8_t i;
	uint8_t idx;
	uint8_t samples;

	if ((trend == 0) || (window_ms == 0UL) || (min_samples == 0U))
	{
		return WATER_DEPTH_ERROR_PARAM;
	}

	trend->valid = 0U;
	trend->sample_count = 0U;
	trend->elapsed_ms = 0UL;
	if ((s_state.valid == 0U) || (s_trend_count < min_samples))
	{
		return WATER_DEPTH_PENDING;
	}

	newest_index = (s_trend_index == 0U) ? (BOARD_WATER_TREND_SAMPLES - 1U) : (uint8_t)(s_trend_index - 1U);
	newest = s_trend_samples[newest_index];
	oldest = newest;
	samples = 1U;

	for (i = 1U; i < s_trend_count; i++)
	{
		idx = (newest_index >= i) ?
		      (uint8_t)(newest_index - i) :
		      (uint8_t)(BOARD_WATER_TREND_SAMPLES + newest_index - i);
		if ((uint32_t)(now_ms - s_trend_samples[idx].timestamp_ms) > window_ms)
		{
			break;
		}
		oldest = s_trend_samples[idx];
		samples++;
	}

	if ((samples < min_samples) || (newest.timestamp_ms == oldest.timestamp_ms))
	{
		return WATER_DEPTH_PENDING;
	}

	trend->valid = 1U;
	trend->sample_count = samples;
	trend->elapsed_ms = newest.timestamp_ms - oldest.timestamp_ms;
	trend->oldest_basket_depth_mm_x10 = oldest.basket_depth_mm_x10;
	trend->newest_basket_depth_mm_x10 = newest.basket_depth_mm_x10;
	trend->oldest_tank_depth_mm_x10 = oldest.tank_depth_mm_x10;
	trend->newest_tank_depth_mm_x10 = newest.tank_depth_mm_x10;
	trend->basket_drop_rate_mm_x10_per_min =
		WaterDepth_ComputeDropRateMmX10PerMin(oldest.basket_depth_mm_x10,
		                                      newest.basket_depth_mm_x10,
		                                      trend->elapsed_ms);
	trend->tank_drop_rate_mm_x10_per_min =
		WaterDepth_ComputeDropRateMmX10PerMin(oldest.tank_depth_mm_x10,
		                                      newest.tank_depth_mm_x10,
		                                      trend->elapsed_ms);
	return WATER_DEPTH_OK;
}
