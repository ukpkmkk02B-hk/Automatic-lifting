#include "app_state.h"
#include "auto_control.h"
#include "board_config.h"
#include "error_code.h"
#include "error_manager.h"
#include "homing.h"
#include "limit.h"
#include "menu.h"
#include "nap_scheduler.h"
#include "param_store.h"
#include "position_tracker.h"
#include "self_test.h"
#include "stepper_um244.h"
#include "water_depth.h"
#include "wf5805f.h"

typedef enum
{
	// 等待运动前的新鲜水深读数，确认传感器链路可用。
	APP_NAP_PHASE_PRE_DEPTH = 0,
	// 发起一次自动打盹有限脉冲。
	APP_NAP_PHASE_START_MOVE,
	// 等待 STEP 底层完成有限脉冲和 DIR 保持时间。
	APP_NAP_PHASE_WAIT_MOVE,
	// 等待运动后的新鲜水深读数，用于超差和卡滞趋势检查。
	APP_NAP_PHASE_POST_DEPTH
} AppState_NapPhase_t;

typedef enum
{
	APP_DEPTH_WAIT = 0,
	APP_DEPTH_READY,
	APP_DEPTH_TIMEOUT
} AppState_DepthResult_t;

static AppState_State_t s_state;
static AppState_State_t s_manual_return_state;
static ParamStore_Record_t s_record;
static uint32_t s_last_second_ms;
static AppState_NapPhase_t s_nap_phase;
static uint32_t s_nap_phase_start_ms;
static WaterDepth_State_t s_nap_before_depth;
static StepperUM244_Direction_t s_nap_direction;
static uint16_t s_nap_pulses;
static uint16_t s_auto_frequency_hz;
static MotionSource_t s_auto_source;
static uint8_t s_auto_move_recorded;
static uint8_t s_auto_position_applied;
static uint8_t s_check_water_notice;
static uint8_t s_drop_recovery_saved;
static uint8_t s_manual_chunk_active;
static StepperUM244_Direction_t s_manual_direction;

static void AppState_ApplyAutoCompletedPartial(uint32_t now_ms);
static void AppState_ApplyManualCompleted(void);

// 函    数：AppState_ToStoredState
// 参    数：state 当前运行态。
// 返 回 值：Flash 恢复记录中的应用状态。
// 注意事项：未知状态保守保存为 PAUSED，避免重启后自动追赶未知运动。
static ParamStore_AppState_t AppState_ToStoredState(AppState_State_t state)
{
	switch (state)
	{
	case APP_STATE_SELF_TEST:
		return PARAM_STORE_APP_SELF_TEST;
	case APP_STATE_AUTO_RUN:
		return PARAM_STORE_APP_AUTO_RUN;
	case APP_STATE_NAP_WAIT:
		return PARAM_STORE_APP_NAP_WAIT;
	case APP_STATE_NAP_MOVE:
		return PARAM_STORE_APP_NAP_MOVE;
	case APP_STATE_MANUAL:
		return PARAM_STORE_APP_MANUAL;
	case APP_STATE_MAINTENANCE:
		return PARAM_STORE_APP_MAINTENANCE;
	case APP_STATE_MOTOR_RELEASE:
		return PARAM_STORE_APP_MOTOR_RELEASE;
	case APP_STATE_FAULT:
		return PARAM_STORE_APP_FAULT;
	case APP_STATE_PAUSED:
	default:
		return PARAM_STORE_APP_PAUSED;
	}
}

// 函    数：AppState_ToUiMode
// 参    数：state 当前运行态。
// 返 回 值：OLED 页面显示模式。
// 注意事项：AUTO_RUN/NAP_WAIT/NAP_MOVE 统一显示为自动页，具体动作由 motion_text 区分。
static UiPages_Mode_t AppState_ToUiMode(AppState_State_t state)
{
	switch (state)
	{
	case APP_STATE_SELF_TEST:
		return UI_PAGES_MODE_SELF_TEST;
	case APP_STATE_MANUAL:
		return UI_PAGES_MODE_MANUAL;
	case APP_STATE_MAINTENANCE:
	case APP_STATE_MOTOR_RELEASE:
		return UI_PAGES_MODE_MAINTENANCE;
	case APP_STATE_FAULT:
		return UI_PAGES_MODE_FAULT;
	case APP_STATE_PAUSED:
		return UI_PAGES_MODE_PAUSED;
	case APP_STATE_AUTO_RUN:
	case APP_STATE_NAP_WAIT:
	case APP_STATE_NAP_MOVE:
	default:
		return UI_PAGES_MODE_AUTO;
	}
}

// 函    数：AppState_GetMotionText
// 参    数：state 当前运行态。
// 返 回 值：4 字符以内 ASCII 运动状态文本。
// 注意事项：DROP/CHECK WATER 恢复提示优先显示，便于人工确认水位和位置。
static const char *AppState_GetMotionText(AppState_State_t state)
{
	switch (state)
	{
	case APP_STATE_SELF_TEST:
		return "TEST";
	case APP_STATE_PAUSED:
	case APP_STATE_FAULT:
		return (s_check_water_notice != 0U) ? "DROP" : "STOP";
	case APP_STATE_NAP_WAIT:
		if (AutoControl_GetDisplay() == AUTO_CONTROL_DISPLAY_TRACK)
		{
			return "TRK";
		}
		if (AutoControl_GetDisplay() == AUTO_CONTROL_DISPLAY_DROP)
		{
			return "DROP";
		}
		return "WAIT";
	case APP_STATE_NAP_MOVE:
		if (s_auto_source == MOTION_SOURCE_DEPTH_TRACK_UP)
		{
			return "TRK";
		}
		if (s_auto_source == MOTION_SOURCE_DEPTH_TRACK_DOWN)
		{
			return "TRK";
		}
		if (s_auto_source == MOTION_SOURCE_DROP_FOLLOW)
		{
			return "DROP";
		}
		return "NAP";
	case APP_STATE_MANUAL:
		return "JOG";
	case APP_STATE_MAINTENANCE:
	case APP_STATE_MOTOR_RELEASE:
		return "IDLE";
	case APP_STATE_AUTO_RUN:
	default:
		return "RUN";
	}
}

