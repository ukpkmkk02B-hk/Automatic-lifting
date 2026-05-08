#include "stm32f10x.h"                  
#include "Motor.h"
#include "PWM.h"
	
//小车调速函数
void Speed_Control(uint16_t Compare)
{
	Compare*=200; 				//100->20000	
	PWM_SetCompare1(Compare); 	//调速
}
 
//小车前进函数
void run(uint16_t Compare)
{
	Speed_Control(Compare);
	Right_moto_go();       		//右电机往前
}
 
//小车后退函数
void backrun(uint16_t Compare)
{
	Speed_Control(Compare);
	Right_moto_back();     		//右电机往后
}
 
//小车停车函数
void stop(void)         		
{   	
	Right_moto_Stop();     		//右电机停止
}
