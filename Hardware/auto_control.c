#include "auto_control.h"
#include "board_config.h"

typedef enum
{
	AUTO_TRACK_IDLE = 0,
	AUTO_TRACK_STABLE
} AutoControl_TrackState_t;

typedef enum
{
	AUTO_DROP_IDLE = 0,
	AUTO_DROP_ACTIVE
} AutoControl_DropState_t;

typedef enum
{
	AUTO_TRACK_RESULT_IDLE = 0,
	AUTO_TRACK_RESULT_BLOCK_DAILY,
	AUTO_TRACK_RESULT_MOVE,
	AUTO_TRACK_RESULT_FAULT
} AutoControl_TrackResult_t;

static AutoControl_TrackState_t s_track_state;
static AutoControl_DropState_t s_drop_state;
static AutoControl_Display_t s_display;
static uint32_t s_track_next_check_ms;
static uint32_t s_track_stable_start_ms;
static uint32_t s_track_last_sample_ms;
static int32_t s_track_last_depth_mm_x10;
static uint8_t s_track_stable_count;
static uint8_t s_track_failure_count;
static uint32_t s_track_hour_start_ms;
static uint32_t s_track_hour_pulses;
static uint32_t s_track_day_start_ms;
static uint32_t s_track_day_pulses;
static uint32_t s_drop_start_ms;
static uint32_t s_drop_distance_pulses;
static uint32_t s_drop_stable_start_ms;
static int32_t s_drop_hold_basket_mm_x10;
static uint8_t s_drop_notice;

static int32_t AutoControl_Abs32(int32_t value)
{
	return (value < 0L) ? -value : value;
}

static uint16_t AutoControl_LimitU16(uint32_t value, uint16_t max_value)
{
	return (value > (uint32_t)max_value) ? max_value : (uint16_t)value;
}

static uint16_t AutoControl_PulsesFromMmX10(int32_t mm_x10)
{
	uint32_t abs_mm_x10;
	uint32_t pulses;

	abs_mm_x10 = (uint32_t)AutoControl_Abs32(mm_x10);
	pulses = (abs_mm_x10 * BOARD_STEPPER_PULSE_PER_MM) / 10UL;
	return AutoControl_LimitU16(pulses, 0xFFFFU);
}

static void AutoControl_ClearDecision(AutoControl_Decision_t *decision)
{
	if (decision == 0)
	{
		return;
	}

	decision->type = AUTO_CONTROL_DECISION_NONE;
	decision->direction = STEPPER_UM244_DIRECTION_UP;
	decision->pulses = 0U;
	decision->frequency_hz = BOARD_STEPPER_AUTO_FREQ_HZ;
	decision->source = MOTION_SOURCE_DAILY_SHALLOW;
	decision->error_code = ERROR_CODE_E_NONE;
	decision->display = s_display;
	decision->check_water_notice = 0U;
}

static void AutoControl_ResetTrack(uint32_t now_ms)
{
	s_track_state = AUTO_TRACK_IDLE;
	s_track_next_check_ms = now_ms + BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS;
	s_track_stable_start_ms = 0UL;
	s_track_last_sample_ms = 0UL;
	s_track_last_depth_mm_x10 = 0L;
	s_track_stable_count = 0U;
	s_track_failure_count = 0U;
}

static void AutoControl_ResetDrop(void)
{
	s_drop_state = AUTO_DROP_IDLE;
	s_drop_start_ms = 0UL;
	s_drop_distance_pulses = 0UL;
	s_drop_stable_start_ms = 0UL;
	s_drop_hold_basket_mm_x10 = 0L;
}

static void AutoControl_ServiceTrackWindows(uint32_t now_ms)
{
	if ((uint32_t)(now_ms - s_track_hour_start_ms) >= 3600000UL)
	{
		s_track_hour_start_ms = now_ms;
		s_track_hour_pulses = 0UL;
	}
	if ((uint32_t)(now_ms - s_track_day_start_ms) >= (BOARD_SECONDS_PER_DAY * 1000UL))
	{
		s_track_day_start_ms = now_ms;
		s_track_day_pulses = 0UL;
	}
}

