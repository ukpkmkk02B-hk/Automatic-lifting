#include "homing.h"
#include "board_config.h"
#include "error_manager.h"
#include "limit.h"
#include "position_tracker.h"
#include "stepper_um244.h"
#include "water_depth.h"
#include "wf5805f.h"

static Homing_State_t s_state;
static Homing_Status_t s_last_status;
static ErrorCode_t s_last_error_code;
static uint32_t s_phase_search_pulses;
static uint32_t s_total_search_pulses;
static uint32_t s_release_deadline_ms;

// 函    数：Homing_TimeElapsed
// 参    数：now_ms 当前毫秒时间戳；deadline_ms 到期时间戳。
// 返 回 值：达到或超过到期时间返回 1，否则返回 0。
// 注意事项：使用有符号差值判断，允许毫秒计数回绕。
static uint8_t Homing_TimeElapsed(uint32_t now_ms, uint32_t deadline_ms)
{
	return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1U : 0U;
}

// 水位趋势速度可能为正负，维护回零只关心鱼缸水位是否仍在快速变化。
static int32_t Homing_Abs32(int32_t value)
{
	return (value < 0L) ? -value : value;
}

// 当前传感器连续失败仍达到阈值时才阻塞维护回零；旧锁存告警不在这里自动清除。
static ErrorCode_t Homing_GetActiveSensorFaultCode(void)
{
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_AIR) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		return ERROR_CODE_E_SENSOR_AIR_FAIL;
	}
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_TANK) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		return ERROR_CODE_E_SENSOR_TANK_FAIL;
	}
	if (WF5805F_GetFailureCount(WF5805F_SENSOR_BASKET) >= BOARD_SENSOR_FAILURE_LIMIT)
	{
		return ERROR_CODE_E_SENSOR_BASKET_FAIL;
	}
	return ERROR_CODE_E_NONE;
}

// 当前 I2C 总线恢复失败仍达到阈值时才阻塞维护回零；自动运行仍受锁存故障约束。
static ErrorCode_t Homing_GetActiveI2CFaultCode(void)
{
	if (WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_AIR) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT)
	{
		return ERROR_CODE_E_I2C_A_FAIL;
	}
	if (WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_BASKET) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT)
	{
		return ERROR_CODE_E_I2C_B_FAIL;
	}
	if (WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_TANK) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT)
	{
		return ERROR_CODE_E_I2C_C_FAIL;
	}
	return ERROR_CODE_E_NONE;
}

// 维护回零允许已恢复的旧水位锁存存在，但当前水深仍越界或物理异常时必须拒绝移动。
static ErrorCode_t Homing_GetActiveWaterFaultCode(void)
{
	WaterDepth_State_t depth;

	if (WaterDepth_GetState(&depth) != WATER_DEPTH_OK)
	{
		if (ErrorManager_IsActive(ERROR_CODE_E_TANK_LOW) != 0U) { return ERROR_CODE_E_TANK_LOW; }
		if (ErrorManager_IsActive(ERROR_CODE_E_BASKET_LOW) != 0U) { return ERROR_CODE_E_BASKET_LOW; }
		if (ErrorManager_IsActive(ERROR_CODE_E_BASKET_HIGH) != 0U) { return ERROR_CODE_E_BASKET_HIGH; }
		if (ErrorManager_IsActive(ERROR_CODE_E_PRESSURE_PHYSICAL) != 0U) { return ERROR_CODE_E_PRESSURE_PHYSICAL; }
		return ERROR_CODE_E_NONE;
	}

	if (WaterDepth_IsTankLowActive() != 0U) { return ERROR_CODE_E_TANK_LOW; }
	if (depth.basket_depth_mm_x10 < ((int32_t)BOARD_BASKET_MIN_SAFE_DEPTH_MM * 10L)) { return ERROR_CODE_E_BASKET_LOW; }
	if (depth.basket_depth_mm_x10 > ((int32_t)BOARD_BASKET_MAX_SAFE_DEPTH_MM * 10L)) { return ERROR_CODE_E_BASKET_HIGH; }
	if ((depth.basket_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10) ||
	    (depth.tank_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10))
	{
		return ERROR_CODE_E_PRESSURE_PHYSICAL;
	}

	return ERROR_CODE_E_NONE;
}

