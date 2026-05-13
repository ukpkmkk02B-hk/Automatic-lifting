#include "crc16.h"

#define CRC16_CCITT_FALSE_INIT 0xFFFFU
#define CRC16_CCITT_FALSE_POLY 0x1021U

// 函    数：CRC16_CcittFalse
// 参    数：data 待校验数据首地址；length 数据长度，单位 byte。
// 返 回 值：CRC16 校验值。
// 注意事项：用于 Flash 参数记录完整性校验，算法固定为 CRC-16/CCITT-FALSE。
uint16_t CRC16_CcittFalse(const uint8_t *data, uint16_t length)
{
	uint16_t crc;
	uint16_t i;
	uint8_t bit;

	crc = CRC16_CCITT_FALSE_INIT;
	if ((data == 0) && (length != 0U))
	{
		return crc;
	}

	for (i = 0U; i < length; i++)
	{
		crc ^= (uint16_t)((uint16_t)data[i] << 8);
		for (bit = 0U; bit < 8U; bit++)
		{
			if ((crc & 0x8000U) != 0U)
			{
				crc = (uint16_t)((crc << 1) ^ CRC16_CCITT_FALSE_POLY);
			}
			else
			{
				crc = (uint16_t)(crc << 1);
			}
		}
	}

	return crc;
}