static uint8_t AutoControl_TrackBudgetAllows(uint16_t pulses)
{
	if ((s_track_hour_pulses + (uint32_t)pulses) > BOARD_DEPTH_TRACK_HOUR_LIMIT_PULSES)
	{
		return 0U;
	}
	if ((s_track_day_pulses + (uint32_t)pulses) > BOARD_DEPTH_TRACK_DAY_LIMIT_PULSES)
	{
		return 0U;
	}
	return 1U;
}

static void AutoControl_StartDropFromTrend(const WaterDepth_Trend_t *trend, uint32_t now_ms)
{
	s_drop_state = AUTO_DROP_ACTIVE;
	s_drop_start_ms = now_ms;
	s_drop_distance_pulses = 0UL;
	s_drop_stable_start_ms = 0UL;
	s_drop_hold_basket_mm_x10 = trend->oldest_basket_depth_mm_x10;
	s_drop_notice = 0U;
	s_display = AUTO_CONTROL_DISPLAY_DROP;
	// DROP 抢占低频闭环等待/稳定状态，防止两个自动控制源同时争用 STEP。
	AutoControl_ResetTrack(now_ms);
}

static void AutoControl_ServiceDropEntry(uint32_t now_ms,
                                         const AutoControl_Input_t *input,
                                         AutoControl_Decision_t *decision)
{
	WaterDepth_Trend_t trend;
	int32_t basket_shallow_mm_x10;
	int32_t entry_rate_x10;
	int32_t danger_rate_x10;

	if (WaterDepth_GetTrend(now_ms,
	                        BOARD_DROP_TREND_WINDOW_MS,
	                        BOARD_DROP_TREND_MIN_SAMPLES,
	                        &trend) != WATER_DEPTH_OK)
	{
		return;
	}

	entry_rate_x10 = (int32_t)BOARD_DROP_ENTRY_RATE_MM_PER_MIN * 10L;
	danger_rate_x10 = (int32_t)BOARD_DROP_DANGER_RATE_MM_PER_MIN * 10L;
	if (trend.tank_drop_rate_mm_x10_per_min > danger_rate_x10)
	{
		decision->type = AUTO_CONTROL_DECISION_FAULT;
		decision->error_code = ERROR_CODE_E_WATER_JUMP;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
		return;
	}
	if (trend.tank_drop_rate_mm_x10_per_min < entry_rate_x10)
	{
		return;
	}

	basket_shallow_mm_x10 = trend.oldest_basket_depth_mm_x10 - input->depth.basket_depth_mm_x10;
	if (basket_shallow_mm_x10 < BOARD_DROP_START_ERROR_MM_X10)
	{
		// 鱼缸在可跟随速度内下降，但框篮水深没有同步变浅，视为不可跟随突变或传感器安装异常。
		decision->type = AUTO_CONTROL_DECISION_FAULT;
		decision->error_code = ERROR_CODE_E_WATER_JUMP;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
		return;
	}

	AutoControl_StartDropFromTrend(&trend, now_ms);
}

static void AutoControl_ServiceDangerDrop(uint32_t now_ms, AutoControl_Decision_t *decision)
{
	WaterDepth_Trend_t trend;
	int32_t danger_rate_x10;

	if (WaterDepth_GetTrend(now_ms,
	                        BOARD_DROP_TREND_WINDOW_MS,
	                        BOARD_DROP_TREND_MIN_SAMPLES,
	                        &trend) != WATER_DEPTH_OK)
	{
		return;
	}

	danger_rate_x10 = (int32_t)BOARD_DROP_DANGER_RATE_MM_PER_MIN * 10L;
	if (trend.tank_drop_rate_mm_x10_per_min > danger_rate_x10)
	{
		decision->type = AUTO_CONTROL_DECISION_FAULT;
		decision->error_code = ERROR_CODE_E_WATER_JUMP;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
	}
}