// 旧 WATER JUMP 锁存不直接阻塞维护回零；只有当前鱼缸水位趋势仍异常或样本不足时才拒绝。
static uint8_t Homing_IsWaterJumpStillActive(uint32_t now_ms)
{
	WaterDepth_Trend_t trend;
	int32_t threshold_x10;

	if (WaterDepth_GetTrend(now_ms,
	                        BOARD_DROP_TREND_WINDOW_MS,
	                        BOARD_DROP_TREND_MIN_SAMPLES,
	                        &trend) != WATER_DEPTH_OK)
	{
		return 1U;
	}
	if (trend.elapsed_ms < BOARD_DROP_STABLE_WAIT_MS)
	{
		return 1U;
	}

	threshold_x10 = (int32_t)BOARD_WATER_JUMP_MM_PER_MIN * 10L;
	return (Homing_Abs32(trend.tank_drop_rate_mm_x10_per_min) > threshold_x10) ? 1U : 0U;
}

// 函    数：Homing_GetBlockingFaultCode
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：当前阻塞回零的首个错误码，E_NONE 表示无阻塞故障。
// 注意事项：维护回零可在旧传感器/I2C/水位锁存已恢复后启动，但不会清除这些锁存告警。
static ErrorCode_t Homing_GetBlockingFaultCode(uint32_t now_ms)
{
	ErrorCode_t code;

	code = Homing_GetActiveSensorFaultCode();
	if (code != ERROR_CODE_E_NONE) { return code; }
	code = Homing_GetActiveI2CFaultCode();
	if (code != ERROR_CODE_E_NONE) { return code; }
	code = Homing_GetActiveWaterFaultCode();
	if (code != ERROR_CODE_E_NONE) { return code; }

	if ((ErrorManager_IsActive(ERROR_CODE_E_WATER_JUMP) != 0U) &&
	    (Homing_IsWaterJumpStillActive(now_ms) != 0U))
	{
		return ERROR_CODE_E_WATER_JUMP;
	}
	if (ErrorManager_IsActive(ERROR_CODE_E_STALL) != 0U) { return ERROR_CODE_E_STALL; }
	if (ErrorManager_IsActive(ERROR_CODE_E_SELF_TEST_FAIL) != 0U) { return ERROR_CODE_E_SELF_TEST_FAIL; }
	if (ErrorManager_IsActive(ERROR_CODE_E_MOTOR_RELEASED) != 0U) { return ERROR_CODE_E_MOTOR_RELEASED; }

	return ERROR_CODE_E_NONE;
}

// 下限位触碰和退离阶段允许左右下限位短暂先后变化，其他阶段仍视为机械不同步。
static uint8_t Homing_IsLowerMismatchAllowed(void)
{
	return ((s_state == HOMING_STATE_START_DOWN_FIRST) ||
	        (s_state == HOMING_STATE_DOWN_FIRST) ||
	        (s_state == HOMING_STATE_START_BACKOFF) ||
	        (s_state == HOMING_STATE_BACKOFF_UP) ||
	        (s_state == HOMING_STATE_WAIT_RELEASE) ||
	        (s_state == HOMING_STATE_START_DOWN_SECOND) ||
	        (s_state == HOMING_STATE_DOWN_SECOND) ||
	        (s_state == HOMING_STATE_START_FINAL_BACKOFF) ||
	        (s_state == HOMING_STATE_FINAL_BACKOFF_UP) ||
	        (s_state == HOMING_STATE_WAIT_FINAL_RELEASE)) ? 1U : 0U;
}

// 函    数：Homing_Fail
// 参    数：status 回零失败状态；code 需要锁存的错误码，E_NONE 表示不额外置位。
// 返 回 值：无
// 注意事项：进入回零流程后的任何失败都先停止 STEP，再锁存错误并把位置标记为不可信。
static void Homing_Fail(Homing_Status_t status, ErrorCode_t code)
{
	StepperUM244_Stop();
	if (code != ERROR_CODE_E_NONE)
	{
		ErrorManager_Set(code);
	}
	PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_NOT_HOMED);
	s_last_status = status;
	s_last_error_code = code;
	s_state = HOMING_STATE_FAULT;
}

