#include <stm32h7xx_hal.h>
#include <stm32_hal_legacy.h>

#include "fatfs\ff.h"
#include "fatfs\ffconf.h"
#include "fatfs\diskio.h"

static SPI_HandleTypeDef hspi1;
static RTC_HandleTypeDef hrtc;
static GPIO_InitTypeDef  GPIO_InitStruct;


#ifdef __cplusplus
extern "C"
#endif
void SysTick_Handler(void)
{
	HAL_IncTick();
	HAL_SYSTICK_IRQHandler();
	disk_timerproc();
}

int main(void)
{
	HAL_Init();

	/*__GPIOC_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;

	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);*/

	////configure PA4 as manual slave (regular ole GPIO)
	//GPIO_InitStruct.Pin = GPIO_PIN_4;
	//GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	//HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	//__SPI1_CLK_ENABLE();
	//hspi1.Instance = SPI1;
	//hspi1.Init.Mode = SPI_MODE_MASTER;
	//hspi1.Init.Direction = SPI_DIRECTION_2LINES;
	//hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	//hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
	//hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
	//hspi1.Init.NSS = SPI_NSS_SOFT;
	//hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
	//hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
	//hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
	//hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	//hspi1.Init.CRCPolynomial = 7;
	//hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
	//hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;

	FATFS FatFs;
	FIL fil;
	FRESULT fres = f_mount(&FatFs, "1", 1); //1=mount now
	if (fres != FR_OK) {

		while (1);
	}

	TCHAR tchar;
	f_open(&fil, &tchar, 1);

	

	/*GPIO_InitTypeDef GPIO_InitStructure;

	GPIO_InitStructure.Pin = GPIO_PIN_12;

	GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStructure.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

	for (;;)
	{
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);
		HAL_Delay(500);
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);
		HAL_Delay(500);
	}*/
	while (1)
	{

	}
}

#if !FF_FS_NORTC && !FF_FS_READONLY
DWORD get_fattime(void)
{
	//RTCTIME rtc;

	///* Get local time */
	//if (!rtc_gettime(&rtc)) return 0;

	///* Pack date and time into a DWORD variable */
	//return	  ((DWORD)(rtc.year - 1980) << 25)
	//	| ((DWORD)rtc.month << 21)
	//	| ((DWORD)rtc.mday << 16)
	//	| ((DWORD)rtc.hour << 11)
	//	| ((DWORD)rtc.min << 5)
	//	| ((DWORD)rtc.sec >> 1);
	return 0;
}
#endif