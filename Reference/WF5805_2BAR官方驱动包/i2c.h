#ifndef	_I2C_H_
#define	_I2C_H_

 
void SDA_OUTPUT(void);
void SDA_INPUT(void);
void SCL_OUTPUT(void);
void SCL_INPUT(void);

void iic_start(void);
void iic_stop(void);
void iic_wait_ack(void);
void iic_ack(void);
void iic_noAck(void);

void iic_write_byte(u8 byte);
u8 iic_read_byte(void);

void IIC_ReadContiune(u8 Device,u8 addr,u8 *p,u8 Length);

void IIC_WriteByte(u8 Device,u8 addr,u8 Data);

#endif //_I2C_H_