// 简单有符号绝对值工具，用于 mm_x10 误差比较。
static int32_t AppState_Abs32(int32_t value)
{
	return (value < 0L) ? -value : value;
}

// 判断 Flash 中的恢复状态是否允许自检后尝试自动恢复。
static uint8_t AppState_IsAutoStoredState(uint8_t state)
{
	return ((state == (uint8_t)PARAM_STORE_APP_AUTO_RUN) ||
	        (state == (uint8_t)PARAM_STORE_APP_NAP_WAIT)) ? 1U : 0U;
}

// 恢复关键状态入口必须先停 STEP，避免自动/手动脉冲跨状态继续输出。
static uint8_t AppState_ShouldStopBeforeEnter(AppState_State_t state)
{
	return ((state == APP_STATE_FAULT) ||
	        (state == APP_STATE_PAUSED) ||
	        (state == APP_STATE_MAINTENANCE) ||
	        (state == APP_STATE_MOTOR_RELEASE)) ? 1U : 0U;
}

// 从 Flash 参数缓存加载运行记录；失败时退回默认值并同步蜂鸣器静音标志。
static void AppState_LoadRecord(void)
{
	if (ParamStore_Load(&s_record) != PARAM_STORE_STATUS_OK)
	{
		ParamStore_LoadDefaults(&s_record);
	}
	ErrorManager_SetBuzzerMuted(s_record.buzzer_muted);
}

// 把当前运行状态、位置和最近水深同步到 s_record，供受控 Flash 保存使用。
static void AppState_SyncRecordRuntime(void)
{
	WaterDepth_State_t depth;

	s_record.last_app_state = (uint8_t)AppState_ToStoredState(s_state);
	s_record.buzzer_muted = ErrorManager_IsBuzzerMuted();
	s_record.position_trusted = PositionTracker_IsTrusted();
	s_record.basket_position_pulses = PositionTracker_GetPulses();
	if (WaterDepth_GetState(&depth) == WATER_DEPTH_OK)
	{
		s_record.last_basket_depth_mm_x10 = depth.basket_depth_mm_x10;
		s_record.last_tank_depth_mm_x10 = depth.tank_depth_mm_x10;
	}
	NapScheduler_SyncRuntime(&s_record);
}

// 按指定恢复状态强制保存运行记录；仅用于关键状态切换或保守恢复标记。
static void AppState_SaveStateAs(ParamStore_AppState_t stored_state, uint32_t now_ms)
{
	AppState_SyncRecordRuntime();
	s_record.last_app_state = (uint8_t)stored_state;
	(void)ParamStore_ForceSaveRuntime(&s_record, now_ms);
}

// 保存当前应用状态；CHECK WATER 场景复用 NAP_MOVE 表示重启后必须人工确认。
static void AppState_SaveState(uint32_t now_ms)
{
	if ((s_check_water_notice != 0U) &&
	    ((s_state == APP_STATE_PAUSED) || (s_state == APP_STATE_FAULT)))
	{
		if (s_drop_recovery_saved == 0U)
		{
			// CHECK WATER 复用 NAP_MOVE 作为保守恢复标记；重启后必须人工检查水位/位置。
			AppState_SaveStateAs(PARAM_STORE_APP_NAP_MOVE, now_ms);
			s_drop_recovery_saved = 1U;
		}
		return;
	}

	AppState_SaveStateAs(AppState_ToStoredState(s_state), now_ms);
}

// 关键状态保存入口，便于集中保留故障/暂停/维护等恢复语义。
static void AppState_SaveCriticalState(uint32_t now_ms)
{
	AppState_SaveState(now_ms);
}

// 按 10min 节流保存普通运行状态，运动中和 CHECK WATER 标记期间禁止覆盖恢复语义。
static void AppState_SaveRuntimeIfDue(uint32_t now_ms)
{
	if ((s_state == APP_STATE_NAP_MOVE) || (StepperUM244_IsBusy() != 0U))
	{
		return;
	}
	if (s_check_water_notice != 0U)
	{
		// CHECK WATER 期间保留 NAP_MOVE/DROP 标记，避免 10min 运行保存覆盖保守恢复语义。
		return;
	}

	if (ParamStore_ShouldSaveRuntime(now_ms) == 0U)
	{
		return;
	}

	AppState_SyncRecordRuntime();
	(void)ParamStore_SaveRuntime(&s_record, now_ms);
}

// 函    数：AppState_Enter
// 参    数：state 目标应用状态；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：暂停/故障/维护/释放为恢复关键状态，进入前停止自动 STEP 并强制保存。
static void AppState_Enter(AppState_State_t state, uint32_t now_ms)
{
	if (s_state == state)
	{
		return;
	}

	if (AppState_ShouldStopBeforeEnter(state) != 0U)
	{
		// 进入恢复关键状态前先切断 STEP；若中断的是手动点动，随后按已输出 pulse 结算位置。
		StepperUM244_Stop();
		AppState_ApplyAutoCompletedPartial(now_ms);
		AppState_ApplyManualCompleted();
	}

	s_state = state;
	if ((state == APP_STATE_FAULT) ||
	    (state == APP_STATE_PAUSED) ||
	    (state == APP_STATE_MAINTENANCE) ||
	    (state == APP_STATE_MOTOR_RELEASE))
	{
		AppState_SaveCriticalState(now_ms);
	}
}

// 每秒累计上电运行时间；日切换只按通电秒数推进，不补偿断电期间错过的时间。
static void AppState_ServiceSeconds(uint32_t now_ms)
{
	if ((uint32_t)(now_ms - s_last_second_ms) < 1000UL)
	{
		return;
	}

	s_last_second_ms += 1000UL;
	s_record.total_run_seconds++;
	s_record.today_run_seconds++;
	if (s_record.today_run_seconds >= BOARD_SECONDS_PER_DAY)
	{
		NapScheduler_OnDayRollover(&s_record, now_ms);
	}
	else
	{
		NapScheduler_SyncRuntime(&s_record);
	}
}