static void AutoControl_RequestDropMove(uint32_t now_ms,
                                        const AutoControl_Input_t *input,
                                        AutoControl_Decision_t *decision)
{
	WaterDepth_Trend_t trend;
	int32_t error_mm_x10;
	int32_t stable_rate_x10;
	uint16_t pulses;

	s_display = AUTO_CONTROL_DISPLAY_DROP;
	if ((uint32_t)(now_ms - s_drop_start_ms) >= BOARD_DROP_HARD_MAX_TIME_MS)
	{
		decision->type = AUTO_CONTROL_DECISION_FAULT;
		decision->error_code = ERROR_CODE_E_WATER_JUMP;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
		return;
	}
	if ((uint32_t)(now_ms - s_drop_start_ms) >= BOARD_DROP_MAX_CONT_TIME_MS)
	{
		s_drop_notice = 1U;
		decision->type = AUTO_CONTROL_DECISION_PAUSE;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
		decision->check_water_notice = 1U;
		return;
	}
	if (s_drop_distance_pulses >= BOARD_DROP_MAX_DISTANCE_PULSES)
	{
		s_drop_notice = 1U;
		decision->type = AUTO_CONTROL_DECISION_PAUSE;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
		decision->check_water_notice = 1U;
		return;
	}

	error_mm_x10 = s_drop_hold_basket_mm_x10 - input->depth.basket_depth_mm_x10;
	if (error_mm_x10 > BOARD_DROP_STOP_ERROR_MM_X10)
	{
		pulses = AutoControl_PulsesFromMmX10(error_mm_x10);
		if (pulses < BOARD_DROP_MIN_STEP_PULSES)
		{
			pulses = BOARD_DROP_MIN_STEP_PULSES;
		}
		if (pulses > BOARD_DROP_MAX_STEP_PULSES)
		{
			pulses = BOARD_DROP_MAX_STEP_PULSES;
		}

		decision->type = AUTO_CONTROL_DECISION_MOVE;
		decision->direction = STEPPER_UM244_DIRECTION_DOWN;
		decision->pulses = pulses;
		decision->frequency_hz = BOARD_DROP_FREQ_HZ;
		decision->source = MOTION_SOURCE_DROP_FOLLOW;
		decision->display = AUTO_CONTROL_DISPLAY_DROP;
		return;
	}

	if (WaterDepth_GetTrend(now_ms,
	                        BOARD_DROP_TREND_WINDOW_MS,
	                        BOARD_DROP_TREND_MIN_SAMPLES,
	                        &trend) != WATER_DEPTH_OK)
	{
		return;
	}

	stable_rate_x10 = (int32_t)BOARD_DROP_STABLE_RATE_MM_PER_MIN * 10L;
	if (trend.tank_drop_rate_mm_x10_per_min <= stable_rate_x10)
	{
		if (s_drop_stable_start_ms == 0UL)
		{
			s_drop_stable_start_ms = now_ms;
		}
		if ((uint32_t)(now_ms - s_drop_stable_start_ms) >= BOARD_DROP_STABLE_WAIT_MS)
		{
			s_drop_notice = 1U;
			decision->type = AUTO_CONTROL_DECISION_PAUSE;
			decision->display = AUTO_CONTROL_DISPLAY_DROP;
			decision->check_water_notice = 1U;
		}
	}
	else
	{
		s_drop_stable_start_ms = 0UL;
	}
}

static AutoControl_TrackResult_t AutoControl_RecordTrackFailure(uint32_t now_ms,
                                                                AutoControl_Decision_t *decision)
{
	if (s_track_failure_count < 0xFFU)
	{
		s_track_failure_count++;
	}

	if (s_track_failure_count >= BOARD_DEPTH_TRACK_MAX_FAILURES)
	{
		decision->type = AUTO_CONTROL_DECISION_FAULT;
		decision->error_code = ERROR_CODE_E_DEPTH_TRACKING;
		decision->display = AUTO_CONTROL_DISPLAY_TRACK;
		return AUTO_TRACK_RESULT_FAULT;
	}

	s_track_state = AUTO_TRACK_IDLE;
	s_track_next_check_ms = now_ms;
	s_display = AUTO_CONTROL_DISPLAY_TRACK;
	return AUTO_TRACK_RESULT_BLOCK_DAILY;
}

