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
#include "app_state.h"

// SysTick_Handler 每 1ms 递增，主循环只读取时间戳；避免 OLED/I2C/Flash 占用期间丢失毫秒 tick。
volatile uint32_t g_app_ms;

// 函    数：App_TimebaseInit
// 参    数：无
// 返 回 值：无
// 注意事项：SysTick 中断只递增 g_app_ms，不放业务逻辑；主循环使用时间戳差值做非阻塞调度。
static void App_TimebaseInit(void)
{
	SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
	SysTick->VAL = 0U;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
	                SysTick_CTRL_TICKINT_Msk |
	                SysTick_CTRL_ENABLE_Msk;
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

// 函    数：App_IsBasketCommandMotionActive
// 参    数：无
// 返 回 值：1 表示 STEP 命令处于建立/输出/保持阶段；0 表示空闲或故障锁定。
// 注意事项：FAULT 不是命令运动，不能用于刷新水深突变检测的 basket 基准。
static uint8_t App_IsBasketCommandMotionActive(void)
{
	StepperUM244_State_t stepper_state;

	stepper_state = StepperUM244_GetState();
	if ((stepper_state == STEPPER_UM244_STATE_DIR_WAIT) ||
	    (stepper_state == STEPPER_UM244_STATE_RUNNING) ||
	    (stepper_state == STEPPER_UM244_STATE_HOLD_WAIT))
	{
		return 1U;
	}

	return 0U;
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
	AppState_Init(g_app_ms);
	
	while (1)
	{
		// 主循环按“安全输入 -> 运动服务 -> 传感器/水深 -> 显示”的顺序轮询。
		Limit_Update(g_app_ms);
		KeyScan_Update(g_app_ms);
		StepperUM244_Poll(g_app_ms);
		PositionTracker_ServiceSafety();
		Homing_Update(g_app_ms);
		App_UpdateLeds(g_app_ms);
		WF5805F_Update(g_app_ms);
		// 框篮命令运动期间，水深模块只抑制 basket_depth 自身变化导致的突变误报；tank_depth 安全判断保持有效。
		WaterDepth_SetBasketMotionActive(App_IsBasketCommandMotionActive());
		WaterDepth_Update(g_app_ms);

		key_events = KeyScan_GetEvents();
		// 阶段 8：主状态机接管自检、恢复、自动打盹、故障优先级和菜单意图。
		AppState_Update(g_app_ms, key_events);
	}
}