// 函    数：AppState_GetFreshDepth
// 参    数：now_ms 当前时间；start_ms 等待起点；timeout_ms 超时门限；depth 输出水深。
// 返 回 值：等待、已就绪或超时。
// 注意事项：自动运动前后都要求 start_ms 之后的新鲜读数，避免用旧水深判断安全。
static AppState_DepthResult_t AppState_GetFreshDepth(uint32_t now_ms,
                                                     uint32_t start_ms,
                                                     uint32_t timeout_ms,
                                                     WaterDepth_State_t *depth)
{
	WaterDepth_State_t current;

	if (WaterDepth_GetState(&current) == WATER_DEPTH_OK)
	{
		if ((int32_t)(current.timestamp_ms - start_ms) >= 0)
		{
			if (depth != 0)
			{
				*depth = current;
			}
			return APP_DEPTH_READY;
		}
	}

	if ((uint32_t)(now_ms - start_ms) >= timeout_ms)
	{
		return APP_DEPTH_TIMEOUT;
	}

	return APP_DEPTH_WAIT;
}

// 检查当前框篮水深与目标水深的硬超差；超过 ±5mm_x10 阈值时停止自动控制。
static uint8_t AppState_CheckDepthTolerance(void)
{
	WaterDepth_State_t depth;
	int32_t error_mm_x10;

	if (WaterDepth_GetState(&depth) != WATER_DEPTH_OK)
	{
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		return 0U;
	}

	error_mm_x10 = depth.basket_depth_mm_x10 - NapScheduler_GetTargetDepthMmX10();
	if (AppState_Abs32(error_mm_x10) > BOARD_DEPTH_TRACK_HARD_ERROR_MM_X10)
	{
		// 受限闭环允许普通 ±2mm 偏差由低频修正处理，超过 5mm 视为跟踪硬故障。
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		return 0U;
	}

	return 1U;
}

// 第二次报警确认时复查根因；条件仍存在则重新锁存对应错误并阻止清故障。
static uint8_t AppState_BlockFaultClearIf(uint8_t condition, ErrorCode_t code)
{
	if (condition != 0U)
	{
		ErrorManager_Set(code);
		return 1U;
	}

	return 0U;
}

// 函    数：AppState_CanClearLatchedFaults
// 参    数：无
// 返 回 值：1 表示可受控清除严重故障，0 表示根因仍存在。
// 注意事项：静音不等于清故障；限位/I2C/传感器/水深/位置/电机释放都会阻止 ClearAll。
static uint8_t AppState_CanClearLatchedFaults(void)
{
	WaterDepth_State_t depth;
	uint8_t blocked;
	int32_t target_error_mm_x10;

	blocked = 0U;

	// 第二次确认前复查仍可观测的硬件/状态根因；根因还在时只允许静音，不允许 ClearAll()。
	if (AppState_BlockFaultClearIf((StepperUM244_IsMotorReleased() != 0U) ? 1U : 0U,
	                               ERROR_CODE_E_MOTOR_RELEASED) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((Limit_IsSameDirectionMismatch() != 0U) ? 1U : 0U,
	                               ERROR_CODE_E_LIMIT_MISMATCH) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf(((Limit_IsAnyUpperActive() != 0U) ||
	                                (Limit_IsRawDirectionActive(LIMIT_DIRECTION_UP) != 0U)) ? 1U : 0U,
	                               ERROR_CODE_E_UPPER_LIMIT) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf(((Limit_IsAnyLowerActive() != 0U) ||
	                                (Limit_IsRawDirectionActive(LIMIT_DIRECTION_DOWN) != 0U)) ? 1U : 0U,
	                               ERROR_CODE_E_LOWER_LIMIT) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((PositionTracker_CanAutoRun() == 0U) ? 1U : 0U,
	                               ERROR_CODE_E_POSITION_UNTRUSTED) != 0U)
	{
		blocked = 1U;
	}

	if (AppState_BlockFaultClearIf((WF5805F_GetFailureCount(WF5805F_SENSOR_AIR) >= BOARD_SENSOR_FAILURE_LIMIT) ? 1U : 0U,
	                               ERROR_CODE_E_SENSOR_AIR_FAIL) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((WF5805F_GetFailureCount(WF5805F_SENSOR_TANK) >= BOARD_SENSOR_FAILURE_LIMIT) ? 1U : 0U,
	                               ERROR_CODE_E_SENSOR_TANK_FAIL) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((WF5805F_GetFailureCount(WF5805F_SENSOR_BASKET) >= BOARD_SENSOR_FAILURE_LIMIT) ? 1U : 0U,
	                               ERROR_CODE_E_SENSOR_BASKET_FAIL) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_AIR) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT) ? 1U : 0U,
	                               ERROR_CODE_E_I2C_A_FAIL) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_BASKET) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT) ? 1U : 0U,
	                               ERROR_CODE_E_I2C_B_FAIL) != 0U)
	{
		blocked = 1U;
	}
	if (AppState_BlockFaultClearIf((WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_TANK) >= BOARD_I2C_RECOVERY_FAILURE_LIMIT) ? 1U : 0U,
	                               ERROR_CODE_E_I2C_C_FAIL) != 0U)
	{
		blocked = 1U;
	}

	if (WaterDepth_GetState(&depth) != WATER_DEPTH_OK)
	{
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		blocked = 1U;
	}
	else
	{
		if (AppState_BlockFaultClearIf((depth.tank_depth_mm_x10 < ((int32_t)BOARD_TANK_MIN_DEPTH_MM * 10L)) ? 1U : 0U,
		                               ERROR_CODE_E_TANK_LOW) != 0U)
		{
			blocked = 1U;
		}
		if (AppState_BlockFaultClearIf((depth.tank_depth_mm_x10 > ((int32_t)BOARD_TANK_MAX_DEPTH_MM * 10L)) ? 1U : 0U,
		                               ERROR_CODE_E_TANK_HIGH) != 0U)
		{
			blocked = 1U;
		}
		if (AppState_BlockFaultClearIf((depth.basket_depth_mm_x10 < ((int32_t)BOARD_BASKET_MIN_SAFE_DEPTH_MM * 10L)) ? 1U : 0U,
		                               ERROR_CODE_E_BASKET_LOW) != 0U)
		{
			blocked = 1U;
		}
		if (AppState_BlockFaultClearIf((depth.basket_depth_mm_x10 > ((int32_t)BOARD_BASKET_MAX_SAFE_DEPTH_MM * 10L)) ? 1U : 0U,
		                               ERROR_CODE_E_BASKET_HIGH) != 0U)
		{
			blocked = 1U;
		}
		if (AppState_BlockFaultClearIf(((depth.basket_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10) ||
		                                (depth.tank_depth_mm_x10 < BOARD_PRESSURE_PHYSICAL_MIN_MM_X10)) ? 1U : 0U,
		                               ERROR_CODE_E_PRESSURE_PHYSICAL) != 0U)
		{
			blocked = 1U;
		}

		target_error_mm_x10 = depth.basket_depth_mm_x10 - NapScheduler_GetTargetDepthMmX10();
		if (AppState_BlockFaultClearIf((AppState_Abs32(target_error_mm_x10) > BOARD_DEPTH_TRACK_HARD_ERROR_MM_X10) ? 1U : 0U,
		                               ERROR_CODE_E_DEPTH_TRACKING) != 0U)
		{
			blocked = 1U;
		}
	}

	return (blocked == 0U) ? 1U : 0U;
}

