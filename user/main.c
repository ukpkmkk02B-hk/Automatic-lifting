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
#include "menu.h"

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
	Menu_Init(g_app_ms);
	
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
		// Stage 7：菜单统一处理 OLED 页面、参数编辑、报警静音和维护入口。
		Menu_Update(g_app_ms, key_events);
	}
}
