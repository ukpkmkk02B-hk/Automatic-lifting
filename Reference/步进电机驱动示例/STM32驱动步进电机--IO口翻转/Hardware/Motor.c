#include "stm32f10x.h"
#include "Delay.h"

void Motor_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);//开启GPIOA的时钟
	
	GPIO_InitTypeDef  GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_4 | GPIO_Pin_3;  //PA5->EN;PA4->DIR;PA3->PWM
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  GPIO_SetBits(GPIOA, GPIO_Pin_5 | GPIO_Pin_4| GPIO_Pin_3);	
}

//dir方向，1正向0反向；num圈数；speed是脉冲高低电平时间
void Motor_Run(uint32_t dir,uint32_t num,uint32_t speed)  
{
	if(dir==1)
	{
		GPIO_SetBits(GPIOA, GPIO_Pin_4);		
	}
	
	if(dir==0)
	{
		GPIO_ResetBits(GPIOA, GPIO_Pin_4);			
	}
	
	for(uint32_t i=0;i<=(num*3200);i++)  //电平翻转2次为一个脉冲，所以旋转一周需要3200个脉冲，
	{		
		Delay_us(speed);                   //高低电平持续时间，脉冲频率
		GPIOA->ODR ^= GPIO_Pin_3;  				 //翻转PA3输出电平
	}	
}