// 函    数：AppState_CheckAutoSafety
// 参    数：require_depth 非 0 时要求当前水深有效。
// 返 回 值：1 表示可继续自动流程，0 表示已锁存故障。
// 注意事项：自动模式必须保持电机、限位一致、位置可信，且不能在水深无效时运动。
static uint8_t AppState_CheckAutoSafety(uint8_t require_depth)
{
	WaterDepth_State_t depth;

	if (StepperUM244_IsMotorReleased() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_MOTOR_RELEASED);
	}
	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_LIMIT_MISMATCH);
	}
	if (PositionTracker_CanAutoRun() == 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_POSITION_UNTRUSTED);
	}
	if ((require_depth != 0U) && (WaterDepth_GetState(&depth) != WATER_DEPTH_OK))
	{
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
	}

	return (ErrorManager_HasFault() == 0U) ? 1U : 0U;
}

// 函    数：AppState_RecoveryDepthMatches
// 参    数：无
// 返 回 值：1 表示重启后水深与 Flash 记录一致，0 表示必须人工确认。
// 注意事项：差异超过 3mm 时不追赶断电期间错过的运动。
static uint8_t AppState_RecoveryDepthMatches(void)
{
	WaterDepth_State_t depth;

	if (WaterDepth_GetState(&depth) != WATER_DEPTH_OK)
	{
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		return 0U;
	}

	if ((s_record.last_basket_depth_mm_x10 <= 0L) ||
	    (s_record.last_tank_depth_mm_x10 <= 0L))
	{
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		return 0U;
	}

	if ((AppState_Abs32(depth.basket_depth_mm_x10 - s_record.last_basket_depth_mm_x10) >
	     BOARD_RESTART_DEPTH_DIFF_MM_X10) ||
	    (AppState_Abs32(depth.tank_depth_mm_x10 - s_record.last_tank_depth_mm_x10) >
	     BOARD_RESTART_DEPTH_DIFF_MM_X10))
	{
		// 重启水深差异超过 3mm 时，不追赶断电期间错过的运动。
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		return 0U;
	}

	return 1U;
}

// 函    数：AppState_HandleSelfTestPass
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：只在开机自检通过后执行一次断电恢复判定；NAP_MOVE/DROP 标记必须转人工确认。
static void AppState_HandleSelfTestPass(uint32_t now_ms)
{
	if (s_record.position_trusted != 0U)
	{
		(void)PositionTracker_Restore(s_record.basket_position_pulses, 1U);
	}

	NapScheduler_UpdateConfig(&s_record, now_ms);
	if (s_record.last_app_state == (uint8_t)PARAM_STORE_APP_MOTOR_RELEASE)
	{
		ErrorManager_Set(ERROR_CODE_E_MOTOR_RELEASED);
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if (s_record.last_app_state == (uint8_t)PARAM_STORE_APP_FAULT)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if (s_record.last_app_state == (uint8_t)PARAM_STORE_APP_NAP_MOVE)
	{
		// 断电时处在自动有限运动或 DROP 标记内，不能证明篮位/水位仍与 Flash 一致。
		s_check_water_notice = 1U;
		s_drop_recovery_saved = 1U;
		PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_INTERRUPTED_MOVE);
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if (AppState_IsAutoStoredState(s_record.last_app_state) == 0U)
	{
		AppState_Enter(APP_STATE_PAUSED, now_ms);
		return;
	}

	if (ErrorManager_IsActive(ERROR_CODE_W_PARAM_DEFAULT) != 0U)
	{
		AppState_Enter(APP_STATE_PAUSED, now_ms);
		return;
	}
	if (PositionTracker_Restore(s_record.basket_position_pulses,
	                            s_record.position_trusted) == 0U)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if ((AppState_CheckAutoSafety(1U) == 0U) ||
	    (AppState_RecoveryDepthMatches() == 0U))
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}

	// 自动恢复只从自检后的恢复判断进入，下一次打盹从当前时间重新排程。
	NapScheduler_ResetNext(now_ms);
	AutoControl_Reset(now_ms);
	s_check_water_notice = 0U;
	s_drop_recovery_saved = 0U;
	AppState_Enter(APP_STATE_AUTO_RUN, now_ms);
}

