#include "stm32f10x.h"
#include "led.h"
#include "usart.h"
#include "delay.h"
#include "oled.h"
#include "water.h"
#include "adcx.h"

/*****************辰哥单片机设计******************
											STM32
 * 项目			:	非接触式水位传感器实验                     
 * 版本			: V1.0
 * 日期			: 2025.1.28
 * MCU			:	STM32F103C8T6
 * 接口			:	参看water.h										
 * IP账号		:	辰哥单片机设计（同BILIBILI|抖音|快手|小红书|CSDN|公众号|视频号等）
 * 作者			:	辰哥 
 * 工作室		: 异方辰电子工作室
 * 讲解视频	:	https://www.bilibili.com/video/BV15XFQebE7i/?share_source=copy_web
 * 官方网站	:	www.yfcdz.cn

**********************BEGIN***********************/

u16 value;
u8 buff[30];//参数显示缓存数组

int main(void)
{ 
	
  SystemInit();//配置系统时钟为72M	
	delay_init(72);
	LED_Init();
	LED_On();
	WATER_Init();
	USART1_Config();//串口初始化
	
	OLED_Init();
	printf("Start \n");
	delay_ms(1000);
	
	OLED_Clear();
	//显示“水位:”
	OLED_ShowChinese(0,0,0,16,1);
	OLED_ShowChinese(16,0,1,16,1);

	OLED_ShowChar(32,0,':',16,1);
	
	

  while (1)
  {
		LED_Toggle();
		value = WATER_GetData();  
		
		printf("水位: %d\r\n",value);
		OLED_ShowNum(80,0,value,1,16,1);


		delay_ms(200);

  }
	
}


