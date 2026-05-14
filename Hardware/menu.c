#include "menu.h"
#include "board_config.h"
#include "ui_pages.h"
#include "key_scan.h"
#include "buzzer.h"
#include "limit.h"
#include "wf5805f.h"
#include "water_depth.h"
#include "error_manager.h"
#include "stepper_um244.h"
#include "position_tracker.h"
#include "homing.h"
#include "param_store.h"

typedef enum
{
	MENU_PAGE_MAIN = 0,
	MENU_PAGE_SELF_TEST,
	MENU_PAGE_SENSOR,
	MENU_PAGE_LIMIT,
	MENU_PAGE_PARAM,
	MENU_PAGE_MANUAL,
	MENU_PAGE_ALARM,
	MENU_PAGE_MAINTENANCE,
	MENU_PAGE_COUNT
} Menu_Page_t;

typedef enum
{
	MENU_CONFIRM_NONE = 0,
	MENU_CONFIRM_ENTER_MAINTENANCE,
	MENU_CONFIRM_HOME_ZERO,
	MENU_CONFIRM_MOTOR_RELEASE
} Menu_Confirm_t;

static Menu_Page_t s_page;
static Menu_Page_t s_return_page;
static uint8_t s_in_maintenance;
static uint8_t s_maintenance_menu_index;
static uint8_t s_maintenance_debug;
static Menu_Confirm_t s_confirm;
static ParamStore_Record_t s_param_record;
static UiPages_ParamId_t s_param_id;
static uint8_t s_param_dirty;
static uint8_t s_param_error;
static uint32_t s_last_refresh_ms;
static uint32_t s_last_param_repeat_ms;
static uint32_t s_beep_off_ms;
static uint8_t s_manual_hold_active;
static uint8_t s_manual_chunk_active;
static StepperUM244_Direction_t s_manual_direction;

static uint8_t Menu_TimeElapsed(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
	return ((uint32_t)(now_ms - last_ms) >= interval_ms) ? 1U : 0U;
}

static void Menu_RequestRenderNow(uint32_t now_ms)
{
	if (now_ms >= BOARD_UI_REFRESH_MS)
	{
		s_last_refresh_ms = now_ms - BOARD_UI_REFRESH_MS;
	}
	else
	{
		s_last_refresh_ms = 0UL;
	}
}

static void Menu_StartShortBeep(uint32_t now_ms)
{
	Buzzer_On();
	s_beep_off_ms = now_ms + BOARD_UI_BEEP_MS;
}

// 函    数：Menu_ServiceBuzzer
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：短鸣和报警静音只影响声音输出，不清除 error_manager 中的故障锁存位。
static void Menu_ServiceBuzzer(uint32_t now_ms)
{
	if (s_beep_off_ms != 0UL)
	{
		if ((int32_t)(now_ms - s_beep_off_ms) >= 0)
		{
			s_beep_off_ms = 0UL;
			if ((ErrorManager_HasFault() == 0U) || (ErrorManager_IsBuzzerMuted() != 0U))
			{
				Buzzer_Off();
			}
		}
		return;
	}

	if ((ErrorManager_HasFault() != 0U) && (ErrorManager_IsBuzzerMuted() == 0U))
	{
		Buzzer_On();
	}
	else
	{
		Buzzer_Off();
	}
}

static void Menu_LoadParams(void)
{
	if (ParamStore_Load(&s_param_record) != PARAM_STORE_STATUS_OK)
	{
		ParamStore_LoadDefaults(&s_param_record);
	}
}

static uint8_t Menu_CountActiveErrors(void)
{
	uint8_t i;
	uint8_t count;

	count = 0U;
	for (i = 1U; i < (uint8_t)ERROR_CODE_COUNT; i++)
	{
		if (ErrorManager_IsActive((ErrorCode_t)i) != 0U)
		{
			count++;
		}
	}

	return count;
}

