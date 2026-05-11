#include "stm32f10x.h"                  // Device header
#include "oled.h"
#include "buzzer.h"
#include "limit.h"
#include "key_scan.h"
#include "LED.h"
#include "wf5805f.h"

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

static void App_ShowSensorLine(uint8_t line, char label, WF5805F_Sensor_t sensor)
{
	WF5805F_Reading_t reading;
	WF5805F_Status_t status;
	uint32_t pressure;

	status = WF5805F_GetReading(sensor, &reading);
	OLED_ShowChar(line, 1, label);

	if ((status == WF5805F_OK) && (reading.valid != 0U))
	{
		OLED_ShowString(line, 2, " OK P");
		if (reading.pressure_hpa_x100 < 0)
		{
			pressure = (uint32_t)(-reading.pressure_hpa_x100);
			OLED_ShowChar(line, 7, '-');
			OLED_ShowNum(line, 8, pressure % 100000UL, 5);
			OLED_ShowString(line, 13, "    ");
		}
		else
		{
			pressure = (uint32_t)reading.pressure_hpa_x100;
			OLED_ShowNum(line, 7, pressure % 1000000UL, 6);
			OLED_ShowString(line, 13, "    ");
		}
	}
	else
	{
		OLED_ShowString(line, 2, " ER F");
		OLED_ShowNum(line, 7, WF5805F_GetFailureCount(sensor) % 10000U, 4);
		OLED_ShowString(line, 11, "      ");
	}
}

static void App_ShowStage2State(void)
{
	OLED_ShowString(1, 1, "STAGE2 WF5805F  ");
	App_ShowSensorLine(2, 'A', WF5805F_SENSOR_AIR);
	App_ShowSensorLine(3, 'B', WF5805F_SENSOR_BASKET);
	App_ShowSensorLine(4, 'C', WF5805F_SENSOR_TANK);
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
	WF5805F_InitAll();
	OLED_Init();
	OLED_Clear();
	App_ShowStage2State();
	last_display_ms = 0U;
	
	while (1)
	{
		App_TimebasePoll();
		Limit_Update(g_app_ms);
		KeyScan_Update(g_app_ms);
		App_UpdateLeds(g_app_ms);
		WF5805F_Update(g_app_ms);

		key_events = KeyScan_GetEvents();
		if (key_events != 0U)
		{
			App_HandleKeyEvents(key_events);
		}

		if ((uint32_t)(g_app_ms - last_display_ms) >= 250U)
		{
			last_display_ms = g_app_ms;
			App_ShowStage2State();
		}
	}
}