// 函    数：Homing_BlockStart
// 参    数：status 启动被拒绝状态；code 需要锁存的错误码，E_NONE 表示不额外置位。
// 返 回 值：无
// 注意事项：启动预检失败尚未进入回零流程，不能把已有可信位置改为不可信。
static void Homing_BlockStart(Homing_Status_t status, ErrorCode_t code)
{
	StepperUM244_Stop();
	if (code != ERROR_CODE_E_NONE)
	{
		ErrorManager_Set(code);
	}
	s_last_status = status;
	s_last_error_code = code;
	s_state = HOMING_STATE_FAULT;
}

// 函    数：Homing_CheckSafety
// 参    数：now_ms 当前系统毫秒时间戳；mark_position_untrusted 非 0 表示已经进入回零流程。
// 返 回 值：安全条件满足返回 1；发现故障并进入 FAULT 返回 0。
// 注意事项：启动预检失败只拒绝本次启动；电机释放、限位或阻塞故障都不能绕过。
static uint8_t Homing_CheckSafety(uint32_t now_ms, uint8_t mark_position_untrusted)
{
	ErrorCode_t blocking_code;
	uint8_t lower_mismatch_allowed;

	if (StepperUM244_IsMotorReleased() != 0U)
	{
		// MF 释放时不能回零；预检阶段只拒绝启动，不主动改回保持状态。
		if (mark_position_untrusted != 0U)
		{
			Homing_Fail(HOMING_STATUS_ERROR_FAULT, ERROR_CODE_E_MOTOR_RELEASED);
		}
		else
		{
			Homing_BlockStart(HOMING_STATUS_ERROR_FAULT, ERROR_CODE_E_MOTOR_RELEASED);
		}
		return 0U;
	}

	lower_mismatch_allowed = Homing_IsLowerMismatchAllowed();
	if ((mark_position_untrusted == 0U) && (Limit_IsAnyLowerActive() != 0U))
	{
		// 启动时若已经压在单侧下限位，允许进入退离流程，由 2mm 退离确认能否释放。
		lower_mismatch_allowed = 1U;
	}
	if ((Limit_IsUpperMismatch() != 0U) ||
	    ((Limit_IsLowerMismatch() != 0U) && (lower_mismatch_allowed == 0U)))
	{
		// 上限位不一致始终故障；下限位不一致只在触碰/退离阶段允许短暂存在。
		if (mark_position_untrusted != 0U)
		{
			Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LIMIT_MISMATCH);
			PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_LIMIT_MISMATCH);
		}
		else
		{
			Homing_BlockStart(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LIMIT_MISMATCH);
		}
		return 0U;
	}

	if (Limit_IsAnyUpperActive() != 0U)
	{
		// 回零流程不应从上限位触发状态继续，避免向上退限位时撞限。
		if (mark_position_untrusted != 0U)
		{
			Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_UPPER_LIMIT);
		}
		else
		{
			Homing_BlockStart(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_UPPER_LIMIT);
		}
		return 0U;
	}

	blocking_code = Homing_GetBlockingFaultCode(now_ms);
	if (blocking_code != ERROR_CODE_E_NONE)
	{
		if (mark_position_untrusted != 0U)
		{
			Homing_Fail(HOMING_STATUS_ERROR_FAULT, blocking_code);
		}
		else
		{
			Homing_BlockStart(HOMING_STATUS_ERROR_FAULT, blocking_code);
		}
		return 0U;
	}

	return 1U;
}

// 函    数：Homing_StartDownChunk
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：回零运动启动结果。
// 注意事项：向下搜索允许下限位作为预期停止条件，同时设置最大脉冲块防止无限运动。
static Homing_Status_t Homing_StartDownChunk(uint32_t now_ms)
{
	StepperUM244_Status_t status;

	status = StepperUM244_StartUntilLimit(STEPPER_UM244_DIRECTION_DOWN,
	                                     BOARD_HOMING_SEARCH_CHUNK_PULSES,
	                                     BOARD_STEPPER_HOMING_FREQ_HZ,
	                                     now_ms);
	if (status == STEPPER_UM244_STATUS_OK)
	{
		return HOMING_STATUS_OK;
	}
	if (status == STEPPER_UM244_STATUS_BUSY)
	{
		return HOMING_STATUS_BUSY;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_LIMIT)
	{
		return HOMING_STATUS_ERROR_LIMIT;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_FAULT)
	{
		return HOMING_STATUS_ERROR_FAULT;
	}

	return HOMING_STATUS_ERROR_PARAM;
}

