/*
Main includes sd_card.h

Order of calls:
main.c/cpp -->SDCard_Config-->SD_IO_Init-->HW_config--|-->HW_config_gpio (first)
                    |             |                   |-->HW_config_spi_handler (second)
					|			  |---------------------->HW_IO_SPI_send (third)
					|			  |---------------------->SD_GoIdleState (fourth)
					|------------------------------------>f_mount-->disk_initialize-->pntr_from_link_drvr_SD_initialize (fifth)
					|------------------------------------>f_open--->disk_read-------->pntr_from_link_drvr_SD_read (sixth)
					|------------------------------------>f_read--->disk_read-------->pntr_from_link_drvr_SD_read (seventh)
					|------------------------------------>f_write-->disk_write------->pntr_from_link_drvr_SD_write (eighth)

hardware_config configures the actual hardware such as GPIO and SPI
hardware_io performs the low level send and receive functions
sd_card contain high level send/receive functions specific to sd card. It calls into the low level hardware_io functions

This example uses SPI1 on port GPIOA pins PA4,PA5,PA6,PA7. PA4 is slave.chip select and is SOFTWARE controlled, not hardware
*/

#include <stm32h7xx_hal.h>
#include <stm32_hal_legacy.h>
#include "../Code/Hardware/sd_card.h"
#include <string.h>

#ifdef __cplusplus
extern "C"
#endif
void SysTick_Handler(void)
{
	HAL_IncTick();
}


static void SystemClock_Config(void);
static void SDCard_Config(void);

volatile uint8_t safe1 = 1;
volatile uint8_t safe2 = 1;
int main(void)
{
	HAL_Init();
	SystemClock_Config();
	SDCard_Config();
}

static void SystemClock_Config(void)
{
	RCC_ClkInitTypeDef RCC_ClkInitStruct;
	RCC_OscInitTypeDef RCC_OscInitStruct;
	HAL_StatusTypeDef ret = HAL_OK;

	/*!< Supply configuration update enable */
	HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

	/* The voltage scaling allows optimizing the power consumption when the device is
	   clocked below the maximum system frequency, to update the voltage scaling value
	   regarding system frequency refer to product datasheet.  */
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

	/* Enable HSE Oscillator and activate PLL with HSE as source */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
	RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
	RCC_OscInitStruct.CSIState = RCC_CSI_OFF;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;

	RCC_OscInitStruct.PLL.PLLM = 4;
	RCC_OscInitStruct.PLL.PLLN = 400;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLQ = 4;

	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
	ret = HAL_RCC_OscConfig(&RCC_OscInitStruct);
	if (ret != HAL_OK)
	{
		//Error_Handler();
	}

	/* Select PLL as system clock source and configure  bus clocks dividers */
	RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 | \
		RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1);

	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
	ret = HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
	if (ret != HAL_OK)
	{
		//Error_Handler();
	}

	/*activate CSI clock mondatory for I/O Compensation Cell*/
	__HAL_RCC_CSI_ENABLE();

	/* Enable SYSCFG clock mondatory for I/O Compensation Cell */
	__HAL_RCC_SYSCFG_CLK_ENABLE();

	/* Enables the I/O Compensation Cell */
	HAL_EnableCompensationCell();

}

static void SDCard_Config(void)
{
	FATFS SD_FatFs;
	FRESULT fr;
	
	//Call sd_card.c->SD_IO_Init(), passing a SPI object
	fr = (FRESULT)SD_IO_Init(SPI1);

	//Mount the drive. 
	//This will call ff.c->f_mount(), diskio.c->disk_initialize, sd_card.c->pntr_from_link_drvr_SD_initialize
	fr = f_mount(&SD_FatFs, (TCHAR const*)"/", 1);

	FIL fil;
	//Open a file
	//This will call ff.c->f_open(), diskio.c->disk_read, sd_card.c->pntr_from_link_drvr_SD_read
	fr = f_open(&fil, "hello.txt\0", FA_OPEN_ALWAYS | FA_WRITE);
	char buff[256] = {"Yet another test!\0" };
	UINT bytes_done = 0;
	UINT bytes_to_go = strlen(buff);
	//Read a file
	//This will call ff.c->f_read(), diskio.c->disk_read, sd_card.c->pntr_from_link_drvr_SD_read
	//fr = f_read(&fil, buff, bytes_to_go, &bytes_done);
	
	//Write a file
	//This will call ff.c->f_write(), diskio.c->disk_write, sd_card.c->pntr_from_link_drvr_SD_write
	//fr = f_read(&fil, buff, bytes_to_go, &bytes_done);
	fr = f_write(&fil, buff, bytes_to_go, &bytes_done);
	f_close(&fil);
}