static void Menu_SaveRuntimeState(ParamStore_AppState_t state, uint32_t now_ms)
{
	ParamStore_Record_t record;
	WaterDepth_State_t depth;

	if (ParamStore_Load(&record) != PARAM_STORE_STATUS_OK)
	{
		ParamStore_LoadDefaults(&record);
	}

	record.last_app_state = (uint8_t)state;
	record.buzzer_muted = ErrorManager_IsBuzzerMuted();
	record.position_trusted = PositionTracker_IsTrusted();
	record.basket_position_pulses = PositionTracker_GetPulses();
	if (WaterDepth_GetState(&depth) == WATER_DEPTH_OK)
	{
		record.last_basket_depth_mm_x10 = depth.basket_depth_mm_x10;
		record.last_tank_depth_mm_x10 = depth.tank_depth_mm_x10;
	}

	(void)ParamStore_ForceSaveRuntime(&record, now_ms);
	Menu_LoadParams();
}

static void Menu_NextPage(void)
{
	if ((s_page == MENU_PAGE_PARAM) && (s_param_error == 0U))
	{
		if (s_param_dirty != 0U)
		{
			Menu_LoadParams();
			s_param_dirty = 0U;
		}
		s_param_id = (UiPages_ParamId_t)((uint8_t)s_param_id + 1U);
		if (s_param_id < UI_PAGES_PARAM_COUNT)
		{
			return;
		}
		s_param_id = UI_PAGES_PARAM_INITIAL_DEPTH;
	}

	if ((s_page == MENU_PAGE_MAINTENANCE) && (s_in_maintenance != 0U) && (s_maintenance_debug != 0U))
	{
		s_maintenance_debug = 0U;
		return;
	}

	s_page = (Menu_Page_t)((uint8_t)s_page + 1U);
	if (s_page >= MENU_PAGE_COUNT)
	{
		s_page = MENU_PAGE_MAIN;
	}
}

static void Menu_StartConfirm(Menu_Confirm_t confirm, Menu_Page_t return_page)
{
	s_confirm = confirm;
	s_return_page = return_page;
	s_page = MENU_PAGE_MAINTENANCE;
}

static void Menu_AdjustParam(int8_t delta)
{
	int32_t value;

	if ((s_page != MENU_PAGE_PARAM) || (s_param_error != 0U) ||
	    (s_param_id == UI_PAGES_PARAM_MANUAL_SPEED))
	{
		return;
	}

	switch (s_param_id)
	{
	case UI_PAGES_PARAM_INITIAL_DEPTH:
		value = s_param_record.initial_target_mm_x10 + ((int32_t)delta * 10L);
		if (value < 0L)
		{
			value = 0L;
		}
		if (value > 1100L)
		{
			value = 1100L;
		}
		s_param_record.initial_target_mm_x10 = value;
		break;
	case UI_PAGES_PARAM_FINAL_DEPTH:
		value = s_param_record.final_target_mm_x10 + ((int32_t)delta * 10L);
		if (value < 0L)
		{
			value = 0L;
		}
		if (value > 1100L)
		{
			value = 1100L;
		}
		s_param_record.final_target_mm_x10 = value;
		break;
	case UI_PAGES_PARAM_DAILY_RATE:
		value = s_param_record.daily_shallow_mm_x10 + (int32_t)delta;
		if (value < 0L)
		{
			value = 0L;
		}
		if (value > 30L)
		{
			value = 30L;
		}
		s_param_record.daily_shallow_mm_x10 = value;
		break;
	case UI_PAGES_PARAM_NAP_PULSE:
		value = (int32_t)s_param_record.nap_pulses + (int32_t)delta;
		if (value < 0L)
		{
			value = 0L;
		}
		if (value > 20L)
		{
			value = 20L;
		}
		s_param_record.nap_pulses = (uint16_t)value;
		break;
	default:
		break;
	}

	s_param_dirty = 1U;
}

static void Menu_SaveParam(uint32_t now_ms)
{
	ParamStore_Status_t status;

	if ((s_page != MENU_PAGE_PARAM) || (s_param_id == UI_PAGES_PARAM_MANUAL_SPEED))
	{
		return;
	}

	status = ParamStore_ValidateRecord(&s_param_record);
	if (status == PARAM_STORE_STATUS_OK)
	{
		status = ParamStore_SaveParameters(&s_param_record);
	}

	if (status == PARAM_STORE_STATUS_OK)
	{
		s_param_dirty = 0U;
		ErrorManager_Clear(ERROR_CODE_W_PARAM_REJECTED);
		Menu_LoadParams();
	}
	else
	{
		s_param_error = 1U;
		ErrorManager_Set(ERROR_CODE_W_PARAM_REJECTED);
		Menu_StartShortBeep(now_ms);
	}
}

