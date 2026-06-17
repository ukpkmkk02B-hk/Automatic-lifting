#include "nap_scheduler.h"
#include "board_config.h"

static ParamStore_Record_t s_record;
static uint32_t s_next_nap_ms;
static uint32_t s_interval_ms;
static uint16_t s_stall_accum_pulses;
static uint8_t s_stall_ref_valid;
static int32_t s_stall_ref_depth_mm_x10;
static uint8_t s_stall_failure_count;

// 限制单次打盹脉冲数；0 pulse 视为参数损坏并退回默认值。
static uint16_t NapScheduler_ClampNapPulses(uint16_t pulses)
{
	if (pulses == 0U)
	{
		return BOARD_NAP_DEFAULT_PULSES;
	}
	if (pulses > BOARD_NAP_MAX_PULSES)
	{
		return BOARD_NAP_MAX_PULSES;
	}
	return pulses;
}

// 将每日变浅量 mm_x10/day 换算成今日自动变浅 pulse 预算。
static uint32_t NapScheduler_GetDailyBudgetPulsesFromRecord(const ParamStore_Record_t *record)
{
	if ((record == 0) || (record->daily_shallow_mm_x10 <= 0L))
	{
		return 0UL;
	}

	return ((uint32_t)record->daily_shallow_mm_x10 *
	        BOARD_STEPPER_PULSE_PER_MM) / 10UL;
}

// 函    数：NapScheduler_ComputeIntervalMs
// 参    数：record 当前参数记录。
// 返 回 值：打盹间隔，单位 ms。
// 注意事项：按 daily_shallow_mm_x10/day 和 pulse/mm 计算；最小值受 BOARD_NAP_MIN_INTERVAL_MS 保护。
static uint32_t NapScheduler_ComputeIntervalMs(const ParamStore_Record_t *record)
{
	uint32_t pulses_per_day;
	uint32_t interval_ms;
	uint16_t nap_pulses;

	if ((record == 0) || (record->daily_shallow_mm_x10 <= 0L))
	{
		return BOARD_NAP_MIN_INTERVAL_MS;
	}

	nap_pulses = NapScheduler_ClampNapPulses(record->nap_pulses);
	pulses_per_day = NapScheduler_GetDailyBudgetPulsesFromRecord(record);
	if (pulses_per_day == 0UL)
	{
		return BOARD_NAP_MIN_INTERVAL_MS;
	}

	interval_ms = (BOARD_SECONDS_PER_DAY * 1000UL * (uint32_t)nap_pulses) /
	              pulses_per_day;
	if (interval_ms < BOARD_NAP_MIN_INTERVAL_MS)
	{
		interval_ms = BOARD_NAP_MIN_INTERVAL_MS;
	}

	return interval_ms;
}

// 函    数：NapScheduler_CheckStallTrend
// 参    数：direction 自动运动方向；pulses 本次完成脉冲；before/after 运动前后框篮水深，单位 mm_x10。
// 返 回 值：记录结果。
// 注意事项：只在累计达到 1mm 后判断趋势，避免 8 pulse 微动被传感器噪声误判。
static NapScheduler_RecordResult_t NapScheduler_CheckStallTrend(StepperUM244_Direction_t direction,
                                                                uint16_t pulses,
                                                                int32_t before_depth_mm_x10,
                                                                int32_t after_depth_mm_x10)
{
	int32_t expected_delta;
	int32_t actual_delta;
	NapScheduler_RecordResult_t result;

	if (s_stall_ref_valid == 0U)
	{
		s_stall_ref_valid = 1U;
		s_stall_ref_depth_mm_x10 = before_depth_mm_x10;
	}

	s_stall_accum_pulses = (uint16_t)(s_stall_accum_pulses + pulses);
	if (s_stall_accum_pulses < BOARD_STALL_CHECK_PULSES)
	{
		return NAP_SCHEDULER_RECORD_OK;
	}

	if (direction == STEPPER_UM244_DIRECTION_UP)
	{
		// 上升应让框篮可活动水深变浅，因此参考水深 - 当前水深应为正。
		actual_delta = s_stall_ref_depth_mm_x10 - after_depth_mm_x10;
	}
	else
	{
		actual_delta = after_depth_mm_x10 - s_stall_ref_depth_mm_x10;
	}

	expected_delta = BOARD_STALL_EXPECTED_DELTA_MM_X10;
	if (actual_delta < expected_delta)
	{
		if (s_stall_failure_count < 0xFFU)
		{
			s_stall_failure_count++;
		}
	}
	else
	{
		s_stall_failure_count = 0U;
	}

	s_stall_accum_pulses = 0U;
	s_stall_ref_depth_mm_x10 = after_depth_mm_x10;
	result = (s_stall_failure_count >= BOARD_STALL_FAILURE_LIMIT) ?
	         NAP_SCHEDULER_RECORD_STALL_FAULT : NAP_SCHEDULER_RECORD_CHECKPOINT;
	return result;
}

