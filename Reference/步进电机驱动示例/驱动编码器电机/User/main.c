#include "stm32f10x.h"            
#include "Key.h"
#include "OLED.h"
#include "Timer.h"
#include "Motor.h"
#include "MotorRun.h"
#include "Encoder.h"

int16_t Speed;
int8_t KeyNum;
 
int main(void)
{
	Key_Init();
	OLED_Init();
	Timer_Init();
	Motor_Init();
	Encoder_Init();
		
	OLED_ShowString(1, 3, "Speed:");
	
	while (1)
	{
		KeyNum=Key_GetNum();
        //电机旋转
		if(KeyNum==1)
		{
			run(50);
		}
		OLED_ShowSignedNum(1, 9, Speed, 5); //显示速度
	}
}
 
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
        //每隔一段时间获取速度
		Speed = Encoder_Get();
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}
