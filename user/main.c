#include "stm32f10x.h"                  // Device header
#include "oled.h"
#include "buzzer.h"
#include "limit.h"
#include "key_scan.h"
#include "LED.h"

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

static void App_ShowLimitState(void)
{
	OLED_ShowString(1, 1, "STAGE1 GPIO    ");
	OLED_ShowString(2, 1, "LU");
	OLED_ShowNum(2, 3, Limit_IsActive(LIMIT_LEFT_UPPER), 1);
	OLED_ShowString(2, 5, "LD");
	OLED_ShowNum(2, 7, Limit_IsActive(LIMIT_LEFT_LOWER), 1);
	OLED_ShowString(2, 9, "RU");
	OLED_ShowNum(2, 11, Limit_IsActive(LIMIT_RIGHT_UPPER), 1);
	OLED_ShowString(2, 13, "RD");
	OLED_ShowNum(2, 15, Limit_IsActive(LIMIT_RIGHT_LOWER), 1);
	OLED_ShowString(3, 1, "U");
	OLED_ShowNum(3, 2, Limit_IsAnyUpperActive(), 1);
	OLED_ShowString(3, 4, "D");
	OLED_ShowNum(3, 5, Limit_IsAnyLowerActive(), 1);
	OLED_ShowString(3, 7, "M");
	OLED_ShowNum(3, 8, Limit_IsSameDirectionMismatch(), 1);
	OLED_ShowString(3, 10, "BZ");
	OLED_ShowNum(3, 12, Buzzer_IsOn(), 1);
}

static void App_ShowKeyEvents(uint16_t events)
{
	OLED_ShowString(4, 1, "KEY EVT ");
	OLED_ShowHexNum(4, 9, events, 4);
	OLED_ShowString(4, 13, "   ");
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
	OLED_Init();
	OLED_Clear();
	App_ShowLimitState();
	App_ShowKeyEvents(0U);
	last_display_ms = 0U;
	
	while (1)
	{
		App_TimebasePoll();
		Limit_Update(g_app_ms);
		KeyScan_Update(g_app_ms);
		App_UpdateLeds(g_app_ms);

		key_events = KeyScan_GetEvents();
		if (key_events != 0U)
		{
			App_HandleKeyEvents(key_events);
			App_ShowKeyEvents(key_events);
		}

		if ((uint32_t)(g_app_ms - last_display_ms) >= 200U)
		{
			last_display_ms = g_app_ms;
			App_ShowLimitState();
		}
	}
}
