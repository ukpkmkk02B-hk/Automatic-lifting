#include "stm32f10x.h"                  // Device header
#include "oled.h"
#include "buzzer.h"
#include "limit.h"
#include "key_scan.h"
#include "LED.h"
#include "wf5805f.h"
#include "water_depth.h"
#include "error_manager.h"

static uint32_t g_app_ms;

static void App_TimebaseInit(void)
{
	SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;
	SysTick->VAL = 0U;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

static void App_TimebasePoll(void)
{
	if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U)
	{
		g_app_ms++;
	}
}

static void App_ShowDepthLine(uint8_t line, char label, int32_t depth_mm_x10)
{
	uint32_t value;

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
	OLED_ShowString(line, 11, "mm");
}

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
			OLED_ShowString(4, 8, "FAULT ");
		}
		else
		{
			OLED_ShowString(4, 8, "WARN  ");
		}
	}
}

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

static void App_HandleKeyEvents(uint16_t events)
{
	if ((events & KEY_SCAN_EVENT_KEY1_SHORT) != 0U)
	{
		Buzzer_On();
	}
	if ((events & (KEY_SCAN_EVENT_KEY2_SHORT |
	               KEY_SCAN_EVENT_PAUSE_SHORT |
	               KEY_SCAN_EVENT_PAGE_SHORT |
	               KEY_SCAN_EVENT_MAINTENANCE_ENTRY)) != 0U)
	{
		Buzzer_Off();
	}
}

static void App_UpdateLeds(uint32_t now_ms)
{
	static uint32_t last_led1_ms;
	static uint32_t last_led2_ms;

	if ((uint32_t)(now_ms - last_led1_ms) >= 500U)
	{
		last_led1_ms = now_ms;
		LED1_Turn();
	}

	if ((uint32_t)(now_ms - last_led2_ms) >= 1000U)
	{
		last_led2_ms = now_ms;
		LED2_Turn();
	}
}

int main(void)
{
	uint32_t last_display_ms;
	uint16_t key_events;

	SystemCoreClockUpdate();
	App_TimebaseInit();
	Buzzer_Init();
	LED_Init();
	Limit_Init();
	KeyScan_Init();
	ErrorManager_Init();
	WaterDepth_Init();
	WF5805F_InitAll();
	OLED_Init();
	OLED_Clear();
	App_ShowStage3State();
	last_display_ms = 0U;
	
	while (1)
	{
		App_TimebasePoll();
		Limit_Update(g_app_ms);
		KeyScan_Update(g_app_ms);
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
			last_display_ms = g_app_ms;
			App_ShowStage3State();
		}
	}
}
