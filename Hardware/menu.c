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

// 判断无符号毫秒时间是否到期，兼容 SysTick 回绕。
static uint8_t Menu_TimeElapsed(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
	return ((uint32_t)(now_ms - last_ms) >= interval_ms) ? 1U : 0U;
}

// 强制下一轮 Menu_Update 刷新 OLED，用于初始化或按键后立即更新页面。
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

// 清空菜单到 app_state 的一次性意图；长按手动意图在 Menu_GetIntents 中单独补充。
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
	intents->params_save_request = 0U;
	intents->manual_up_hold = 0U;
	intents->manual_down_hold = 0U;
}

// 启动一次 UI 短鸣，截止时间到后由 Menu_ServiceBuzzer 关闭。
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

// 从 Flash 读取可编辑参数；读取失败时加载默认值供页面显示和后续保存。
static void Menu_LoadParams(void)
{
	if (ParamStore_Load(&s_param_record) != PARAM_STORE_STATUS_OK)
	{
		ParamStore_LoadDefaults(&s_param_record);
	}
}

// 统计当前锁存/活动错误数量，用于报警页显示摘要。
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

// 切换到下一页；参数页会先在各参数项之间轮转，再退出参数页面。
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

// 进入维护确认页，确认完成或取消后回到 return_page。
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

// 按参数类型调整当前值，并立即做范围约束；真正写 Flash 必须等待保存确认。
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

// 提交当前参数页编辑结果；真正 Flash 写入由 app_state 合并当前运行态后完成。
static void Menu_SaveParam(uint32_t now_ms)
{
	if ((s_page != MENU_PAGE_PARAM) || (s_param_id == UI_PAGES_PARAM_MANUAL_SPEED))
	{
		return;
	}

	(void)now_ms;
	// 菜单只传递编辑后的参数快照；运行秒数、位置、水深和恢复状态由 app_state 当前状态提供。
	s_pending_intents.params_record = s_param_record;
	s_pending_intents.params_save_request = 1U;
}

// 处理参数页长按连续调整；重复周期由 BOARD_UI_PARAM_REPEAT_MS 限制。
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

// 报警页确认键只产生静音/确认意图，故障锁存是否允许清除由 app_state 判断。
static void Menu_HandleAlarmKey(uint32_t now_ms)
{
	(void)now_ms;
	s_pending_intents.alarm_ack = 1U;
}