// 汇总状态机、调度器和自检上下文到菜单快照；只更新显示数据，不改变运动状态。
static void AppState_UpdateMenuSnapshot(uint32_t now_ms)
{
	Menu_AppSnapshot_t snapshot;
	NapScheduler_Display_t nap_display;
	uint32_t run_days;

	NapScheduler_GetDisplay(now_ms,
	                        ((s_state == APP_STATE_NAP_WAIT) ||
	                         (s_state == APP_STATE_AUTO_RUN)) ? 1U : 0U,
	                        &nap_display);

	snapshot.valid = 1U;
	snapshot.force_self_test_page = (s_state == APP_STATE_SELF_TEST) ? 1U : 0U;
	snapshot.mode = AppState_ToUiMode(s_state);
	snapshot.motion_text = AppState_GetMotionText(s_state);
	snapshot.notice_text = (s_check_water_notice != 0U) ? "CHECK WATER" : 0;
	snapshot.target_depth_valid = 1U;
	snapshot.target_depth_mm_x10 = nap_display.target_depth_mm_x10;
	run_days = (s_record.total_run_seconds / BOARD_SECONDS_PER_DAY) + 1UL;
	if (run_days > 999UL)
	{
		run_days = 999UL;
	}
	snapshot.run_days = (uint16_t)run_days;
	snapshot.next_nap_valid = nap_display.next_nap_valid;
	snapshot.next_nap_remaining_s = nap_display.next_nap_remaining_s;
	snapshot.today_done_pulses = nap_display.today_done_pulses;
	snapshot.nap_pulses = nap_display.nap_pulses;
	SelfTest_GetContext(&snapshot.self_test);
	Menu_SetAppSnapshot(&snapshot);
}

// 函    数：AppState_StartAuto
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：人工确认后进入自动前重新检查电机、限位、位置、水深和目标误差。
static void AppState_StartAuto(uint32_t now_ms)
{
	uint8_t had_check_water_notice;

	had_check_water_notice = s_check_water_notice;
	if (StepperUM244_IsMotorReleased() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_MOTOR_RELEASED);
	}
	if (AppState_CheckAutoSafety(1U) == 0U)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if (AppState_CheckDepthTolerance() == 0U)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}

	NapScheduler_UpdateConfig(&s_record, now_ms);
	NapScheduler_ResetNext(now_ms);
	AutoControl_Reset(now_ms);
	s_check_water_notice = 0U;
	s_drop_recovery_saved = 0U;
	AppState_Enter(APP_STATE_AUTO_RUN, now_ms);
	if (had_check_water_notice != 0U)
	{
		// 人工确认水位/位置后重新启动自动运行，清掉保守恢复标记，避免下次重启重复提示。
		AppState_SaveStateAs(PARAM_STORE_APP_AUTO_RUN, now_ms);
	}
}

// 手动点动启动前检查当前故障、电机释放和目标方向限位，维护模式也不能绕过限位。
static uint8_t AppState_ManualMoveAllowed(StepperUM244_Direction_t direction)
{
	if ((ErrorManager_HasFault() != 0U) &&
	    (s_state != APP_STATE_MAINTENANCE) &&
	    (s_state != APP_STATE_MOTOR_RELEASE))
	{
		return 0U;
	}
	if (StepperUM244_IsMotorReleased() != 0U)
	{
		return 0U;
	}
	if (Limit_IsSameDirectionMismatch() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_LIMIT_MISMATCH);
		return 0U;
	}
	if (direction == STEPPER_UM244_DIRECTION_UP)
	{
		return (Limit_IsDirectionBlocked(LIMIT_DIRECTION_UP) == 0U) ? 1U : 0U;
	}

	return (Limit_IsDirectionBlocked(LIMIT_DIRECTION_DOWN) == 0U) ? 1U : 0U;
}

// 结算手动点动已输出的 STEP，避免松手或故障中断时丢失真实位移。
static void AppState_ApplyManualCompleted(void)
{
	uint16_t pulses;

	if ((s_manual_chunk_active == 0U) || (StepperUM244_IsBusy() != 0U))
	{
		return;
	}

	pulses = StepperUM244_GetCompletedPulses();
	if (pulses != 0U)
	{
		// 手动松手、暂停或故障前已经输出的 STEP 都代表真实位移，不能只在整段完成时结算。
		PositionTracker_ApplyCompletedMove(s_manual_direction, pulses);
	}
	s_manual_chunk_active = 0U;
}

// 非阻塞处理手动保持按键；每次只发小段有限脉冲，松手立即停止并结算位置。
static void AppState_ServiceManual(uint32_t now_ms, const Menu_Intents_t *intents)
{
	uint8_t hold;
	StepperUM244_Status_t status;

	AppState_ApplyManualCompleted();
	hold = 0U;
	if (intents != 0)
	{
		if (intents->manual_up_hold != 0U)
		{
			hold = 1U;
			s_manual_direction = STEPPER_UM244_DIRECTION_UP;
		}
		else if (intents->manual_down_hold != 0U)
		{
			hold = 1U;
			s_manual_direction = STEPPER_UM244_DIRECTION_DOWN;
		}
	}

	if (hold == 0U)
	{
		if (s_state == APP_STATE_MANUAL)
		{
			StepperUM244_Stop();
			AppState_ApplyManualCompleted();
			AppState_Enter(s_manual_return_state, now_ms);
		}
		return;
	}

	if ((s_state != APP_STATE_MANUAL) &&
	    (s_state != APP_STATE_MAINTENANCE) &&
	    (s_state != APP_STATE_MOTOR_RELEASE))
	{
		s_manual_return_state = (s_state == APP_STATE_MAINTENANCE) ?
		                        APP_STATE_MAINTENANCE : APP_STATE_PAUSED;
		AppState_Enter(APP_STATE_MANUAL, now_ms);
	}

	if ((s_manual_chunk_active == 0U) && (StepperUM244_IsBusy() == 0U))
	{
		if (AppState_ManualMoveAllowed(s_manual_direction) == 0U)
		{
			return;
		}
		status = StepperUM244_StartPulses(s_manual_direction,
		                                  BOARD_STEPPER_MANUAL_CHUNK_PULSES,
		                                  BOARD_STEPPER_MANUAL_FREQ_HZ,
		                                  now_ms);
		if (status == STEPPER_UM244_STATUS_OK)
		{
			s_manual_chunk_active = 1U;
		}
		else if (status != STEPPER_UM244_STATUS_BUSY)
		{
			AppState_Enter(APP_STATE_FAULT, now_ms);
		}
	}
}

