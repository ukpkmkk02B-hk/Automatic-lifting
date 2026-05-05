/**************************************************************
WFSensor传感器驱动文件
***************************************************************/
#include "i2c.h"
#include "wfsensor.h"
#define		WFSensorIICDevice  0XDA			
#define		CMD_GROUP_CONVERT  0X0A			
u8 QT[5];
/********************************************************************
* Function Name  : WFSensor_ReadByte.
* Description    : IIC 写入数据
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void WFSensor_WriteByte(u8 addr,u8 Data)
{
	SCL_OUTPUT();
	iic_start();
	iic_write_byte(WFSensorIICDevice); //写设备号
	iic_wait_ack();
	iic_write_byte(addr); //写要写入的寄存器地址
	iic_wait_ack();
	iic_write_byte(Data); //读取数据
	iic_wait_ack();
	iic_stop();
	SDA_INPUT();
	SCL_INPUT();
}
/********************************************************************
* Function Name  : WFSensor_ReadByte.
* Description    : IIC 写入数据
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void WFSensor_ReadContiune(u8 addr,u8 *p,u8 Length)
{
	u8 i = 0;
//	SDA_OUTPUT();
	SCL_OUTPUT();
	iic_start();
	iic_write_byte(WFSensorIICDevice); //写设备号
	iic_wait_ack();
	iic_write_byte(addr); //写要写入的寄存器地址
	iic_wait_ack();
	iic_start();
	iic_write_byte(WFSensorIICDevice|0X01); //读取数据
	iic_wait_ack();
	for(i=0;i<Length;i++)
	{
		p[i]= iic_read_byte();
		if(i < Length-1)
		{
			iic_ack();
		}
	}
	
	iic_noAck();
	iic_stop();
	SDA_INPUT();
	SCL_INPUT();
}

u8 WFSensor_WaitFinish()
{
	//delayMs(50);
	u8 i = 0;
	SCL_OUTPUT();
	iic_start();
	iic_write_byte(WFSensorIICDevice); //写设备号
	iic_wait_ack();
	iic_write_byte(0x02); //0x02寄存器
	iic_wait_ack();
	iic_start();
	iic_write_byte(WFSensorIICDevice | 0X01); //读取数据
	iic_wait_ack();
	i = iic_read_byte();
	iic_noAck();
	iic_stop();
	SDA_INPUT();
	SCL_INPUT();
	return i;
}

void WFSensor_indicateGroupConvert(void)
{
	WFSensor_WriteByte(0X30,CMD_GROUP_CONVERT);
	delayMs(5);
}

void WFSensor_getTPData(void)
{
	WFSensor_ReadContiune(0X06,QT,5);
}

void calculatePress(void)
{	
    float fDat;
	float Press_Data;  //unit = kpa
	float Temp_Data;   //unit = c 摄氏度
	s32 dat;//有符32位
	
	dat = QT[0];
	dat <<= 8;
	dat |= QT[1];
	dat <<= 8;
	dat |= QT[2];
	if (dat > 8388608) 
	{
		fDat = (dat - 16777216) / 8388608.0f;
	} else {
		fDat = dat / 8388608.0f;
	}
	
	Press_Data = (500 * fDat + 750) / 6.0;

	
	//温度
	dat = QT[3];
	dat = dat << 8;
	dat |= QT[4];
	if (dat > 32768) {
		Temp_Data = (dat - 65844) / 256.0f;
	} else {
		Temp_Data = (dat - 308) / 256.0f;
	}
}







