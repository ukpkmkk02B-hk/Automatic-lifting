#include "app_state.h"
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
static uint8_t s_manual_chunk_active;
static StepperUM244_Direction_t s_manual_direction;

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

static const char *AppState_GetMotionText(AppState_State_t state)
{
	switch (state)
	{
	case APP_STATE_SELF_TEST:
		return "TEST";
	case APP_STATE_PAUSED:
	case APP_STATE_FAULT:
		return "STOP";
	case APP_STATE_NAP_WAIT:
		return "WAIT";
	case APP_STATE_NAP_MOVE:
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

static int32_t AppState_Abs32(int32_t value)
{
	return (value < 0L) ? -value : value;
}

static uint8_t AppState_IsAutoStoredState(uint8_t state)
{
	return ((state == (uint8_t)PARAM_STORE_APP_AUTO_RUN) ||
	        (state == (uint8_t)PARAM_STORE_APP_NAP_WAIT) ||
	        (state == (uint8_t)PARAM_STORE_APP_NAP_MOVE)) ? 1U : 0U;
}

static void AppState_LoadRecord(void)
{
	if (ParamStore_Load(&s_record) != PARAM_STORE_STATUS_OK)
	{
		ParamStore_LoadDefaults(&s_record);
	}
	ErrorManager_SetBuzzerMuted(s_record.buzzer_muted);
}

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

static void AppState_SaveState(uint32_t now_ms)
{
	AppState_SyncRecordRuntime();
	(void)ParamStore_ForceSaveRuntime(&s_record, now_ms);
}

static void AppState_SaveRuntimeIfDue(uint32_t now_ms)
{
	if ((s_state == APP_STATE_NAP_MOVE) || (StepperUM244_IsBusy() != 0U))
	{
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

	if ((state == APP_STATE_FAULT) || (state == APP_STATE_PAUSED))
	{
		StepperUM244_Stop();
		s_manual_chunk_active = 0U;
	}

	s_state = state;
	if ((state == APP_STATE_FAULT) ||
	    (state == APP_STATE_PAUSED) ||
	    (state == APP_STATE_MAINTENANCE) ||
	    (state == APP_STATE_MOTOR_RELEASE))
	{
		AppState_SaveState(now_ms);
	}
}

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
	if (AppState_Abs32(error_mm_x10) > BOARD_CONTROL_TOLERANCE_MM_X10)
	{
		// 自动控制误差超过 ±1mm 时停止自动，避免按错误目标继续移动。
		ErrorManager_Set(ERROR_CODE_E_DEPTH_TRACKING);
		return 0U;
	}

	return 1U;
}

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
	AppState_Enter(APP_STATE_AUTO_RUN, now_ms);
}

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

static void AppState_StartAuto(uint32_t now_ms)
{
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
	AppState_Enter(APP_STATE_AUTO_RUN, now_ms);
}

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

static void AppState_ApplyManualCompleted(void)
{
	uint16_t pulses;

	if ((s_manual_chunk_active == 0U) || (StepperUM244_IsBusy() != 0U))
	{
		return;
	}

	if (StepperUM244_GetStopReason() == STEPPER_UM244_STOP_PULSE_DONE)
	{
		pulses = StepperUM244_GetCompletedPulses();
		if (pulses != 0U)
		{
			PositionTracker_ApplyCompletedMove(s_manual_direction, pulses);
		}
	}
	s_manual_chunk_active = 0U;
}

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

	// 第二次确认执行受控清除；若真实故障仍存在，底层轮询会在后续周期重新置位。
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

static void AppState_BeginNapMove(uint32_t now_ms)
{
	s_nap_phase = APP_NAP_PHASE_PRE_DEPTH;
	s_nap_phase_start_ms = now_ms;
	s_nap_direction = STEPPER_UM244_DIRECTION_UP;
	s_nap_pulses = NapScheduler_GetNextPulses();
}

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
		step_status = StepperUM244_StartNapMove(s_nap_direction,
		                                        s_nap_pulses,
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
			AppState_Enter(APP_STATE_FAULT, now_ms);
			return;
		}
		completed_pulses = StepperUM244_GetCompletedPulses();
		if (completed_pulses != 0U)
		{
			PositionTracker_ApplyCompletedMove(s_nap_direction, completed_pulses);
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
	                                        s_nap_direction,
	                                        completed_pulses,
	                                        s_nap_before_depth.basket_depth_mm_x10,
	                                        after_depth.basket_depth_mm_x10,
	                                        now_ms);
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

static void AppState_ServiceAutomatic(uint32_t now_ms)
{
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
		if (NapScheduler_IsDue(now_ms) != 0U)
		{
			AppState_BeginNapMove(now_ms);
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
	s_manual_chunk_active = 0U;
	s_manual_direction = STEPPER_UM244_DIRECTION_DOWN;
	NapScheduler_Init(&s_record, now_ms);
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