static void Menu_ServiceParamRepeat(uint32_t now_ms)
{
	if ((s_page != MENU_PAGE_PARAM) || (s_param_error != 0U))
	{
		return;
	}

	if (Menu_TimeElapsed(now_ms, s_last_param_repeat_ms, BOARD_UI_PARAM_REPEAT_MS) == 0U)
	{
		return;
	}

	if (KeyScan_IsPressed(KEY_SCAN_KEY1) != 0U)
	{
		s_last_param_repeat_ms = now_ms;
		Menu_AdjustParam(-1);
	}
	else if (KeyScan_IsPressed(KEY_SCAN_KEY2) != 0U)
	{
		s_last_param_repeat_ms = now_ms;
		Menu_AdjustParam(1);
	}
}

static uint8_t Menu_ManualMoveAllowed(StepperUM244_Direction_t direction)
{
	if ((ErrorManager_HasFault() != 0U) && (s_in_maintenance == 0U))
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

static void Menu_ApplyManualCompleted(void)
{
	uint16_t pulses;

	if ((s_manual_chunk_active == 0U) || (StepperUM244_IsBusy() != 0U))
	{
		return;
	}

	pulses = StepperUM244_GetCompletedPulses();
	if (pulses != 0U)
	{
		PositionTracker_ApplyCompletedMove(s_manual_direction, pulses);
	}
	s_manual_chunk_active = 0U;
}

// 函    数：Menu_ServiceManualMove
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：手动移动必须先收到长按事件；之后只要按键保持按下，就连续发送 80 pulse 有限小段，松手立即停止。
static void Menu_ServiceManualMove(uint32_t now_ms)
{
	uint8_t pressed;
	StepperUM244_Status_t status;

	(void)now_ms;
	Menu_ApplyManualCompleted();

	if (s_page != MENU_PAGE_MANUAL)
	{
		if (s_manual_hold_active != 0U)
		{
			StepperUM244_Stop();
			Menu_ApplyManualCompleted();
		}
		s_manual_hold_active = 0U;
		return;
	}

	if (s_manual_hold_active == 0U)
	{
		return;
	}

	pressed = (s_manual_direction == STEPPER_UM244_DIRECTION_UP) ?
	          KeyScan_IsPressed(KEY_SCAN_KEY2) :
	          KeyScan_IsPressed(KEY_SCAN_KEY1);
	if (pressed == 0U)
	{
		StepperUM244_Stop();
		Menu_ApplyManualCompleted();
		s_manual_hold_active = 0U;
		return;
	}

	if ((s_manual_chunk_active == 0U) && (StepperUM244_IsBusy() == 0U))
	{
		if (Menu_ManualMoveAllowed(s_manual_direction) == 0U)
		{
			s_manual_hold_active = 0U;
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
			s_manual_hold_active = 0U;
		}
	}
}

static void Menu_HandleAlarmKey(uint32_t now_ms)
{
	ErrorCode_t primary;

	primary = ErrorManager_GetPrimary();
	if (primary == ERROR_CODE_E_NONE)
	{
		ErrorManager_SetBuzzerMuted(0U);
		Buzzer_Off();
		return;
	}

	if (ErrorManager_IsBuzzerMuted() == 0U)
	{
		ErrorManager_SetBuzzerMuted(1U);
		Buzzer_Off();
		Menu_SaveRuntimeState(PARAM_STORE_APP_FAULT, now_ms);
	}
	else if (ErrorManager_GetLevel(primary) == ERROR_LEVEL_WARNING)
	{
		ErrorManager_Clear(primary);
	}
}

static void Menu_HandleMaintenanceOk(uint32_t now_ms)
{
	WF5805F_Reading_t air;

	if (s_in_maintenance == 0U)
	{
		return;
	}

	if (s_maintenance_debug != 0U)
	{
		s_maintenance_debug = 0U;
		return;
	}

	switch (s_maintenance_menu_index)
	{
	case 0U:
		if ((WF5805F_GetReading(WF5805F_SENSOR_AIR, &air) == WF5805F_OK) &&
		    (air.valid != 0U))
		{
			s_param_record.air_offset_hpa_x100 = air.pressure_hpa_x100;
			(void)ParamStore_SaveParameters(&s_param_record);
			Menu_LoadParams();
		}
		else
		{
			Menu_StartShortBeep(now_ms);
		}
		break;
	case 1U:
		Menu_StartConfirm(MENU_CONFIRM_HOME_ZERO, MENU_PAGE_MAINTENANCE);
		break;
	case 2U:
		Menu_StartConfirm(MENU_CONFIRM_MOTOR_RELEASE, MENU_PAGE_MAINTENANCE);
		Menu_StartShortBeep(now_ms);
		break;
	default:
		s_maintenance_debug = 1U;
		break;
	}
}

static void Menu_HandleConfirm(uint32_t now_ms, uint16_t key_events)
{
	if ((key_events & KEY_SCAN_EVENT_PAGE_SHORT) != 0U)
	{
		s_confirm = MENU_CONFIRM_NONE;
		s_page = s_return_page;
		return;
	}

	if ((key_events & KEY_SCAN_EVENT_PAUSE_SHORT) == 0U)
	{
		return;
	}

	switch (s_confirm)
	{
	case MENU_CONFIRM_ENTER_MAINTENANCE:
		s_in_maintenance = 1U;
		s_page = MENU_PAGE_MAINTENANCE;
		s_maintenance_debug = 0U;
		Menu_SaveRuntimeState(PARAM_STORE_APP_MAINTENANCE, now_ms);
		break;
	case MENU_CONFIRM_HOME_ZERO:
		(void)Homing_Start(now_ms);
		Menu_SaveRuntimeState(PARAM_STORE_APP_MAINTENANCE, now_ms);
		s_page = MENU_PAGE_MAINTENANCE;
		break;
	case MENU_CONFIRM_MOTOR_RELEASE:
		if (StepperUM244_IsMotorReleased() == 0U)
		{
			StepperUM244_SetMotorRelease(1U);
			PositionTracker_MarkUntrusted(POSITION_TRACKER_UNTRUSTED_MOTOR_RELEASED);
			Menu_SaveRuntimeState(PARAM_STORE_APP_MOTOR_RELEASE, now_ms);
		}
		else
		{
			StepperUM244_SetMotorRelease(0U);
			Menu_SaveRuntimeState(PARAM_STORE_APP_MAINTENANCE, now_ms);
		}
		s_page = MENU_PAGE_MAINTENANCE;
		break;
	default:
		break;
	}

	s_confirm = MENU_CONFIRM_NONE;
}

static void Menu_HandleKeys(uint32_t now_ms, uint16_t key_events)
{
	if ((key_events & KEY_SCAN_EVENT_MAINTENANCE_ENTRY) != 0U)
	{
		if (s_in_maintenance != 0U)
		{
			s_in_maintenance = 0U;
			s_maintenance_debug = 0U;
			StepperUM244_SetMotorRelease(0U);
			Menu_SaveRuntimeState(PARAM_STORE_APP_PAUSED, now_ms);
			s_page = MENU_PAGE_MAIN;
		}
		else
		{
			Menu_StartConfirm(MENU_CONFIRM_ENTER_MAINTENANCE, s_page);
		}
		return;
	}

	if (s_confirm != MENU_CONFIRM_NONE)
	{
		Menu_HandleConfirm(now_ms, key_events);
		return;
	}

	if (s_param_error != 0U)
	{
		if ((key_events & (KEY_SCAN_EVENT_PAUSE_SHORT | KEY_SCAN_EVENT_PAGE_SHORT)) != 0U)
		{
			s_param_error = 0U;
			Menu_LoadParams();
			s_param_dirty = 0U;
		}
		return;
	}

	if ((key_events & KEY_SCAN_EVENT_PAGE_SHORT) != 0U)
	{
		Menu_NextPage();
	}

	if (s_page == MENU_PAGE_PARAM)
	{
		if ((key_events & KEY_SCAN_EVENT_KEY1_SHORT) != 0U)
		{
			Menu_AdjustParam(-1);
			s_last_param_repeat_ms = now_ms;
		}
		if ((key_events & KEY_SCAN_EVENT_KEY2_SHORT) != 0U)
		{
			Menu_AdjustParam(1);
			s_last_param_repeat_ms = now_ms;
		}
		if ((key_events & KEY_SCAN_EVENT_PAUSE_SHORT) != 0U)
		{
			Menu_SaveParam(now_ms);
		}
	}
	else if (s_page == MENU_PAGE_MANUAL)
	{
		if ((key_events & KEY_SCAN_EVENT_KEY1_LONG) != 0U)
		{
			s_manual_direction = STEPPER_UM244_DIRECTION_DOWN;
			s_manual_hold_active = 1U;
		}
		if ((key_events & KEY_SCAN_EVENT_KEY2_LONG) != 0U)
		{
			s_manual_direction = STEPPER_UM244_DIRECTION_UP;
			s_manual_hold_active = 1U;
		}
	}
	else if (s_page == MENU_PAGE_ALARM)
	{
		if ((key_events & KEY_SCAN_EVENT_PAUSE_SHORT) != 0U)
		{
			Menu_HandleAlarmKey(now_ms);
		}
	}
	else if (s_page == MENU_PAGE_MAINTENANCE)
	{
		if ((key_events & KEY_SCAN_EVENT_KEY1_SHORT) != 0U)
		{
			if (s_maintenance_menu_index == 0U)
			{
				s_maintenance_menu_index = 3U;
			}
			else
			{
				s_maintenance_menu_index--;
			}
		}
		if ((key_events & KEY_SCAN_EVENT_KEY2_SHORT) != 0U)
		{
			s_maintenance_menu_index++;
			if (s_maintenance_menu_index >= 4U)
			{
				s_maintenance_menu_index = 0U;
			}
		}
		if ((key_events & KEY_SCAN_EVENT_PAUSE_SHORT) != 0U)
		{
			Menu_HandleMaintenanceOk(now_ms);
		}
	}
}

static UiPages_Mode_t Menu_GetDisplayMode(void)
{
	if (ErrorManager_HasFault() != 0U)
	{
		return UI_PAGES_MODE_FAULT;
	}
	if (s_in_maintenance != 0U)
	{
		return UI_PAGES_MODE_MAINTENANCE;
	}
	if (s_page == MENU_PAGE_MANUAL)
	{
		return UI_PAGES_MODE_MANUAL;
	}
	if (s_page == MENU_PAGE_SELF_TEST)
	{
		return UI_PAGES_MODE_SELF_TEST;
	}

	return UI_PAGES_MODE_AUTO;
}

static void Menu_RenderMain(void)
{
	UiPages_MainContext_t ctx;
	WaterDepth_State_t depth;
	uint32_t run_days;

	ctx.basket_depth_valid = 0U;
	ctx.tank_depth_valid = 0U;
	ctx.target_depth_valid = 1U;
	ctx.target_depth_mm_x10 = s_param_record.initial_target_mm_x10;
	run_days = (s_param_record.total_run_seconds / 86400UL) + 1UL;
	if (run_days > 999UL)
	{
		run_days = 999UL;
	}
	ctx.run_days = (uint16_t)run_days;
	ctx.mode = Menu_GetDisplayMode();
	if (ErrorManager_HasFault() != 0U)
	{
		ctx.motion_text = "STOP";
	}
	else if (s_in_maintenance != 0U)
	{
		ctx.motion_text = "IDLE";
	}
	else if (s_page == MENU_PAGE_MANUAL)
	{
		ctx.motion_text = "JOG";
	}
	else
	{
		ctx.motion_text = "RUN";
	}
	ctx.next_nap_valid = 0U;
	ctx.next_nap_remaining_s = 0UL;
	ctx.today_done_pulses = (s_param_record.today_pulses_done > 999UL) ?
	                        999U : (uint16_t)s_param_record.today_pulses_done;
	ctx.nap_pulses = s_param_record.nap_pulses;
	ctx.primary_error = ErrorManager_GetPrimary();
	ctx.buzzer_muted = ErrorManager_IsBuzzerMuted();

	if (WaterDepth_GetState(&depth) == WATER_DEPTH_OK)
	{
		ctx.basket_depth_valid = 1U;
		ctx.basket_depth_mm_x10 = depth.basket_depth_mm_x10;
		ctx.tank_depth_valid = 1U;
		ctx.tank_depth_mm_x10 = depth.tank_depth_mm_x10;
	}
	else
	{
		ctx.basket_depth_mm_x10 = 0L;
		ctx.tank_depth_mm_x10 = 0L;
	}

	UiPages_RenderMain(&ctx);
}

static void Menu_RenderSelfTest(void)
{
	UiPages_SelfTestContext_t ctx;

	ctx.seconds_left_valid = 0U;
	ctx.seconds_left = 0U;
	ctx.i2c_a_ok = (WF5805F_GetFailureCount(WF5805F_SENSOR_AIR) == 0U) ? 1U : 0U;
	ctx.i2c_b_ok = (WF5805F_GetFailureCount(WF5805F_SENSOR_BASKET) == 0U) ? 1U : 0U;
	ctx.i2c_c_ok = (WF5805F_GetFailureCount(WF5805F_SENSOR_TANK) == 0U) ? 1U : 0U;
	Limit_GetState(&ctx.limits);
	UiPages_RenderSelfTest(&ctx);
}

static void Menu_RenderSensor(void)
{
	UiPages_SensorContext_t ctx;
	WF5805F_Reading_t reading;
	WaterDepth_State_t depth;

	ctx.air_pressure_valid = 0U;
	ctx.air_pressure_hpa_x100 = 0L;
	if ((WF5805F_GetReading(WF5805F_SENSOR_AIR, &reading) == WF5805F_OK) &&
	    (reading.valid != 0U))
	{
		ctx.air_pressure_valid = 1U;
		ctx.air_pressure_hpa_x100 = reading.pressure_hpa_x100;
	}

	ctx.basket_depth_valid = 0U;
	ctx.tank_depth_valid = 0U;
	ctx.basket_depth_mm_x10 = 0L;
	ctx.tank_depth_mm_x10 = 0L;
	if (WaterDepth_GetState(&depth) == WATER_DEPTH_OK)
	{
		ctx.basket_depth_valid = 1U;
		ctx.basket_depth_mm_x10 = depth.basket_depth_mm_x10;
		ctx.tank_depth_valid = 1U;
		ctx.tank_depth_mm_x10 = depth.tank_depth_mm_x10;
	}

	ctx.i2c_a_failures = WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_AIR);
	ctx.i2c_b_failures = WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_BASKET);
	ctx.i2c_c_failures = WF5805F_GetRecoveryFailureCount(WF5805F_SENSOR_TANK);
	UiPages_RenderSensor(&ctx);
}

