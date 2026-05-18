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
static StepperUM244_Direction_t s_manual_direction;
static Menu_AppSnapshot_t s_app_snapshot;
static Menu_Intents_t s_pending_intents;

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

static void Menu_ClearIntents(Menu_Intents_t *intents)
{
	if (intents == 0)
	{
		return;
	}

	intents->start_auto = 0U;
	intents->pause = 0U;
	intents->alarm_ack = 0U;
	intents->enter_maintenance = 0U;
	intents->exit_maintenance = 0U;
	intents->air_calibrate = 0U;
	intents->home_zero = 0U;
	intents->motor_release_toggle = 0U;
	intents->params_saved = 0U;
	intents->manual_up_hold = 0U;
	intents->manual_down_hold = 0U;
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

// 函    数：Menu_GetMaxDailyShallowMmX10
// 参    数：nap_pulses 单次打盹脉冲数，单位 pulse。
// 返 回 值：在最小打盹间隔限制下可实现的最大每日变浅量，单位 mm_x10/day。
// 注意事项：5min 最小间隔优先于菜单最大值；8 pulse 时最多约 2.8mm/day，3.0mm/day 需至少 9 pulse。
static int32_t Menu_GetMaxDailyShallowMmX10(uint16_t nap_pulses)
{
	uint32_t max_daily_pulses;
	uint32_t max_daily_mm_x10;

	if (nap_pulses == 0U)
	{
		return 0L;
	}

	max_daily_pulses = ((BOARD_SECONDS_PER_DAY * 1000UL) * (uint32_t)nap_pulses) /
	                   BOARD_NAP_MIN_INTERVAL_MS;
	max_daily_mm_x10 = (max_daily_pulses * 10UL) / BOARD_STEPPER_PULSE_PER_MM;
	if (max_daily_mm_x10 > (uint32_t)BOARD_DAILY_SHALLOW_MAX_MM_X10)
	{
		max_daily_mm_x10 = (uint32_t)BOARD_DAILY_SHALLOW_MAX_MM_X10;
	}

	return (int32_t)max_daily_mm_x10;
}

static void Menu_AdjustParam(int8_t delta)
{
	int32_t value;
	int32_t max_daily_mm_x10;

	if ((s_page != MENU_PAGE_PARAM) || (s_param_error != 0U) ||
	    (s_param_id == UI_PAGES_PARAM_MANUAL_SPEED))
	{
		return;
	}

	switch (s_param_id)
	{
	case UI_PAGES_PARAM_INITIAL_DEPTH:
		value = s_param_record.initial_target_mm_x10 + ((int32_t)delta * 10L);
		if (value < s_param_record.final_target_mm_x10)
		{
			value = s_param_record.final_target_mm_x10;
		}
		if (value < BOARD_TARGET_MIN_DEPTH_MM_X10)
		{
			value = BOARD_TARGET_MIN_DEPTH_MM_X10;
		}
		if (value > BOARD_TARGET_MAX_DEPTH_MM_X10)
		{
			value = BOARD_TARGET_MAX_DEPTH_MM_X10;
		}
		s_param_record.initial_target_mm_x10 = value;
		break;
	case UI_PAGES_PARAM_FINAL_DEPTH:
		value = s_param_record.final_target_mm_x10 + ((int32_t)delta * 10L);
		if (value < BOARD_TARGET_MIN_DEPTH_MM_X10)
		{
			value = BOARD_TARGET_MIN_DEPTH_MM_X10;
		}
		if (value > BOARD_TARGET_MAX_DEPTH_MM_X10)
		{
			value = BOARD_TARGET_MAX_DEPTH_MM_X10;
		}
		if (value > s_param_record.initial_target_mm_x10)
		{
			value = s_param_record.initial_target_mm_x10;
		}
		s_param_record.final_target_mm_x10 = value;
		break;
	case UI_PAGES_PARAM_DAILY_RATE:
		value = s_param_record.daily_shallow_mm_x10 + (int32_t)delta;
		if (value < 0L)
		{
			value = 0L;
		}
		if (value > BOARD_DAILY_SHALLOW_MAX_MM_X10)
		{
			value = BOARD_DAILY_SHALLOW_MAX_MM_X10;
		}
		max_daily_mm_x10 = Menu_GetMaxDailyShallowMmX10(s_param_record.nap_pulses);
		if (value > max_daily_mm_x10)
		{
			value = max_daily_mm_x10;
		}
		s_param_record.daily_shallow_mm_x10 = value;
		break;
	case UI_PAGES_PARAM_NAP_PULSE:
		value = (int32_t)s_param_record.nap_pulses + (int32_t)delta;
		if (value < 1L)
		{
			value = 1L;
		}
		if (value > (int32_t)BOARD_NAP_MAX_PULSES)
		{
			value = BOARD_NAP_MAX_PULSES;
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

	// 菜单调整参数后，s_param_record 的 crc16 仍是旧记录的校验值；
	// 保存接口会在写 Flash 前统一刷新 seq/crc16，并再次执行范围校验。
	status = ParamStore_SaveParameters(&s_param_record);

	if (status == PARAM_STORE_STATUS_OK)
	{
		s_param_dirty = 0U;
		ErrorManager_Clear(ERROR_CODE_W_PARAM_REJECTED);
		Menu_LoadParams();
		s_pending_intents.params_saved = 1U;
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

// 函    数：Menu_ServiceManualMove
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：阶段 8 起菜单只维护“长按保持”意图，实际 STEP 输出由 app_state 统一执行。
static void Menu_ServiceManualMove(uint32_t now_ms)
{
	uint8_t pressed;

	(void)now_ms;

	if (s_page != MENU_PAGE_MANUAL)
	{
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
		s_manual_hold_active = 0U;
		return;
	}
}

static void Menu_HandleAlarmKey(uint32_t now_ms)
{
	(void)now_ms;
	s_pending_intents.alarm_ack = 1U;
}

static void Menu_HandleMaintenanceOk(uint32_t now_ms)
{
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
		s_pending_intents.air_calibrate = 1U;
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
	(void)now_ms;

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
		s_pending_intents.enter_maintenance = 1U;
		break;
	case MENU_CONFIRM_HOME_ZERO:
		s_pending_intents.home_zero = 1U;
		s_page = MENU_PAGE_MAINTENANCE;
		break;
	case MENU_CONFIRM_MOTOR_RELEASE:
		s_pending_intents.motor_release_toggle = 1U;
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
			s_pending_intents.exit_maintenance = 1U;
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

	if ((key_events & KEY_SCAN_EVENT_PAUSE_SHORT) != 0U)
	{
		if (ErrorManager_HasFault() != 0U)
		{
			s_pending_intents.alarm_ack = 1U;
		}
		else if ((s_page == MENU_PAGE_MAIN) && (s_app_snapshot.valid != 0U))
		{
			if (s_app_snapshot.mode == UI_PAGES_MODE_PAUSED)
			{
				s_pending_intents.start_auto = 1U;
			}
			else if (s_app_snapshot.mode == UI_PAGES_MODE_AUTO)
			{
				s_pending_intents.pause = 1U;
			}
		}
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
	if (s_app_snapshot.valid != 0U)
	{
		return s_app_snapshot.mode;
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
	if (s_app_snapshot.valid != 0U)
	{
		ctx.target_depth_valid = s_app_snapshot.target_depth_valid;
		ctx.target_depth_mm_x10 = s_app_snapshot.target_depth_mm_x10;
		ctx.run_days = s_app_snapshot.run_days;
		ctx.motion_text = s_app_snapshot.motion_text;
		ctx.notice_text = s_app_snapshot.notice_text;
		ctx.next_nap_valid = s_app_snapshot.next_nap_valid;
		ctx.next_nap_remaining_s = s_app_snapshot.next_nap_remaining_s;
		ctx.today_done_pulses = s_app_snapshot.today_done_pulses;
		ctx.nap_pulses = s_app_snapshot.nap_pulses;
	}
	else
	{
		ctx.target_depth_valid = 1U;
		ctx.target_depth_mm_x10 = s_param_record.initial_target_mm_x10;
		run_days = (s_param_record.total_run_seconds / 86400UL) + 1UL;
		if (run_days > 999UL)
		{
			run_days = 999UL;
		}
		ctx.run_days = (uint16_t)run_days;
		ctx.next_nap_valid = 0U;
		ctx.next_nap_remaining_s = 0UL;
		ctx.today_done_pulses = (s_param_record.today_pulses_done > 999UL) ?
		                        999U : (uint16_t)s_param_record.today_pulses_done;
		ctx.nap_pulses = s_param_record.nap_pulses;
		ctx.notice_text = 0;
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
	}
	ctx.mode = Menu_GetDisplayMode();
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

	if (s_app_snapshot.valid != 0U)
	{
		ctx = s_app_snapshot.self_test;
		UiPages_RenderSelfTest(&ctx);
		return;
	}

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
	ctx.primary_error_level = ErrorManager_GetLevel(ctx.primary_error);
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
	ctx.air_reference_valid = (s_param_record.air_offset_hpa_x100 != 0L) ? 1U : 0U;
	ctx.air_reference_hpa_x100 = s_param_record.air_offset_hpa_x100;

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
	if ((s_app_snapshot.valid != 0U) && (s_app_snapshot.force_self_test_page != 0U))
	{
		Menu_RenderSelfTest();
		return;
	}

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
	s_manual_direction = STEPPER_UM244_DIRECTION_DOWN;
	s_app_snapshot.valid = 0U;
	s_app_snapshot.force_self_test_page = 0U;
	Menu_ClearIntents(&s_pending_intents);
	Menu_LoadParams();
	Menu_RequestRenderNow(now_ms);
}

void Menu_SetAppSnapshot(const Menu_AppSnapshot_t *snapshot)
{
	if (snapshot == 0)
	{
		s_app_snapshot.valid = 0U;
		return;
	}
	s_app_snapshot = *snapshot;
}

void Menu_GetIntents(Menu_Intents_t *intents)
{
	if (intents == 0)
	{
		Menu_ClearIntents(&s_pending_intents);
		return;
	}

	*intents = s_pending_intents;
	if ((s_page == MENU_PAGE_MANUAL) && (s_manual_hold_active != 0U))
	{
		if ((s_manual_direction == STEPPER_UM244_DIRECTION_UP) &&
		    (KeyScan_IsPressed(KEY_SCAN_KEY2) != 0U))
		{
			intents->manual_up_hold = 1U;
		}
		else if ((s_manual_direction == STEPPER_UM244_DIRECTION_DOWN) &&
		         (KeyScan_IsPressed(KEY_SCAN_KEY1) != 0U))
		{
			intents->manual_down_hold = 1U;
		}
	}

	Menu_ClearIntents(&s_pending_intents);
}

void Menu_ReloadParams(void)
{
	Menu_LoadParams();
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