// 函    数：Homing_StartBackoff
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：回零运动启动结果。
// 注意事项：下限位触发后上升 2mm，释放机械限位再做第二次慢速靠近或完成退离。
static Homing_Status_t Homing_StartBackoff(uint32_t now_ms)
{
	StepperUM244_Status_t status;

	status = StepperUM244_StartHomingBackoff(BOARD_HOMING_BACKOFF_PULSES,
	                                        BOARD_STEPPER_HOMING_FREQ_HZ,
	                                        now_ms);
	if (status == STEPPER_UM244_STATUS_OK)
	{
		return HOMING_STATUS_OK;
	}
	if (status == STEPPER_UM244_STATUS_BUSY)
	{
		return HOMING_STATUS_BUSY;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_LIMIT)
	{
		return HOMING_STATUS_ERROR_LIMIT;
	}
	if (status == STEPPER_UM244_STATUS_ERROR_FAULT)
	{
		return HOMING_STATUS_ERROR_FAULT;
	}

	return HOMING_STATUS_ERROR_PARAM;
}

// 函    数：Homing_HandleDownComplete
// 参    数：next_when_limit 预期触发下限位后的下一状态。
// 返 回 值：无
// 注意事项：根据步进停止原因判断是进入下一阶段、继续搜索还是进入故障。
static void Homing_HandleDownComplete(Homing_State_t next_when_limit)
{
	StepperUM244_StopReason_t reason;

	reason = StepperUM244_GetStopReason();
	if (reason == STEPPER_UM244_STOP_EXPECTED_LIMIT)
	{
		// 按预期触发下限位，进入下一回零阶段。
		s_phase_search_pulses = 0U;
		s_state = next_when_limit;
		return;
	}

	if (reason == STEPPER_UM244_STOP_PULSE_DONE)
	{
		// 当前搜索块未触发下限位，累计搜索距离并继续下一块。
		s_phase_search_pulses += StepperUM244_GetCompletedPulses();
		s_total_search_pulses += StepperUM244_GetCompletedPulses();
		if ((s_phase_search_pulses >= BOARD_HOMING_MAX_SEARCH_PULSES) ||
		    (s_total_search_pulses >= (BOARD_HOMING_MAX_SEARCH_PULSES * 2U)))
		{
			// 超过机械行程仍未找到下限位，认为位置不可建立。
			Homing_Fail(HOMING_STATUS_ERROR_TIMEOUT, ERROR_CODE_E_POSITION_UNTRUSTED);
		}
		else
		{
			s_state = (next_when_limit == HOMING_STATE_START_BACKOFF) ?
			          HOMING_STATE_START_DOWN_FIRST :
			          HOMING_STATE_START_DOWN_SECOND;
		}
		return;
	}

	if (reason == STEPPER_UM244_STOP_MISMATCH_FAULT)
	{
		// 搜索过程中出现左右限位不一致，零点不可用。
		Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LIMIT_MISMATCH);
		return;
	}

	Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LOWER_LIMIT);
}

// 函    数：Homing_Init
// 参    数：无
// 返 回 值：无
// 注意事项：初始化回零状态机为空闲，清除本次搜索计数。
void Homing_Init(void)
{
	s_state = HOMING_STATE_IDLE;
	s_last_status = HOMING_STATUS_OK;
	s_last_error_code = ERROR_CODE_E_NONE;
	s_phase_search_pulses = 0U;
	s_total_search_pulses = 0U;
	s_release_deadline_ms = 0U;
}