static void Menu_RenderLimit(void)
{
	UiPages_LimitContext_t ctx;

	Limit_GetState(&ctx.limits);
	ctx.upper_blocked = Limit_IsAnyUpperActive();
	ctx.lower_blocked = Limit_IsAnyLowerActive();
	ctx.position_valid = 1U;
	ctx.position_mm_x10 = PositionTracker_GetMmX10();
	ctx.position_trusted = PositionTracker_IsTrusted();
	ctx.limit_mismatch = Limit_IsSameDirectionMismatch();
	UiPages_RenderLimit(&ctx);
}

static void Menu_RenderParam(void)
{
	UiPages_ParamContext_t ctx;

	ctx.param_id = s_param_id;
	ctx.record = &s_param_record;
	ctx.dirty = s_param_dirty;
	ctx.save_error = s_param_error;
	UiPages_RenderParam(&ctx);
}

static void Menu_RenderManual(void)
{
	UiPages_ManualContext_t ctx;
	WaterDepth_State_t depth;

	ctx.state = UI_PAGES_MANUAL_STOP;
	if (s_manual_hold_active != 0U)
	{
		ctx.state = (s_manual_direction == STEPPER_UM244_DIRECTION_UP) ?
		            UI_PAGES_MANUAL_UP : UI_PAGES_MANUAL_DOWN;
	}
	ctx.limit_blocked = 0U;
	if (s_manual_direction == STEPPER_UM244_DIRECTION_UP)
	{
		ctx.limit_blocked = Limit_IsDirectionBlocked(LIMIT_DIRECTION_UP);
	}
	else
	{
		ctx.limit_blocked = Limit_IsDirectionBlocked(LIMIT_DIRECTION_DOWN);
	}
	if (ctx.limit_blocked != 0U)
	{
		ctx.state = UI_PAGES_MANUAL_BLOCKED;
	}

	ctx.basket_depth_valid = 0U;
	ctx.basket_depth_mm_x10 = 0L;
	if (WaterDepth_GetState(&depth) == WATER_DEPTH_OK)
	{
		ctx.basket_depth_valid = 1U;
		ctx.basket_depth_mm_x10 = depth.basket_depth_mm_x10;
	}
	ctx.position_valid = 1U;
	ctx.position_mm_x10 = PositionTracker_GetMmX10();
	UiPages_RenderManual(&ctx);
}

