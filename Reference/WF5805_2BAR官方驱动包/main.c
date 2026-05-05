#include "wfsensor.h"
/******************************************************************************************
** 函数名称: main
** 函数描述: main函数   
** 输入参数: 无
** 输出参数: 无 
*******************************************************************************************/
void main(void)
{
	while (1)
	{
		WFSensor_indicateGroupConvert(); //发送转换命令		
		while (WFSensor_WaitFinish() != 0x01)
		{
			delayMs(2);
		}
		WFSensor_getTPData(); //读取数据
		calculatePress(); //换算数据
		delayMs(1000);
	}
}
