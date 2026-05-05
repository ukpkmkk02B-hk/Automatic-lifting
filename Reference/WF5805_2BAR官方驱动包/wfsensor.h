#ifndef	_WFSensor_H_
#define	_WFSensor_H_


void WFSensor_WriteByte(u8 addr, u8 Data);
u8 WFSensor_ReadByte(u8 addr);
u8 WFSensor_WaitFinish();
void WFSensor_indicateGroupConvert(void);
void WFSensor_getTPData(void);
void WFSensor_indicateOneByOneConvert(void);
void calculatePress(void);

void get_decData(void);

#endif //_WFSensor_H_
