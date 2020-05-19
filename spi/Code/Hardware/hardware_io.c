#include "hardware_io.h"
#include "hardware_config.h"



#define SPI_TIMEOUT 1000

int32_t HW_IO_SPI_send(uint8_t * pTxData, uint32_t Length)
{
	int32_t ret = HARDWARE_IO_OK;

	if (HAL_SPI_Transmit(&cfg_spi_handler, pTxData, (uint16_t)Length, SPI_TIMEOUT) != HAL_OK)
	{
		ret = HARDWARE_IO_ERROR;
	}

	return ret;
}

int32_t HW_IO_SPI_recv(uint8_t* pRxData, uint32_t Length)
{
	int32_t ret = HARDWARE_IO_OK;

	if (HAL_SPI_Receive(&cfg_spi_handler, pRxData, (uint16_t)Length, SPI_TIMEOUT) != HAL_OK)
	{
		ret = HARDWARE_IO_ERROR;
	}

	return ret;
}

int32_t HW_IO_SPI_sendrecv(uint8_t* pTxData, uint8_t* pRxData, uint32_t Length)
{
	int32_t ret = HARDWARE_IO_OK;
	/*uint8_t* single_byte_send = NULL;
	uint8_t* single_byte_recieve = NULL;*/

	/*for (int i = 0; i < Length; i++)
	{
		single_byte_send = pTxData + i;
		single_byte_recieve = pRxData + i;*/
		if (HAL_SPI_TransmitReceive(&cfg_spi_handler, pTxData, pRxData, (uint16_t)Length, SPI_TIMEOUT) != HAL_OK)
		{
			ret = HARDWARE_IO_ERROR;
		}
	//}

	return ret;
}
int32_t HW_IO_SPI_gettick(void)
{
	return (int32_t)HAL_GetTick();
}