static AutoControl_TrackResult_t AutoControl_ServiceTrackStable(uint32_t now_ms,
                                                                const AutoControl_Input_t *input,
                                                                AutoControl_Decision_t *decision)
{
	int32_t delta;
	int32_t error_mm_x10;

	s_display = AUTO_CONTROL_DISPLAY_TRACK;
	if ((uint32_t)(now_ms - s_track_last_sample_ms) >= BOARD_WATER_TREND_SAMPLE_MS)
	{
		delta = input->depth.basket_depth_mm_x10 - s_track_last_depth_mm_x10;
		if (AutoControl_Abs32(delta) <= BOARD_DEPTH_TRACK_STABLE_DELTA_MM_X10)
		{
			if (s_track_stable_count < 0xFFU)
			{
				s_track_stable_count++;
			}
		}
		else
		{
			s_track_stable_count = 0U;
		}
		s_track_last_depth_mm_x10 = input->depth.basket_depth_mm_x10;
		s_track_last_sample_ms = now_ms;
	}

	if (((uint32_t)(now_ms - s_track_stable_start_ms) < BOARD_DEPTH_TRACK_STABLE_WAIT_MS) ||
	    (s_track_stable_count < BOARD_DEPTH_TRACK_STABLE_SAMPLES))
	{
		if ((uint32_t)(now_ms - s_track_stable_start_ms) < BOARD_DEPTH_TRACK_VERIFY_TIMEOUT_MS)
		{
			return AUTO_TRACK_RESULT_BLOCK_DAILY;
		}
		return AutoControl_RecordTrackFailure(now_ms, decision);
	}

	error_mm_x10 = input->depth.basket_depth_mm_x10 - input->target_depth_mm_x10;
	if (AutoControl_Abs32(error_mm_x10) <= BOARD_DEPTH_TRACK_STOP_DEADBAND_MM_X10)
	{
		s_track_failure_count = 0U;
		AutoControl_ResetTrack(now_ms);
		s_display = AUTO_CONTROL_DISPLAY_AUTO;
		return AUTO_TRACK_RESULT_IDLE;
	}
	if (AutoControl_Abs32(error_mm_x10) < BOARD_DEPTH_TRACK_START_DEADBAND_MM_X10)
	{
		s_track_failure_count = 0U;
		s_track_state = AUTO_TRACK_IDLE;
		s_track_next_check_ms = now_ms + BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS;
		s_display = AUTO_CONTROL_DISPLAY_TRACK;
		return AUTO_TRACK_RESULT_BLOCK_DAILY;
	}

	return AutoControl_RecordTrackFailure(now_ms, decision);
}