// 函    数：Homing_Start
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：回零启动状态。
// 注意事项：安全预检包含电机保持状态；进入回零流程后直到二次触发下限位完成前位置都不可信。
Homing_Status_t Homing_Start(uint32_t now_ms)
{
	if ((s_state != HOMING_STATE_IDLE) &&
	    (s_state != HOMING_STATE_COMPLETE) &&
	    (s_state != HOMING_STATE_FAULT) &&
	    (s_state != HOMING_STATE_CANCELLED))
	{
		return HOMING_STATUS_BUSY;
	}

	if (Homing_CheckSafety(now_ms, 0U) == 0U)
	{
		return s_last_status;
	}

	// 安全预检通过后才进入回零流程；直到二次触发下限位完成前，位置都不可信。
	PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_HOMING_STARTED);

	s_phase_search_pulses = 0U;
	s_total_search_pulses = 0U;
	s_release_deadline_ms = now_ms;
	s_last_status = HOMING_STATUS_BUSY;
	s_last_error_code = ERROR_CODE_E_NONE;

	if (Limit_IsAnyLowerActive() != 0U)
	{
		// 已经压在下限位上时，先退 2mm 释放，再二次靠近。
		s_state = HOMING_STATE_START_BACKOFF;
	}
	else
	{
		s_state = HOMING_STATE_START_DOWN_FIRST;
	}

	return HOMING_STATUS_OK;
}

// 函    数：Homing_Update
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：非阻塞推进维护回零状态机，所有实际运动都通过 stepper_um244 有限脉冲接口执行。
void Homing_Update(uint32_t now_ms)
{
	Homing_Status_t status;

	if ((s_state == HOMING_STATE_IDLE) ||
	    (s_state == HOMING_STATE_COMPLETE) ||
	    (s_state == HOMING_STATE_FAULT) ||
	    (s_state == HOMING_STATE_CANCELLED))
	{
		return;
	}

	if (Homing_CheckSafety(now_ms, 1U) == 0U)
	{
		return;
	}

	switch (s_state)
	{
	case HOMING_STATE_START_DOWN_FIRST:
		// 第一次下降用于找到下限位或确认当前未在零点附近。
		status = Homing_StartDownChunk(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_DOWN_FIRST;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_LOWER_LIMIT);
		}
		break;

	case HOMING_STATE_DOWN_FIRST:
		if (StepperUM244_IsBusy() == 0U)
		{
			// 第一次下降结束后，根据停止原因决定退限位或继续搜索。
			Homing_HandleDownComplete(HOMING_STATE_START_BACKOFF);
		}
		break;

	case HOMING_STATE_START_BACKOFF:
		// 从下限位上退开 2mm，为第二次靠近消除机械压靠误差。
		status = Homing_StartBackoff(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_BACKOFF_UP;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_UPPER_LIMIT);
		}
		break;

	case HOMING_STATE_BACKOFF_UP:
		if (StepperUM244_IsBusy() == 0U)
		{
			if (StepperUM244_GetStopReason() != STEPPER_UM244_STOP_PULSE_DONE)
			{
				Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_UPPER_LIMIT);
			}
			else
			{
				s_release_deadline_ms = now_ms + BOARD_HOMING_RELEASE_WAIT_MS;
				s_state = HOMING_STATE_WAIT_RELEASE;
			}
		}
		break;

	case HOMING_STATE_WAIT_RELEASE:
		if (Limit_IsAnyLowerActive() == 0U)
		{
			// 下限位已释放，开始第二次慢速下降建立零点。
			s_phase_search_pulses = 0U;
			s_state = HOMING_STATE_START_DOWN_SECOND;
		}
		else if (Homing_TimeElapsed(now_ms, s_release_deadline_ms) != 0U)
		{
			// 上升 2mm 后仍未释放下限位，说明机械或限位异常。
			Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LOWER_LIMIT);
		}
		else
		{
		}
		break;

	case HOMING_STATE_START_DOWN_SECOND:
		// 第二次下降触发下限位后，才认为零点可用。
		status = Homing_StartDownChunk(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_DOWN_SECOND;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_LOWER_LIMIT);
		}
		break;

	case HOMING_STATE_DOWN_SECOND:
		if (StepperUM244_IsBusy() == 0U)
		{
			// 第二次触发下限位后，还要退离 2mm，避免完成后持续压住限位。
			Homing_HandleDownComplete(HOMING_STATE_START_FINAL_BACKOFF);
		}
		break;

	case HOMING_STATE_START_FINAL_BACKOFF:
		status = Homing_StartBackoff(now_ms);
		if (status == HOMING_STATUS_OK)
		{
			s_state = HOMING_STATE_FINAL_BACKOFF_UP;
		}
		else if (status != HOMING_STATUS_BUSY)
		{
			Homing_Fail(status, ERROR_CODE_E_UPPER_LIMIT);
		}
		break;

	case HOMING_STATE_FINAL_BACKOFF_UP:
		if (StepperUM244_IsBusy() == 0U)
		{
			if (StepperUM244_GetStopReason() != STEPPER_UM244_STOP_PULSE_DONE)
			{
				Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_UPPER_LIMIT);
			}
			else
			{
				s_release_deadline_ms = now_ms + BOARD_HOMING_RELEASE_WAIT_MS;
				s_state = HOMING_STATE_WAIT_FINAL_RELEASE;
			}
		}
		break;

	case HOMING_STATE_WAIT_FINAL_RELEASE:
		if (Limit_IsAnyLowerActive() == 0U)
		{
			// 最终退离下限位后，机械 0 点已找到，当前位置记录为 +2mm。
			PositionTracker_MarkHomedWithOffset(BOARD_HOMING_BACKOFF_PULSES);
			s_last_status = HOMING_STATUS_OK;
			s_last_error_code = ERROR_CODE_E_NONE;
			s_state = HOMING_STATE_COMPLETE;
		}
		else if (Homing_TimeElapsed(now_ms, s_release_deadline_ms) != 0U)
		{
			Homing_Fail(HOMING_STATUS_ERROR_LIMIT, ERROR_CODE_E_LOWER_LIMIT);
		}
		else
		{
		}
		break;

	default:
		Homing_Fail(HOMING_STATUS_ERROR_FAULT, ERROR_CODE_E_NONE);
		break;
	}
}