// 维护页 OK 键分发校准、回零、电机释放确认和调试页面入口。
static void Menu_HandleMaintenanceOk(uint32_t now_ms)
{
	if (s_in_maintenance == 0U)
	{
		// 维护页可能只是被 PB0 翻到，还没有真正进入维护模式；
		// 此时 PB10 先进入维护确认页，避免“1 CAL AIR”看起来按键无响应。
		Menu_StartConfirm(MENU_CONFIRM_ENTER_MAINTENANCE, MENU_PAGE_MAINTENANCE);
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

// 处理维护确认页按键；PAGE 取消，PAUSE/OK 确认并生成对应意图。
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

// 汇总全部按键事件并转换为菜单状态或 app_state 意图；不直接驱动电机和 Flash 以外的硬件动作。
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

	if (((key_events & KEY_SCAN_EVENT_PAUSE_SHORT) != 0U) &&
	    (s_page == MENU_PAGE_MAIN))
	{
		// 主页面保留 PB10 启停自动和报警确认；维护/参数/报警页由各自页面上下文处理，
		// 防止维护动作和故障静音/清除意图在同一轮按键中互相抢占。
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

// 根据故障、维护、手动页和应用快照决定主页面的显示模式。
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

// 组织主页面上下文；优先使用 app_state 快照，未就绪时用参数和传感器当前值降级显示。
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

// 渲染自检页面；自检进行中优先显示 app_state/self_test 提供的倒计时快照。
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

// 渲染传感器诊断页面，显示空气压力、水深和三路 I2C 恢复失败计数。
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

// 渲染限位/位置页面，供维护时确认上下限和位置跟踪可信状态。
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

// 渲染参数编辑页，显示当前参数、未保存标志和保存失败状态。
static void Menu_RenderParam(void)
{
	UiPages_ParamContext_t ctx;

	ctx.param_id = s_param_id;
	ctx.record = &s_param_record;
	ctx.dirty = s_param_dirty;
	ctx.save_error = s_param_error;
	UiPages_RenderParam(&ctx);
}

// 渲染手动点动页；页面状态来自菜单长按标志和限位禁止状态。
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

// 渲染报警页，展示最高优先级错误、错误等级、错误数量和蜂鸣器静音状态。
static void Menu_RenderAlarm(void)
{
	UiPages_AlarmContext_t ctx;

	ctx.primary_error = ErrorManager_GetPrimary();
	ctx.primary_error_level = ErrorManager_GetLevel(ctx.primary_error);
	ctx.active_error_count = Menu_CountActiveErrors();
	ctx.buzzer_muted = ErrorManager_IsBuzzerMuted();
	UiPages_RenderAlarm(&ctx);
}

// 渲染维护页，包含维护菜单、确认页和调试读数页三种视图。
static void Menu_RenderMaintenance(void)
{
	UiPages_MaintContext_t ctx;
	int32_t basket_zero_offset;
	int32_t tank_zero_offset;

	ctx.view = UI_PAGES_MAINT_MENU;
	ctx.menu_index = s_maintenance_menu_index;
	ctx.menu_count = 4U;
	ctx.line2 = 0;
	ctx.line3 = 0;
	ctx.motor_released = StepperUM244_IsMotorReleased();
	ctx.position_trusted = PositionTracker_IsTrusted();
	ctx.homing_busy = Homing_IsBusy();
	ctx.zero_offsets_valid = WaterDepth_UnpackZeroOffsets(s_param_record.air_offset_hpa_x100,
	                                                      &basket_zero_offset,
	                                                      &tank_zero_offset);
	ctx.basket_zero_offset_mm_x10 = WaterDepth_ConvertPressureDiffToMmX10(basket_zero_offset);
	ctx.tank_zero_offset_mm_x10 = WaterDepth_ConvertPressureDiffToMmX10(tank_zero_offset);

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

// 根据当前菜单页分发渲染；强制自检页用于启动阶段覆盖普通页面切换。
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

// 函    数：Menu_Init
// 参    数：now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：初始化菜单页、参数缓存、短鸣状态和待处理意图；不会启动自动运行。
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

// 函    数：Menu_SetAppSnapshot
// 参    数：snapshot app_state 提供的显示快照；传入 0 表示快照无效。
// 返 回 值：无
// 注意事项：菜单只拷贝快照用于显示，不反向修改 app_state。
void Menu_SetAppSnapshot(const Menu_AppSnapshot_t *snapshot)
{
	if (snapshot == 0)
	{
		s_app_snapshot.valid = 0U;
		return;
	}
	s_app_snapshot = *snapshot;
}

// 函    数：Menu_GetIntents
// 参    数：intents 输出一次性菜单意图。
// 返 回 值：无
// 注意事项：读取后会清空一次性意图；手动长按意图会按当前按键保持状态即时生成。
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

// 函    数：Menu_ReloadParams
// 参    数：无
// 返 回 值：无
// 注意事项：外部保存或恢复参数后调用，使菜单缓存与 Flash 记录重新同步。
void Menu_ReloadParams(void)
{
	Menu_LoadParams();
}

// 函    数：Menu_OnParamSaveResult
// 参    数：success 非 0 表示参数已由 app_state 合并当前运行态后写入 Flash；now_ms 当前系统毫秒时间戳。
// 返 回 值：无
// 注意事项：菜单只处理显示缓存和提示，禁止在此处再次写 Flash。
void Menu_OnParamSaveResult(uint8_t success, uint32_t now_ms)
{
	Menu_LoadParams();
	s_param_dirty = 0U;
	if (success != 0U)
	{
		s_param_error = 0U;
		ErrorManager_Clear(ERROR_CODE_W_PARAM_REJECTED);
	}
	else
	{
		s_param_error = 1U;
		ErrorManager_Set(ERROR_CODE_W_PARAM_REJECTED);
		Menu_StartShortBeep(now_ms);
	}
	Menu_RequestRenderNow(now_ms);
}

// 函    数：Menu_Update
// 参    数：now_ms 当前系统毫秒时间戳；key_events 本轮按键边沿/长按事件位。
// 返 回 值：无
// 注意事项：主循环周期调用；处理蜂鸣、按键、参数连发、手动意图和 OLED 周期刷新。
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
