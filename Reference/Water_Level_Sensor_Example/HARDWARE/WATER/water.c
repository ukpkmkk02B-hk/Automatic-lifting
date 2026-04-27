#include "water.h"

/*****************辰哥单片机设计******************
											STM32
 * 文件			:	非接触式水位传感器c文件                   
 * 版本			: V1.0
 * 日期			: 2025.1.28
 * MCU			:	STM32F103C8T6
 * 接口			:	见代码								
 * IP账号		:	辰哥单片机设计（同BILIBILI|抖音|快手|小红书|CSDN|公众号|视频号等）
 * 作者			:	辰哥 
 * 工作室		: 异方辰电子工作室
 * 讲解视频	:	https://www.bilibili.com/video/BV15XFQebE7i/?share_source=copy_web
 * 官方网站	:	www.yfcdz.cn

**********************BEGIN***********************/

void WATER_Init(void)
{

		GPIO_InitTypeDef GPIO_InitStructure;
		
		RCC_APB2PeriphClockCmd (WATER_GPIO_CLK, ENABLE );	// 打开连接 传感器DO 的单片机引脚端口时钟
		GPIO_InitStructure.GPIO_Pin = WATER_GPIO_PIN;			// 配置连接 传感器DO 的单片机引脚模式
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;			// 设置为上拉输入
		
		GPIO_Init(WATER_GPIO_PORT, &GPIO_InitStructure);				// 初始化 
	
}

uint16_t WATER_GetData(void)
{
	
	uint16_t tempData;
	tempData = !GPIO_ReadInputDataBit(WATER_GPIO_PORT, WATER_GPIO_PIN);
	return tempData;
}


