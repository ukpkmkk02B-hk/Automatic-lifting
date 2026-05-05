/*
 * SoftIIC.c
 *
 *  Created on: Aug 3, 2020
 *      Author: ITry
 */

#include "i2c.h"


void SDA_OUTPUT(void)
{
}

void SDA_INPUT(void)
{
}

void SCL_OUTPUT(void)
{
}

void SCL_INPUT(void)
{
}

/********************************************************************
* Function Name  : iic_start.
* Description    : iic启动:当SCL处于高电平状态时，SDA出现一个下降沿,即产生IIC启动信号
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void iic_start(void)
{
	SDA_OUTPUT();
	io_SDA = 1;
	io_SCL = 1;
	delay_us(1);
	io_SDA = 0;
	delay_us(1);
	io_SCL = 0;
	delay_us(2);
}

/********************************************************************
* Function Name  : iic_stop.
* Description    : iic停止:当SCL处于高电平状态时，SDA出现一个上升沿,即产生IIC停止信号
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void iic_stop(void)
{
	SDA_OUTPUT();
	io_SCL = 0;
	io_SDA = 0;
	delay_us(1);
	io_SCL = 1;
	delay_us(1);
	io_SDA = 1;
	delay_us(2);
}

/********************************************************************
* Function Name  : iic_wait_ack.
* Description    : 应答状态，0表示应答，1表示设备无响应
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void iic_wait_ack(void)
{
	u8 err_time = 0;
	SDA_INPUT(); /** 在等待应答信号之前，要释放SDA */
	io_SDA = 1;
	delay_us(2);
	io_SCL = 1;
	delay_us(1);
	//	GIE = 0;
	while (io_SDA)
	{
		err_time++;
		if (err_time > 250)
		{
			//	iic_stop();
			break;
		}
	}
	//	GIE = 1;
	io_SCL = 0;

	delay_us(3);
}

/********************************************************************
* Function Name  : iic_ack.
* Description    : 主机（主控制器）产生应答信号
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void iic_ack(void)
{
	SDA_OUTPUT();
	io_SDA = 0;
	delay_us(1);
	io_SCL = 1;
	delay_us(1);
	io_SCL = 0;
	delay_us(2);
	io_SDA = 1;
}

/********************************************************************
* Function Name  : iic_read_byte.
* Description    : 主机（主控制器）产生不应答信号
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void iic_noAck(void)
{
	SDA_OUTPUT();
	io_SDA = 1;
	delay_us(1);
	io_SCL = 1;
	delay_us(1);
	io_SCL = 0;
	delay_us(2);
	io_SDA = 0;
}

/********************************************************************
* Function Name  : iic_write_byte.
* Description    : 获取一次数据
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void iic_write_byte(u8 byte)
{
	u8 i;
	SDA_OUTPUT();
	/** 发送一个字节的高7位 */
	for (i = 0; i < 8; i++)
	{
		if (byte & 0x80)
		{
			io_SDA = 1;
		}
		else
		{
			io_SDA = 0;
		}

		delay_us(2);
		io_SCL = 1;
		delay_us(2);
		io_SCL = 0;
		byte <<= 1;
		//	delay_us();
	}
}

/********************************************************************
* Function Name  : iic_read_byte.
* Description    : 获取一次数据
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
u8 iic_read_byte(void)
{
	u8 i;
	u8 recv_value = 0;
	delay_us(2);
	SDA_INPUT();
	for (i = 0; i < 8; i++)
	{
		io_SCL = 1;
		delay_us(2);
		recv_value <<= 1;
		if (io_SDA)
		{
			recv_value |= 0x01;
		}
		else
		{
			recv_value &= ~0x01;
		}
		io_SCL = 0;
		delay_us(2);
	}
	return recv_value;
}

/********************************************************************
* Function Name  : IIC_WriteByte.
* Description    : 
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void IIC_WriteByte(u8 Device, u8 addr, u8 Data)
{
	SCL_OUTPUT();
	iic_start();
	iic_write_byte(Device); //写设备号
	iic_wait_ack();
	iic_write_byte(addr); //写要写入的寄存器地址
	iic_wait_ack();
	iic_write_byte(Data); //读取数据
	iic_wait_ack();
	iic_stop();
	SDA_INPUT();
	//	SCL_INPUT();
}

/********************************************************************
* Function Name  : IIC_ReadContiune.
* Description    : 
* Input          : None.
* Output         : None.
* Return         : None.
********************************************************************/
void IIC_ReadContiune(u8 Device, u8 addr, u8* p, u8 Length)
{
	u8 i = 0;
	//	SDA_OUTPUT();
	SCL_OUTPUT();
	iic_start();
	iic_write_byte(Device); //写设备号
	iic_wait_ack();
	iic_write_byte(addr); //写要写入的寄存器地址
	iic_wait_ack();
	iic_start();
	iic_write_byte(Device | 0X01); //读取数据
	iic_wait_ack();
	for (i = 0; i < Length; i++)
	{
		p[i] = iic_read_byte();
		if (i < Length - 1)
		{
			iic_ack();
		}
	}

	iic_noAck();
	iic_stop();
	SDA_INPUT();
	SCL_INPUT();
}