// 处理维护页意图：空气参考、回零、电机释放；自动运行状态不能直接执行维护动作。
static void AppState_HandleMaintenanceIntents(uint32_t now_ms, const Menu_Intents_t *intents)
{
	WF5805F_Reading_t air;

	if (intents == 0)
	{
		return;
	}
	if (intents->enter_maintenance != 0U)
	{
		AppState_Enter(APP_STATE_MAINTENANCE, now_ms);
	}
	if (intents->exit_maintenance != 0U)
	{
		StepperUM244_SetMotorRelease(0U);
		AppState_Enter(APP_STATE_PAUSED, now_ms);
	}
	if ((s_state != APP_STATE_MAINTENANCE) &&
	    (s_state != APP_STATE_MOTOR_RELEASE))
	{
		return;
	}

	if (intents->air_calibrate != 0U)
	{
		if ((WF5805F_GetReading(WF5805F_SENSOR_AIR, &air) == WF5805F_OK) &&
		    (air.valid != 0U))
		{
			s_record.air_offset_hpa_x100 = air.pressure_hpa_x100;
			(void)ParamStore_SaveParameters(&s_record);
			Menu_ReloadParams();
		}
		else
		{
			ErrorManager_Set(ERROR_CODE_W_PARAM_REJECTED);
		}
	}
	if (intents->home_zero != 0U)
	{
		(void)Homing_Start(now_ms);
		AppState_SaveState(now_ms);
	}
	if (intents->motor_release_toggle != 0U)
	{
		if (StepperUM244_IsMotorReleased() == 0U)
		{
			StepperUM244_SetMotorRelease(1U);
			PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_MOTOR_RELEASED);
			ErrorManager_Set(ERROR_CODE_E_MOTOR_RELEASED);
			AppState_Enter(APP_STATE_MOTOR_RELEASE, now_ms);
		}
		else
		{
			StepperUM244_SetMotorRelease(0U);
			ErrorManager_Clear(ERROR_CODE_E_MOTOR_RELEASED);
			AppState_Enter(APP_STATE_MAINTENANCE, now_ms);
		}
	}
}

// 函    数：AppState_HandleAlarmAck
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：严重故障第一次确认只静音，第二次也必须复查根因后才允许受控清除。
static void AppState_HandleAlarmAck(uint32_t now_ms)
{
	ErrorCode_t primary;

	primary = ErrorManager_GetPrimary();
	if (primary == ERROR_CODE_E_NONE)
	{
		ErrorManager_SetBuzzerMuted(0U);
		return;
	}

	if (ErrorManager_GetLevel(primary) == ERROR_LEVEL_WARNING)
	{
		ErrorManager_Clear(primary);
		ErrorManager_SetBuzzerMuted(0U);
		AppState_SaveState(now_ms);
		return;
	}

	if (ErrorManager_IsBuzzerMuted() == 0U)
	{
		// 第一次确认只静音，不清除严重故障锁存。
		ErrorManager_SetBuzzerMuted(1U);
		AppState_SaveState(now_ms);
		return;
	}

	if (AppState_CanClearLatchedFaults() == 0U)
	{
		// 第二次确认时若限位、传感器/I2C、水位、位置或电机释放仍异常，只保持静音和故障态。
		ErrorManager_SetBuzzerMuted(1U);
		AppState_SaveState(now_ms);
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}

	// 第二次确认仅在根因不可继续观测到时执行受控清除；静音本身不清故障。
	ErrorManager_ClearAll();
	ErrorManager_SetBuzzerMuted(0U);
	StepperUM244_ClearFault();
	if (StepperUM244_IsMotorReleased() != 0U)
	{
		ErrorManager_Set(ERROR_CODE_E_MOTOR_RELEASED);
		AppState_Enter(APP_STATE_FAULT, now_ms);
	}
	else
	{
		AppState_Enter(APP_STATE_PAUSED, now_ms);
	}
}

// 消费菜单产生的一次性用户意图，并把真正的运动/保存/清故障决策收敛到状态机。
static void AppState_HandleMenuIntents(uint32_t now_ms, const Menu_Intents_t *intents)
{
	if (intents == 0)
	{
		return;
	}
	if (intents->params_saved != 0U)
	{
		AppState_LoadRecord();
		NapScheduler_UpdateConfig(&s_record, now_ms);
		AutoControl_Reset(now_ms);
	}
	if (intents->alarm_ack != 0U)
	{
		AppState_HandleAlarmAck(now_ms);
	}
	if (intents->pause != 0U)
	{
		AppState_Enter(APP_STATE_PAUSED, now_ms);
	}
	if (intents->start_auto != 0U)
	{
		AppState_StartAuto(now_ms);
	}
	AppState_HandleMaintenanceIntents(now_ms, intents);
	AppState_ServiceManual(now_ms, intents);
}