// 函    数：NapScheduler_UpdateConfig
// 参    数：record 当前参数/恢复记录；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：拷贝记录用于显示和调度；不直接写回 Flash，避免频繁擦写。
void NapScheduler_UpdateConfig(const ParamStore_Record_t *record, uint32_t now_ms)
{
	if (record != 0)
	{
		s_record = *record;
	}
	else
	{
		ParamStore_LoadDefaults(&s_record);
	}
	s_record.nap_pulses = NapScheduler_ClampNapPulses(s_record.nap_pulses);
	s_interval_ms = NapScheduler_ComputeIntervalMs(&s_record);
	s_next_nap_ms = now_ms + s_interval_ms;
}

// 函    数：NapScheduler_SyncRuntime
// 参    数：record 当前运行记录。
// 返 回 值：无
// 注意事项：不改变 next_nap_ms，避免每秒刷新运行秒数时推迟下一次打盹。
void NapScheduler_SyncRuntime(const ParamStore_Record_t *record)
{
	if (record == 0)
	{
		return;
	}

	s_record.total_run_seconds = record->total_run_seconds;
	s_record.today_run_seconds = record->today_run_seconds;
	s_record.today_pulses_done = record->today_pulses_done;
	s_record.basket_position_pulses = record->basket_position_pulses;
	s_record.position_trusted = record->position_trusted;
}

// 函    数：NapScheduler_Init
// 参    数：record 当前参数/恢复记录；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
void NapScheduler_Init(const ParamStore_Record_t *record, uint32_t now_ms)
{
	s_stall_accum_pulses = 0U;
	s_stall_ref_valid = 0U;
	s_stall_ref_depth_mm_x10 = 0L;
	s_stall_failure_count = 0U;
	NapScheduler_UpdateConfig(record, now_ms);
}

// 函    数：NapScheduler_GetTargetDepthMmX10
// 参    数：无
// 返 回 值：当前目标水深，单位 mm_x10。
// 注意事项：无 RTC，目标只随累计上电运行秒数推进，断电期间不会追赶。
int32_t NapScheduler_GetTargetDepthMmX10(void)
{
	int64_t shallow_mm_x10;
	int32_t target;

	shallow_mm_x10 = ((int64_t)s_record.total_run_seconds *
	                  (int64_t)s_record.daily_shallow_mm_x10) /
	                 (int64_t)BOARD_SECONDS_PER_DAY;
	target = s_record.initial_target_mm_x10 - (int32_t)shallow_mm_x10;
	if (target < s_record.final_target_mm_x10)
	{
		target = s_record.final_target_mm_x10;
	}

	return target;
}

// 函    数：NapScheduler_HasDailyBudget
// 参    数：无
// 返 回 值：1 表示今天仍有自动打盹脉冲额度。
uint8_t NapScheduler_HasDailyBudget(void)
{
	return (NapScheduler_GetDailyRemainingPulses() != 0UL) ? 1U : 0U;
}

// 函    数：NapScheduler_GetDailyRemainingPulses
// 参    数：无
// 返 回 值：今日自动变浅剩余 pulse 额度。
// 注意事项：只代表可继续让框篮上升变浅的额度；下降补深和快速跟随不消耗该额度。
uint32_t NapScheduler_GetDailyRemainingPulses(void)
{
	uint32_t daily_budget;

	daily_budget = NapScheduler_GetDailyBudgetPulsesFromRecord(&s_record);
	if (s_record.today_pulses_done >= daily_budget)
	{
		return 0UL;
	}

	return daily_budget - s_record.today_pulses_done;
}

// 函    数：NapScheduler_IsDue
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：到达下一次打盹时间且仍有今日额度返回 1。
uint8_t NapScheduler_IsDue(uint32_t now_ms)
{
	if (NapScheduler_HasDailyBudget() == 0U)
	{
		return 0U;
	}

	return ((int32_t)(now_ms - s_next_nap_ms) >= 0) ? 1U : 0U;
}

// 函    数：NapScheduler_GetNextPulses
// 参    数：无
// 返 回 值：本次应输出的打盹脉冲数，单位 pulse。
// 注意事项：返回值限制在单次配置和今日剩余额度内。
uint16_t NapScheduler_GetNextPulses(void)
{
	uint32_t daily_budget;
	uint32_t remaining;
	uint16_t pulses;

	daily_budget = NapScheduler_GetDailyBudgetPulsesFromRecord(&s_record);
	if (s_record.today_pulses_done >= daily_budget)
	{
		return 0U;
	}

	remaining = daily_budget - s_record.today_pulses_done;
	pulses = NapScheduler_ClampNapPulses(s_record.nap_pulses);
	if ((uint32_t)pulses > remaining)
	{
		pulses = (uint16_t)remaining;
	}

	return pulses;
}

