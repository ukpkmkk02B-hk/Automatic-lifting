#ifndef __WATER_H
#define	__WATER_H
#include "stm32f10x.h"
#include "adcx.h"
#include "../delay.h"
#include "math.h"

/*****************���絥Ƭ�����******************
											STM32
 * �ļ�			:	�ǽӴ�ʽˮλ������h�ļ�                   
 * �汾			: V1.0
 * ����			: 2025.1.28
 * MCU			:	STM32F103C8T6
 * �ӿ�			:	������										
 * IP�˺�		:	���絥Ƭ����ƣ�ͬBILIBILI|����|����|С����|CSDN|���ں�|��Ƶ�ŵȣ�
 * ����			:	���� 
 * ������		: �췽�����ӹ�����
 * ������Ƶ	:	https://www.bilibili.com/video/BV15XFQebE7i/?share_source=copy_web
 * �ٷ���վ	:	www.yfcdz.cn

**********************BEGIN***********************/

/***************�����Լ��������****************/
// WATER GPIO�궨��

#define		WATER_GPIO_CLK								RCC_APB2Periph_GPIOA
#define 	WATER_GPIO_PORT								GPIOA
#define 	WATER_GPIO_PIN								GPIO_Pin_1			


/*********************END**********************/


void WATER_Init(void);
uint16_t WATER_GetData(void);

#endif /* WATER_H_ */