static AutoControl_TrackResult_t AutoControl_RequestTrackMove(uint32_t now_ms,
                                                              const AutoControl_Input_t *input,
                                                              AutoControl_Decision_t *decision)
{
	int32_t error_mm_x10;
	int32_t abs_error_mm_x10;
	uint16_t pulses;

	error_mm_x10 = input->depth.basket_depth_mm_x10 - input->target_depth_mm_x10;
	abs_error_mm_x10 = AutoControl_Abs32(error_mm_x10);
	if (abs_error_mm_x10 > BOARD_DEPTH_TRACK_HARD_ERROR_MM_X10)
	{
		decision->type = AUTO_CONTROL_DECISION_FAULT;
		decision->error_code = ERROR_CODE_E_DEPTH_TRACKING;
		decision->display = AUTO_CONTROL_DISPLAY_TRACK;
		return AUTO_TRACK_RESULT_FAULT;
	}
	if (abs_error_mm_x10 <= BOARD_DEPTH_TRACK_STOP_DEADBAND_MM_X10)
	{
		s_display = AUTO_CONTROL_DISPLAY_AUTO;
		return AUTO_TRACK_RESULT_IDLE;
	}
	if ((int32_t)(now_ms - s_track_next_check_ms) < 0)
	{
		s_display = AUTO_CONTROL_DISPLAY_TRACK;
		return AUTO_TRACK_RESULT_BLOCK_DAILY;
	}
	if (abs_error_mm_x10 < BOARD_DEPTH_TRACK_START_DEADBAND_MM_X10)
	{
		s_track_next_check_ms = now_ms + BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS;
		s_display = AUTO_CONTROL_DISPLAY_TRACK;
		return AUTO_TRACK_RESULT_BLOCK_DAILY;
	}

	pulses = (abs_error_mm_x10 >= 30L) ?
	         BOARD_DEPTH_TRACK_MAX_STEP_PULSES :
	         BOARD_DEPTH_TRACK_DEFAULT_STEP_PULSES;
	if (pulses < BOARD_DEPTH_TRACK_MIN_STEP_PULSES)
	{
		pulses = BOARD_DEPTH_TRACK_MIN_STEP_PULSES;
	}

	if (error_mm_x10 > 0L)
	{
		if (input->daily_remaining_pulses == 0UL)
		{
			s_track_next_check_ms = now_ms + BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS;
			s_display = AUTO_CONTROL_DISPLAY_TRACK;
			return AUTO_TRACK_RESULT_BLOCK_DAILY;
		}
		if ((uint32_t)pulses > input->daily_remaining_pulses)
		{
			pulses = (uint16_t)input->daily_remaining_pulses;
		}
		if (pulses < BOARD_DEPTH_TRACK_MIN_STEP_PULSES)
		{
			s_track_next_check_ms = now_ms + BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS;
			s_display = AUTO_CONTROL_DISPLAY_TRACK;
			return AUTO_TRACK_RESULT_BLOCK_DAILY;
		}
		if (AutoControl_TrackBudgetAllows(pulses) == 0U)
		{
			s_track_next_check_ms = now_ms + BOARD_DEPTH_TRACK_CHECK_INTERVAL_MS;
			s_display = AUTO_CONTROL_DISPLAY_TRACK;
			return AUTO_TRACK_RESULT_BLOCK_DAILY;
		}
		decision->direction = STEPPER_UM244_DIRECTION_UP;
		decision->source = MOTION_SOURCE_DEPTH_TRACK_UP;
	}
	else
	{
		decision->direction = STEPPER_UM244_DIRECTION_DOWN;
		decision->source = MOTION_SOURCE_DEPTH_TRACK_DOWN;
	}

	s_display = AUTO_CONTROL_DISPLAY_TRACK;
	decision->type = AUTO_CONTROL_DECISION_MOVE;
	decision->pulses = pulses;
	decision->frequency_hz = BOARD_DEPTH_TRACK_FREQ_HZ;
	decision->display = AUTO_CONTROL_DISPLAY_TRACK;
	return AUTO_TRACK_RESULT_MOVE;
}

static void AutoControl_RequestNapMove(const AutoControl_Input_t *input,
                                       AutoControl_Decision_t *decision)
{
	if ((input->nap_due == 0U) || (input->nap_pulses == 0U))
	{
		return;
	}

	s_display = AUTO_CONTROL_DISPLAY_AUTO;
	decision->type = AUTO_CONTROL_DECISION_MOVE;
	decision->direction = STEPPER_UM244_DIRECTION_UP;
	decision->pulses = input->nap_pulses;
	decision->frequency_hz = BOARD_STEPPER_AUTO_FREQ_HZ;
	decision->source = MOTION_SOURCE_DAILY_SHALLOW;
	decision->display = AUTO_CONTROL_DISPLAY_AUTO;
}

void AutoControl_Init(uint32_t now_ms)
{
	s_track_hour_start_ms = now_ms;
	s_track_hour_pulses = 0UL;
	s_track_day_start_ms = now_ms;
	s_track_day_pulses = 0UL;
	s_drop_notice = 0U;
	s_display = AUTO_CONTROL_DISPLAY_AUTO;
	AutoControl_ResetTrack(now_ms);
	AutoControl_ResetDrop();
}

void AutoControl_Reset(uint32_t now_ms)
{
	s_drop_notice = 0U;
	s_display = AUTO_CONTROL_DISPLAY_AUTO;
	AutoControl_ResetTrack(now_ms);
	AutoControl_ResetDrop();
}

