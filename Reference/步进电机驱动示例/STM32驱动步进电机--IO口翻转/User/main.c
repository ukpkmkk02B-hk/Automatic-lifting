#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Motor.h"
#include "Key.h"
#include "Stdbool.h"

uint8_t flag=1;
int main(void)
{	
	Motor_Init();
	Key_Init();
	Delay_ms(1000);	
	while (1)
	{					
		if(flag == 1)
		{
			GPIO_SetBits(GPIOA, GPIO_Pin_5);
			Motor_Run(1,3,300);
			Delay_ms(1000);
			Motor_Run(0,2,600);
			Delay_ms(1000);
			}						
		else
		{
			GPIO_ResetBits(GPIOA, GPIO_Pin_5);    //设置PA5为低电平，使能功能开启
		}	
	}		
}

void EXTI0_IRQHandler(void)
	{
		if (EXTI_GetITStatus(EXTI_Line0) == SET)
		{			
				if (flag == 1)
				{						
					GPIO_ResetBits(GPIOA, GPIO_Pin_5);
					flag = 0;				
				}
				else flag=1;
				EXTI_ClearITPendingBit(EXTI_Line0);								
		}
	}