// 准备一次自动有限脉冲运动；DROP 来源首次进入时写入保守恢复标记。
static void AppState_BeginAutoMove(uint32_t now_ms, const AutoControl_Decision_t *decision)
{
	s_nap_phase = APP_NAP_PHASE_PRE_DEPTH;
	s_nap_phase_start_ms = now_ms;
	s_nap_direction = decision->direction;
	s_nap_pulses = decision->pulses;
	s_auto_frequency_hz = decision->frequency_hz;
	s_auto_source = decision->source;
	s_auto_move_recorded = 0U;
	s_auto_position_applied = 0U;
	if ((decision->source == MOTION_SOURCE_DROP_FOLLOW) &&
	    (s_drop_recovery_saved == 0U))
	{
		// DROP 首次接管时只保存一次恢复标记；后续小步不反复擦写 Flash。
		AppState_SaveStateAs(PARAM_STORE_APP_NAP_MOVE, now_ms);
		s_drop_recovery_saved = 1U;
	}
}

// 自动运动被故障/暂停中断时，按已完成 pulse 结算位置和调度状态。
static void AppState_ApplyAutoCompletedPartial(uint32_t now_ms)
{
	uint16_t completed_pulses;

	if ((s_state != APP_STATE_NAP_MOVE) || (s_auto_move_recorded != 0U))
	{
		return;
	}

	completed_pulses = StepperUM244_GetCompletedPulses();
	if (completed_pulses == 0U)
	{
		s_auto_move_recorded = 1U;
		AutoControl_NotifyMoveAbort(s_auto_source, now_ms);
		return;
	}

	if (s_auto_position_applied == 0U)
	{
		PositionTracker_ApplyCompletedMove(s_nap_direction, completed_pulses);
		s_auto_position_applied = 1U;
	}
	(void)NapScheduler_RecordMove(&s_record,
	                               s_auto_source,
	                               s_nap_direction,
	                               completed_pulses,
	                               s_nap_before_depth.basket_depth_mm_x10,
	                               s_nap_before_depth.basket_depth_mm_x10,
	                               now_ms);
	AutoControl_NotifyMoveAbort(s_auto_source, now_ms);
	s_auto_move_recorded = 1U;
}

// 函    数：AppState_ServiceNapMove
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：自动有限脉冲分阶段执行：运动前水深、STEP 输出、运动后水深、卡滞/超差检查。
static void AppState_ServiceNapMove(uint32_t now_ms)
{
	AppState_DepthResult_t depth_result;
	WaterDepth_State_t after_depth;
	StepperUM244_Status_t step_status;
	NapScheduler_RecordResult_t record_result;
	uint16_t completed_pulses;

	if (AppState_CheckAutoSafety(1U) == 0U)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}

	if (s_nap_phase == APP_NAP_PHASE_PRE_DEPTH)
	{
		depth_result = AppState_GetFreshDepth(now_ms,
		                                      s_nap_phase_start_ms,
		                                      BOARD_NAP_SENSOR_FRESH_TIMEOUT_MS,
		                                      &s_nap_before_depth);
		if (depth_result == APP_DEPTH_WAIT)
		{
			return;
		}
		if (depth_result == APP_DEPTH_TIMEOUT)
		{
			ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
			AppState_Enter(APP_STATE_FAULT, now_ms);
			return;
		}
		s_nap_phase = APP_NAP_PHASE_START_MOVE;
	}

	if (s_nap_phase == APP_NAP_PHASE_START_MOVE)
	{
		if (s_nap_pulses == 0U)
		{
			AppState_Enter(APP_STATE_NAP_WAIT, now_ms);
			return;
		}
		step_status = StepperUM244_StartPulses(s_nap_direction,
		                                       s_nap_pulses,
		                                       s_auto_frequency_hz,
		                                       now_ms);
		if (step_status == STEPPER_UM244_STATUS_OK)
		{
			s_nap_phase = APP_NAP_PHASE_WAIT_MOVE;
			return;
		}
		if (step_status != STEPPER_UM244_STATUS_BUSY)
		{
			AppState_Enter(APP_STATE_FAULT, now_ms);
		}
		return;
	}

	if (s_nap_phase == APP_NAP_PHASE_WAIT_MOVE)
	{
		if (StepperUM244_IsBusy() != 0U)
		{
			return;
		}
		if (StepperUM244_GetStopReason() != STEPPER_UM244_STOP_PULSE_DONE)
		{
			AppState_ApplyAutoCompletedPartial(now_ms);
			AppState_Enter(APP_STATE_FAULT, now_ms);
			return;
		}
		completed_pulses = StepperUM244_GetCompletedPulses();
		if (completed_pulses != 0U)
		{
			PositionTracker_ApplyCompletedMove(s_nap_direction, completed_pulses);
			s_auto_position_applied = 1U;
		}
		s_nap_phase = APP_NAP_PHASE_POST_DEPTH;
		s_nap_phase_start_ms = now_ms;
		return;
	}

	depth_result = AppState_GetFreshDepth(now_ms,
	                                      s_nap_phase_start_ms,
	                                      BOARD_NAP_POST_DEPTH_TIMEOUT_MS,
	                                      &after_depth);
	if (depth_result == APP_DEPTH_WAIT)
	{
		return;
	}
	if (depth_result == APP_DEPTH_TIMEOUT)
	{
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if (AppState_CheckDepthTolerance() == 0U)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}

	completed_pulses = StepperUM244_GetCompletedPulses();
	record_result = NapScheduler_RecordMove(&s_record,
	                                        s_auto_source,
	                                        s_nap_direction,
	                                        completed_pulses,
	                                        s_nap_before_depth.basket_depth_mm_x10,
	                                        after_depth.basket_depth_mm_x10,
	                                        now_ms);
	AutoControl_NotifyMoveComplete(s_auto_source,
	                               s_nap_direction,
	                               completed_pulses,
	                               &s_nap_before_depth,
	                               &after_depth,
	                               now_ms);
	s_auto_move_recorded = 1U;
	if (record_result == NAP_SCHEDULER_RECORD_STALL_FAULT)
	{
		ErrorManager_Set(ERROR_CODE_E_STALL);
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if (record_result == NAP_SCHEDULER_RECORD_CHECKPOINT)
	{
		AppState_SaveState(now_ms);
	}

	AppState_Enter(APP_STATE_NAP_WAIT, now_ms);
}