// 判断本次运动是否计入每日自动变浅额度；只有向上变浅动作消耗预算。
static uint8_t NapScheduler_ShouldCountDaily(MotionSource_t source,
                                             StepperUM244_Direction_t direction)
{
	if (direction != STEPPER_UM244_DIRECTION_UP)
	{
		return 0U;
	}

	return ((source == MOTION_SOURCE_DAILY_SHALLOW) ||
	        (source == MOTION_SOURCE_DEPTH_TRACK_UP)) ? 1U : 0U;
}

// 判断本次自动运动是否需要纳入卡滞趋势检查；手动、回零和 DROP 不走此累计窗口。
static uint8_t NapScheduler_ShouldCheckStall(MotionSource_t source)
{
	return ((source == MOTION_SOURCE_DAILY_SHALLOW) ||
	        (source == MOTION_SOURCE_DEPTH_TRACK_UP) ||
	        (source == MOTION_SOURCE_DEPTH_TRACK_DOWN)) ? 1U : 0U;
}

// 函    数：NapScheduler_RecordMove
// 参    数：record 运行记录；source 运动来源；direction 实际方向；pulses 完成脉冲；
//           before_depth_mm_x10/after_depth_mm_x10 运动前后框篮水深。
// 返 回 值：记录结果。
// 注意事项：只有每日变浅和低频向上修正计入 today_pulses_done；完成后重新安排下一次打盹。
NapScheduler_RecordResult_t NapScheduler_RecordMove(ParamStore_Record_t *record,
                                                    MotionSource_t source,
                                                    StepperUM244_Direction_t direction,
                                                    uint16_t pulses,
                                                    int32_t before_depth_mm_x10,
                                                    int32_t after_depth_mm_x10,
                                                    uint32_t now_ms)
{
	NapScheduler_RecordResult_t result;

	if ((record == 0) || (pulses == 0U))
	{
		NapScheduler_ResetNext(now_ms);
		return NAP_SCHEDULER_RECORD_OK;
	}

	if (NapScheduler_ShouldCountDaily(source, direction) != 0U)
	{
		// today_pulses_done 只记录“自动变浅 pulse”，下降补深、DROP、手动和回零都不写入。
		record->today_pulses_done += (uint32_t)pulses;
		s_record.today_pulses_done = record->today_pulses_done;
	}

	if (NapScheduler_ShouldCheckStall(source) != 0U)
	{
		result = NapScheduler_CheckStallTrend(direction,
		                                      pulses,
		                                      before_depth_mm_x10,
		                                      after_depth_mm_x10);
	}
	else
	{
		result = NAP_SCHEDULER_RECORD_OK;
	}
	NapScheduler_ResetNext(now_ms);
	return result;
}

// 函    数：NapScheduler_OnDayRollover
// 参    数：record 运行记录；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：运行日按上电秒数切换，清零当天 pulse 进度和卡滞累计。
void NapScheduler_OnDayRollover(ParamStore_Record_t *record, uint32_t now_ms)
{
	if (record != 0)
	{
		record->today_run_seconds = 0UL;
		record->today_pulses_done = 0UL;
		s_record = *record;
	}
	else
	{
		s_record.today_run_seconds = 0UL;
		s_record.today_pulses_done = 0UL;
	}

	s_stall_accum_pulses = 0U;
	s_stall_ref_valid = 0U;
	NapScheduler_ResetNext(now_ms);
}

// 函    数：NapScheduler_ResetNext
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：下一次打盹只从当前时间向后排，不追赶错过的自动运动。
void NapScheduler_ResetNext(uint32_t now_ms)
{
	s_next_nap_ms = now_ms + s_interval_ms;
}

// 函    数：NapScheduler_GetDisplay
// 参    数：now_ms 当前系统毫秒时间戳；auto_wait_enabled 是否显示倒计时；out 输出结构体。
// 返 回 值：无
// 注意事项：显示数据来自调度缓存，不启动 STEP。
void NapScheduler_GetDisplay(uint32_t now_ms, uint8_t auto_wait_enabled, NapScheduler_Display_t *out)
{
	uint32_t remaining_ms;

	if (out == 0)
	{
		return;
	}

	out->target_depth_mm_x10 = NapScheduler_GetTargetDepthMmX10();
	out->nap_pulses = s_record.nap_pulses;
	out->today_done_pulses = (s_record.today_pulses_done > 999UL) ?
	                         999U : (uint16_t)s_record.today_pulses_done;
	out->next_nap_valid = auto_wait_enabled;
	out->next_nap_remaining_s = 0UL;

	if ((auto_wait_enabled == 0U) || (NapScheduler_HasDailyBudget() == 0U))
	{
		return;
	}

	if ((int32_t)(now_ms - s_next_nap_ms) >= 0)
	{
		return;
	}

	remaining_ms = s_next_nap_ms - now_ms;
	out->next_nap_remaining_s = (remaining_ms + 999UL) / 1000UL;
}

// 函    数：NapScheduler_GetIntervalMs
// 参    数：无
// 返 回 值：当前打盹间隔，单位 ms。
uint32_t NapScheduler_GetIntervalMs(void)
{
	return s_interval_ms;
}
