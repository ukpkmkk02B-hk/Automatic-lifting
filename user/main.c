#include "stm32f10x.h"                  // Device header
#include "oled.h"
#include "buzzer.h"
#include "limit.h"
#include "key_scan.h"
#include "LED.h"
#include "wf5805f.h"
#include "water_depth.h"
#include "error_manager.h"
#include "stepper_um244.h"
#include "position_tracker.h"
#include "homing.h"
#include "param_store.h"

static uint32_t g_app_ms;

// 函    数：App_TimebaseInit
// 参    数：无
// 返 回 值：无
// 注意事项：当前阶段使用 SysTick COUNTFLAG 轮询生成 1ms 时间基准，不在中断里放业务逻辑。
static void App_TimebaseInit(void)
{
	SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
	SysTick->VAL = 0U;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

// 函    数：App_TimebasePoll
// 参    数：无
// 返 回 值：无
// 注意事项：主循环每看到一次 COUNTFLAG 累加 1ms，供滤波、显示和非阻塞调度使用。
static void App_TimebasePoll(void)
{
	if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U)
	{
		g_app_ms++;
	}
}

// 函    数：App_ShowDepthLine
// 参    数：line OLED 行号；label 字段标签；depth_mm_x10 水深，单位 mm_x10。
// 返 回 值：无
// 注意事项：OLED 当前按 4 行 ASCII 字符页面使用，depth_mm_x10 显示为 mm 并保留 1 位小数。
static void App_ShowDepthLine(uint8_t line, char label, int32_t depth_mm_x10)
{
	uint32_t value;

	// 先清行避免旧数字残留；当前阶段不依赖中文字库。
	OLED_ShowString(line, 1, "                ");
	OLED_ShowChar(line, 1, label);
	OLED_ShowString(line, 2, ":");

	if (depth_mm_x10 < 0)
	{
		value = (uint32_t)(-depth_mm_x10);
		OLED_ShowChar(line, 4, '-');
	}
	else
	{
		value = (uint32_t)depth_mm_x10;
		OLED_ShowChar(line, 4, ' ');
	}

	OLED_ShowNum(line, 5, (value / 10U) % 10000U, 4);
	OLED_ShowChar(line, 9, '.');
	OLED_ShowNum(line, 10, value % 10U, 1);
	// depth_mm_x10 显示成 mm，保留 1 位小数。
	OLED_ShowString(line, 11, "mm");
}

// 函    数：App_ShowErrorLine
// 参    数：无
// 返 回 值：无
// 注意事项：这里只显示最高优先级错误和故障级别；故障清除仍必须走 error_manager 恢复流程。
static void App_ShowErrorLine(void)
{
	ErrorCode_t primary;

	primary = ErrorManager_GetPrimary();
	OLED_ShowString(4, 1, "ERR ");
	if (primary == ERROR_CODE_E_NONE)
	{
		OLED_ShowString(4, 5, "NONE       ");
	}
	else
	{
		OLED_ShowNum(4, 5, (uint32_t)primary, 2);
		if (ErrorManager_HasFault() != 0U)
		{
			// 这里只显示故障级别；故障清除仍必须走 error_manager 恢复流程。
			OLED_ShowString(4, 8, "FAULT ");
		}
		else
		{
			OLED_ShowString(4, 8, "WARN  ");
		}
	}
}

// 函    数：App_ShowStage3State
// 参    数：无
// 返 回 值：无
// 注意事项：当前 main.c 仍是阶段性集成页面，尚未接入完整 app_state/UI 菜单。
static void App_ShowStage3State(void)
{
	WaterDepth_State_t state;

	OLED_ShowString(1, 1, "STAGE3 DEPTH    ");

	if (WaterDepth_GetState(&state) == WATER_DEPTH_OK)
	{
		App_ShowDepthLine(2, 'B', state.basket_depth_mm_x10);
		App_ShowDepthLine(3, 'T', state.tank_depth_mm_x10);
	}
	else
	{
		OLED_ShowString(2, 1, "B: WAIT         ");
		OLED_ShowString(3, 1, "T: WAIT         ");
	}

	App_ShowErrorLine();
}