// 函    数：AppState_ServiceAutomatic
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：自动模式统一仲裁 DROP、TRK 和每日打盹；任何故障都优先进入 FAULT。
static void AppState_ServiceAutomatic(uint32_t now_ms)
{
	WaterDepth_State_t depth;
	AutoControl_Input_t input;
	AutoControl_Decision_t decision;

	if ((s_state != APP_STATE_AUTO_RUN) &&
	    (s_state != APP_STATE_NAP_WAIT) &&
	    (s_state != APP_STATE_NAP_MOVE))
	{
		return;
	}

	if (AppState_CheckAutoSafety(1U) == 0U)
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
		return;
	}
	if ((s_state == APP_STATE_AUTO_RUN) || (s_state == APP_STATE_NAP_WAIT))
	{
		if (AppState_CheckDepthTolerance() == 0U)
		{
			AppState_Enter(APP_STATE_FAULT, now_ms);
			return;
		}
	}

	if (s_state == APP_STATE_AUTO_RUN)
	{
		AppState_Enter(APP_STATE_NAP_WAIT, now_ms);
		return;
	}
	if (s_state == APP_STATE_NAP_WAIT)
	{
		if (WaterDepth_GetState(&depth) != WATER_DEPTH_OK)
		{
			ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
			AppState_Enter(APP_STATE_FAULT, now_ms);
			return;
		}
		input.depth = depth;
		input.target_depth_mm_x10 = NapScheduler_GetTargetDepthMmX10();
		input.nap_due = NapScheduler_IsDue(now_ms);
		input.nap_pulses = NapScheduler_GetNextPulses();
		input.daily_remaining_pulses = NapScheduler_GetDailyRemainingPulses();
		AutoControl_Arbitrate(now_ms, &input, &decision);
		if (decision.type == AUTO_CONTROL_DECISION_FAULT)
		{
			ErrorManager_Set(decision.error_code);
			AppState_Enter(APP_STATE_FAULT, now_ms);
			return;
		}
		if (decision.type == AUTO_CONTROL_DECISION_PAUSE)
		{
			// CHECK WATER 暂停由 AppState_SaveCriticalState() 复用 NAP_MOVE 保存保守恢复标记。
			s_check_water_notice = decision.check_water_notice;
			AutoControl_Reset(now_ms);
			AppState_Enter(APP_STATE_PAUSED, now_ms);
			return;
		}
		if (decision.type == AUTO_CONTROL_DECISION_MOVE)
		{
			AppState_BeginAutoMove(now_ms, &decision);
			AppState_Enter(APP_STATE_NAP_MOVE, now_ms);
		}
		return;
	}

	AppState_ServiceNapMove(now_ms);
}

// 函    数：AppState_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：总是先进入 APP_SELF_TEST；是否自动恢复只在自检通过后判断一次。
void AppState_Init(uint32_t now_ms)
{
	AppState_LoadRecord();
	s_state = APP_STATE_SELF_TEST;
	s_manual_return_state = APP_STATE_PAUSED;
	s_last_second_ms = now_ms;
	s_nap_phase = APP_NAP_PHASE_PRE_DEPTH;
	s_nap_phase_start_ms = now_ms;
	s_nap_direction = STEPPER_UM244_DIRECTION_UP;
	s_nap_pulses = 0U;
	s_auto_frequency_hz = BOARD_STEPPER_AUTO_FREQ_HZ;
	s_auto_source = MOTION_SOURCE_DAILY_SHALLOW;
	s_auto_move_recorded = 0U;
	s_auto_position_applied = 0U;
	s_check_water_notice = 0U;
	s_drop_recovery_saved = 0U;
	s_manual_chunk_active = 0U;
	s_manual_direction = STEPPER_UM244_DIRECTION_DOWN;
	NapScheduler_Init(&s_record, now_ms);
	AutoControl_Init(now_ms);
	SelfTest_Init(now_ms);
	Menu_Init(now_ms);
	AppState_UpdateMenuSnapshot(now_ms);
}

// 函    数：AppState_Update
// 参    数：now_ms 当前系统毫秒时间戳；key_events 本轮按键事件位图。
// 返 回 值：无
// 注意事项：主状态机统一处理自检、恢复、自动打盹、故障确认和维护/手动意图。
void AppState_Update(uint32_t now_ms, uint16_t key_events)
{
	SelfTest_Status_t self_status;
	Menu_Intents_t intents;

	AppState_ServiceSeconds(now_ms);

	if (s_state == APP_STATE_SELF_TEST)
	{
		self_status = SelfTest_Update(now_ms);
		if (self_status == SELF_TEST_STATUS_PASS)
		{
			AppState_HandleSelfTestPass(now_ms);
		}
		else if (self_status == SELF_TEST_STATUS_FAIL)
		{
			AppState_Enter(APP_STATE_FAULT, now_ms);
		}
	}

	AppState_UpdateMenuSnapshot(now_ms);
	Menu_Update(now_ms, key_events);
	Menu_GetIntents(&intents);

	if (s_state != APP_STATE_SELF_TEST)
	{
		AppState_HandleMenuIntents(now_ms, &intents);
		AppState_ServiceAutomatic(now_ms);
	}

	if ((ErrorManager_HasFault() != 0U) &&
	    (s_state != APP_STATE_FAULT) &&
	    (s_state != APP_STATE_MAINTENANCE) &&
	    (s_state != APP_STATE_MOTOR_RELEASE))
	{
		AppState_Enter(APP_STATE_FAULT, now_ms);
	}

	AppState_UpdateMenuSnapshot(now_ms);
	SelfTest_ServiceBuzzer(now_ms);
	AppState_SaveRuntimeIfDue(now_ms);
}

// 函    数：AppState_GetState
// 参    数：无
// 返 回 值：当前应用状态。
AppState_State_t AppState_GetState(void)
{
	return s_state;
}