void AutoControl_Arbitrate(uint32_t now_ms,
                           const AutoControl_Input_t *input,
                           AutoControl_Decision_t *decision)
{
	AutoControl_TrackResult_t track_result;

	if ((input == 0) || (decision == 0))
	{
		return;
	}
	AutoControl_ClearDecision(decision);

	AutoControl_ServiceTrackWindows(now_ms);
	AutoControl_ServiceDangerDrop(now_ms, decision);
	if (decision->type == AUTO_CONTROL_DECISION_FAULT)
	{
		return;
	}

	if (s_drop_state == AUTO_DROP_IDLE)
	{
		AutoControl_ServiceDropEntry(now_ms, input, decision);
		if (decision->type == AUTO_CONTROL_DECISION_FAULT)
		{
			return;
		}
	}
	if (s_drop_state == AUTO_DROP_ACTIVE)
	{
		AutoControl_RequestDropMove(now_ms, input, decision);
		return;
	}

	if (s_track_state == AUTO_TRACK_STABLE)
	{
		track_result = AutoControl_ServiceTrackStable(now_ms, input, decision);
		if ((track_result == AUTO_TRACK_RESULT_MOVE) ||
		    (track_result == AUTO_TRACK_RESULT_FAULT) ||
		    (track_result == AUTO_TRACK_RESULT_BLOCK_DAILY))
		{
			return;
		}
	}

	track_result = AutoControl_RequestTrackMove(now_ms, input, decision);
	if ((track_result == AUTO_TRACK_RESULT_MOVE) ||
	    (track_result == AUTO_TRACK_RESULT_FAULT) ||
	    (track_result == AUTO_TRACK_RESULT_BLOCK_DAILY))
	{
		return;
	}

	AutoControl_RequestNapMove(input, decision);
}

void AutoControl_NotifyMoveComplete(MotionSource_t source,
                                    StepperUM244_Direction_t direction,
                                    uint16_t pulses,
                                    const WaterDepth_State_t *before,
                                    const WaterDepth_State_t *after,
                                    uint32_t now_ms)
{
	(void)direction;
	(void)before;

	if ((source == MOTION_SOURCE_DEPTH_TRACK_UP) ||
	    (source == MOTION_SOURCE_DEPTH_TRACK_DOWN))
	{
		s_track_hour_pulses += (uint32_t)pulses;
		s_track_day_pulses += (uint32_t)pulses;
		s_track_state = AUTO_TRACK_STABLE;
		s_track_stable_start_ms = now_ms;
		s_track_last_sample_ms = now_ms;
		s_track_stable_count = 0U;
		if (after != 0)
		{
			s_track_last_depth_mm_x10 = after->basket_depth_mm_x10;
		}
		s_display = AUTO_CONTROL_DISPLAY_TRACK;
	}
	else if (source == MOTION_SOURCE_DROP_FOLLOW)
	{
		s_drop_distance_pulses += (uint32_t)pulses;
		s_display = AUTO_CONTROL_DISPLAY_DROP;
	}
	else
	{
		s_display = AUTO_CONTROL_DISPLAY_AUTO;
	}
}

void AutoControl_NotifyMoveAbort(MotionSource_t source, uint32_t now_ms)
{
	if ((source == MOTION_SOURCE_DEPTH_TRACK_UP) ||
	    (source == MOTION_SOURCE_DEPTH_TRACK_DOWN))
	{
		AutoControl_ResetTrack(now_ms);
	}
	else if (source == MOTION_SOURCE_DROP_FOLLOW)
	{
		AutoControl_ResetDrop();
	}
	s_display = AUTO_CONTROL_DISPLAY_AUTO;
}

AutoControl_Display_t AutoControl_GetDisplay(void)
{
	if (s_drop_notice != 0U)
	{
		return AUTO_CONTROL_DISPLAY_DROP;
	}
	return s_display;
}

const char *AutoControl_GetMotionText(void)
{
	switch (AutoControl_GetDisplay())
	{
	case AUTO_CONTROL_DISPLAY_TRACK:
		return "TRK";
	case AUTO_CONTROL_DISPLAY_DROP:
		return "DROP";
	case AUTO_CONTROL_DISPLAY_AUTO:
	default:
		return "AUTO";
	}
}