// 函    数：Homing_Cancel
// 参    数：无
// 返 回 值：无
// 注意事项：取消回零后不能沿用中途位置估算，必须重新回零或人工确认。
void Homing_Cancel(void)
{
	if (Homing_IsBusy() != 0U)
	{
		// 取消回零后不能沿用中途位置估算。
		StepperUM244_Stop();
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_NOT_HOMED);
		s_last_status = HOMING_STATUS_CANCELLED;
		s_state = HOMING_STATE_CANCELLED;
	}
}

// 函    数：Homing_IsBusy
// 参    数：无
// 返 回 值：1 表示回零正在执行，0 表示空闲/完成/故障/取消。
// 注意事项：供上层避免重复启动回零。
uint8_t Homing_IsBusy(void)
{
	return ((s_state != HOMING_STATE_IDLE) &&
	        (s_state != HOMING_STATE_COMPLETE) &&
	        (s_state != HOMING_STATE_FAULT) &&
	        (s_state != HOMING_STATE_CANCELLED)) ? 1U : 0U;
}

// 函    数：Homing_IsComplete
// 参    数：无
// 返 回 值：1 表示回零完成，0 表示未完成。
// 注意事项：完成时已找到机械 0 点，并用退离下限位后的 2mm 偏移标记可信位置。
uint8_t Homing_IsComplete(void)
{
	return (s_state == HOMING_STATE_COMPLETE) ? 1U : 0U;
}

// 函    数：Homing_GetState
// 参    数：无
// 返 回 值：当前回零状态。
// 注意事项：用于 OLED 或调试页面显示状态机阶段。
Homing_State_t Homing_GetState(void)
{
	return s_state;
}

// 函    数：Homing_GetLastStatus
// 参    数：无
// 返 回 值：最近一次回零结果。
// 注意事项：故障或取消后用于上层显示原因。
Homing_Status_t Homing_GetLastStatus(void)
{
	return s_last_status;
}

// 函    数：Homing_GetLastErrorCode
// 参    数：无
// 返 回 值：最近一次回零失败或启动被拒绝的错误码，E_NONE 表示无错误。
// 注意事项：用于维护调试页显示真实根因，不改变任何故障锁存状态。
ErrorCode_t Homing_GetLastErrorCode(void)
{
	return s_last_error_code;
}

// 函    数：Homing_GetSearchPulses
// 参    数：无
// 返 回 值：本次回零累计搜索脉冲数。
// 注意事项：用于诊断行程、限位接线或机械异常。
uint32_t Homing_GetSearchPulses(void)
{
	return s_total_search_pulses;
}