static void Menu_RenderAlarm(void)
{
	UiPages_AlarmContext_t ctx;

	ctx.primary_error = ErrorManager_GetPrimary();
	ctx.active_error_count = Menu_CountActiveErrors();
	ctx.buzzer_muted = ErrorManager_IsBuzzerMuted();
	UiPages_RenderAlarm(&ctx);
}

static void Menu_RenderMaintenance(void)
{
	UiPages_MaintContext_t ctx;

	ctx.view = UI_PAGES_MAINT_MENU;
	ctx.menu_index = s_maintenance_menu_index;
	ctx.menu_count = 4U;
	ctx.line2 = 0;
	ctx.line3 = 0;
	ctx.motor_released = StepperUM244_IsMotorReleased();
	ctx.position_trusted = PositionTracker_IsTrusted();
	ctx.homing_busy = Homing_IsBusy();

	if (s_confirm != MENU_CONFIRM_NONE)
	{
		ctx.view = UI_PAGES_MAINT_CONFIRM;
		if (s_confirm == MENU_CONFIRM_ENTER_MAINTENANCE)
		{
			ctx.line2 = "MAINT";
			ctx.line3 = "NO AUTO MOVE";
		}
		else if (s_confirm == MENU_CONFIRM_HOME_ZERO)
		{
			ctx.line2 = "HOME";
			ctx.line3 = "MOTOR WILL MOVE";
		}
		else
		{
			ctx.line2 = "MF";
			ctx.line3 = (StepperUM244_IsMotorReleased() != 0U) ? "MOTOR HOLD" : "MOTOR RELEASE";
		}
	}
	else if (s_maintenance_debug != 0U)
	{
		ctx.view = UI_PAGES_MAINT_DEBUG;
	}

	UiPages_RenderMaintenance(&ctx);
}