// 函    数：App_HandleKeyEvents
// 参    数：events KeyScan_GetEvents() 返回的事件位图。
// 返 回 值：无
// 注意事项：当前仅用于阶段性按键验证，不代表最终菜单/报警静音逻辑。
static void App_HandleKeyEvents(uint16_t events)
{
	if ((events & KEY_SCAN_EVENT_KEY1_SHORT) != 0U)
	{
		// 阶段性按键验证：KEY1 短按打开蜂鸣器。
		Buzzer_On();
	}
	if ((events & (KEY_SCAN_EVENT_KEY2_SHORT |
	               KEY_SCAN_EVENT_PAUSE_SHORT |
	               KEY_SCAN_EVENT_PAGE_SHORT |
	               KEY_SCAN_EVENT_MAINTENANCE_ENTRY)) != 0U)
	{
		// 阶段性按键验证：任一其它确认/页面/维护事件关闭蜂鸣器。
		Buzzer_Off();
	}
}

// 函    数：App_UpdateLeds
// 参    数：now_ms 系统毫秒时间戳。
// 返 回 值：无
// 注意事项：LED 闪烁使用时间戳差值，避免 Delay 阻塞主循环。
static void App_UpdateLeds(uint32_t now_ms)
{
	static uint32_t last_led1_ms;
	static uint32_t last_led2_ms;

	if ((uint32_t)(now_ms - last_led1_ms) >= 500U)
	{
		// LED1 当前 500ms 翻转一次，用作主循环仍在运行的状态提示。
		last_led1_ms = now_ms;
		LED1_Turn();
	}

	if ((uint32_t)(now_ms - last_led2_ms) >= 1000U)
	{
		last_led2_ms = now_ms;
		LED2_Turn();
	}
}

// 函    数：main
// 参    数：无
// 返 回 值：不会返回。
// 注意事项：初始化顺序先建立时间基准和安全输出，再启动传感器/OLED 显示；当前仍是阶段性主循环。
int main(void)
{
	uint32_t last_display_ms;
	uint16_t key_events;

	SystemCoreClockUpdate();
	// 初始化顺序先建立时间基准和安全输出，再启动传感器/OLED 显示。
	App_TimebaseInit();
	Buzzer_Init();
	LED_Init();
	Limit_Init();
	KeyScan_Init();
	ErrorManager_Init();
	// 阶段 6：读取 Flash A/B 参数页；无有效记录时载入默认值并锁存 W_PARAM_DEFAULT。
	(void)ParamStore_Init(g_app_ms);
	StepperUM244_Init();
	PositionTracker_Init();
	Homing_Init();
	WaterDepth_Init();
	WF5805F_InitAll();
	OLED_Init();
	OLED_Clear();
	App_ShowStage3State();
	last_display_ms = 0U;
	
	while (1)
	{
		App_TimebasePoll();
		// 主循环按“安全输入 -> 运动服务 -> 传感器/水深 -> 显示”的顺序轮询。
		Limit_Update(g_app_ms);
		KeyScan_Update(g_app_ms);
		StepperUM244_Poll(g_app_ms);
		PositionTracker_ServiceSafety();
		Homing_Update(g_app_ms);
		App_UpdateLeds(g_app_ms);
		WF5805F_Update(g_app_ms);
		WaterDepth_Update(g_app_ms);

		key_events = KeyScan_GetEvents();
		if (key_events != 0U)
		{
			App_HandleKeyEvents(key_events);
		}

		if ((uint32_t)(g_app_ms - last_display_ms) >= 250U)
		{
			// OLED 以 250ms 周期刷新，避免过度占用软件 I2C 时间。
			last_display_ms = g_app_ms;
			App_ShowStage3State();
		}
	}
}