static void Menu_RenderCurrent(void)
{
	switch (s_page)
	{
	case MENU_PAGE_SELF_TEST:
		Menu_RenderSelfTest();
		break;
	case MENU_PAGE_SENSOR:
		Menu_RenderSensor();
		break;
	case MENU_PAGE_LIMIT:
		Menu_RenderLimit();
		break;
	case MENU_PAGE_PARAM:
		Menu_RenderParam();
		break;
	case MENU_PAGE_MANUAL:
		Menu_RenderManual();
		break;
	case MENU_PAGE_ALARM:
		Menu_RenderAlarm();
		break;
	case MENU_PAGE_MAINTENANCE:
		Menu_RenderMaintenance();
		break;
	case MENU_PAGE_MAIN:
	default:
		Menu_RenderMain();
		break;
	}
}

void Menu_Init(uint32_t now_ms)
{
	s_page = MENU_PAGE_MAIN;
	s_return_page = MENU_PAGE_MAIN;
	s_in_maintenance = 0U;
	s_maintenance_menu_index = 0U;
	s_maintenance_debug = 0U;
	s_confirm = MENU_CONFIRM_NONE;
	s_param_id = UI_PAGES_PARAM_INITIAL_DEPTH;
	s_param_dirty = 0U;
	s_param_error = 0U;
	s_last_refresh_ms = now_ms;
	s_last_param_repeat_ms = now_ms;
	s_beep_off_ms = 0UL;
	s_manual_hold_active = 0U;
	s_manual_chunk_active = 0U;
	s_manual_direction = STEPPER_UM244_DIRECTION_DOWN;
	Menu_LoadParams();
	Menu_RequestRenderNow(now_ms);
}

void Menu_Update(uint32_t now_ms, uint16_t key_events)
{
	Menu_ServiceBuzzer(now_ms);
	Menu_HandleKeys(now_ms, key_events);
	Menu_ServiceParamRepeat(now_ms);
	Menu_ServiceManualMove(now_ms);

	if ((key_events != 0U) || Menu_TimeElapsed(now_ms, s_last_refresh_ms, BOARD_UI_REFRESH_MS) != 0U)
	{
		s_last_refresh_ms = now_ms;
		Menu_RenderCurrent();
	}
}
