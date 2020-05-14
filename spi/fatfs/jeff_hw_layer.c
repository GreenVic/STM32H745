#include <stm32h7xx_hal.h>
//#include "fatfs/diskio.h"
#include "ff.h"
#include "diskio.h"
//#include "jeff_hw_layer.h"




#define SPI_CH	1	/* SPI channel to use = 1: SPI1, 11: SPI1/remap, 2: SPI2 */

#define FCLK_SLOW() { SPI1->CFG1 = (SPI_CFG1_MBR_0 | SPI_CFG1_MBR_1 | SPI_CFG1_MBR_2); }	/* Set SCLK = PCLK / 64 */
#define FCLK_FAST() { SPI1->CFG1 = (SPI_CFG1_MBR_0 | SPI_CFG1_MBR_1); }	/* Set SCLK = PCLK / 2 */


#define CS_HIGH()	GPIOA->BSRR = _BV(4)
#define CS_LOW()	GPIOA->BSRR = _BV(4+16)
#define	MMC_CD		0 //!(GPIOC_IDR & _BV(4))	/* Card detect (yes:true, no:false, default:true) */
#define	MMC_WP		0 /* Write protected (yes:true, no:false, default:false) */
#define SPIx_CR1	SPI1->CR1 //<--control register
#define SPIx_SR		SPI1->SR //<-- status register
#define SPIx_TXDR	SPI1->TXDR //<--data register. the H7 chip has 2, 1 for tx and 1 for rx?
#define SPIx_RXDR	SPI1->RXDR //<--data register. the H7 chip has 2, 1 for tx and 1 for rx?

#define	_BV(bit) (1<<(bit))

#define	SPIxENABLE() {SPI1->CR1 |= _BV(SPI_CR1_SPE);}//\
//	__enable_peripheral(SPI1->SPE);\
//	__enable_peripheral(IOPAEN);\
//	__enable_peripheral(IOPCEN);\
//	__gpio_conf_bit(GPIOA, 4, OUT_PP);						/* PA4: MMC_CS */\
//	__gpio_conf_bit(GPIOA, 5, ALT_PP);						/* PA5: MMC_SCLK */\
//	GPIOA_BSRR = _BV(6); __gpio_conf_bit(GPIOA, 6, IN_PUL); /* PA6: MMC_DO with pull-up */\
//	__gpio_conf_bit(GPIOA, 7, ALT_PP);						/* PA7: MMC_DI */\
//	GPIOC_BSRR = _BV(4); __gpio_conf_bit(GPIOC, 4, IN_PUL);	/* PC4: MMC_CD with pull-up */\
//	SPIx_CR1 = _BV(9)|_BV(8)|_BV(6)|_BV(2);					/* Enable SPI1 */\
//}

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/




/* MMC/SD command */
#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND (MMC) */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT (MMC) */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)		/* ERASE_ER_BLK_START */
#define CMD33	(33)		/* ERASE_ER_BLK_END */
#define CMD38	(38)		/* ERASE */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */


static volatile
DSTATUS Stat = STA_NOINIT;	/* Physical drive status */

static volatile
UINT Timer1, Timer2;	/* 1kHz decrement timer stopped at zero (disk_timerproc()) */

static
BYTE CardType;			/* Card type flags */


//This is where the magice happens.... 
//1 enable spi hardware
//2 provide some calls that fatfs can make in here
//3 provide the spi send calls
//4 provide the spi recieve calls

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/
#include <string.h>
/* Initialize MMC interface */
static
void init_spi(void)
{
	//Becasue STM is a turd, and I am sick of fighting with turds, Im making a fake gpioa
	//so I can watch the values change and confirm settings. Its just not working via st link for me.
	GPIO_TypeDef mygpio;
	memset(&mygpio, 0, sizeof(mygpio));
	GPIO_TypeDef* gpioa = &mygpio;
	uint32_t test = 54;
	//Super bare metal version.
	//Bits are from right to left.. NOT left to right

	/*Need to configure GPIO. Specs are:
	pa4 nss/cs/ss/whatever the hell else you wanna call it
	pa5 sck
	pa6 miso
	pa7 mosi
	*/
	//We are not setting pa4 as the slave for spi auto select. We will control that manually
	/*
	Bits 31:0 MODER[15:0][1:0]: Port x configuration I/O pin y (y = 15 to 0)
	These bits are written by software to configure the I/O mode.
	00: Input mode
 -->01: General purpose output mode<-- we want this for out slave select on pa4
 -->10: Alternate function mode <--- we want this for our mode.. 
	11: Analog mode (reset state)
*/
//pa5 must be the clk output signal. We will set that up first
	GPIOA->MODER = 0; //<--set this to zero to clear it out. I dont know what kind of 
	//mess may have been made before I got here but I have kids and I know what they
	//can do.
	
	//Set alternate function mode by making bit 0=0 and bit 1=1
	GPIOA->MODER &= ~(GPIO_MODER_MODE5_0); //clear bit 0 for pa5s af
	GPIOA->MODER |= (GPIO_MODER_MODE5_1);//set bit 1 for pa5s af
	//Set AFR
	/*Bits 31:0 AFR[7:0][3:0]: Alternate function selection for port x I/O pin y (y = 7 to 0)
	These bits are written by software to configure alternate function I/Os.
	0000: AF0
	0001: AF1
	0010: AF2
	0011: AF3
	0100: AF4
	0101: AF5
	0110: AF6
	0111: AF7
	1000: AF8
	1001: AF9
	1010: AF10
	1011: AF11
	1100: AF12
	1101: AF13
	1110: AF14
	1111: AF15
	*/
	//Set pa5's alternate function now. It needs to be sck for spi1. Which the doc says is AF5
	GPIOA->AFR[0] |= GPIO_AFRL_AFSEL5_0; //set 0 bit for pa5 alt function
	GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL5_1; //clear 1 bit for pa5 alt function
	GPIOA->AFR[0] |= GPIO_AFRL_AFSEL5_2; //set 2 bit for pa5 alt function
	GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL5_3; //clear 3 bit for pa5 alt function
	//            ^--- Used = here, NOT |= so that whatever was in AFR is erased and I start from scratch

	//So hey we did all that and turns out alt function 5 is the number 5.. 
	//decimal 5 is binary 00001010 and it happens to be pin 5....
	//each pin is 4 bits wide.
	//5(our pin number to configure) * 4 (bits per pin) = 20 bits to shift
	//so lets shift our value for alternate function (5) over 20 bits
	//GPIO_AFRL_AFSEL5_Pos is 20, its handy. Just wanted to explain the bit shofting. 
	GPIOA->AFR[0] |= (5 << GPIO_AFRL_AFSEL5_Pos); //cake!

	//Quick note here the AF feature is 4 bits per pin. Thats 4*16 pins = 64 bits... Problem is
	//the hardware only has 32bit registers. So they broke it into 2 parts, the low part, and
	//the high part. So the first 8 pins (0-7) are in the 'lower' register, and the remaining
	//8 (8-15) are in the 'upper' register. Thats why I am using GPIOA->AFR[0] instead of GPIOA->AFR[1]
	//because I am only working with pa5,6,7. If it were a pin above 7 I would also have to use
	//GPIOA->AFR[1] to configure that higher pin as well. 

	//setup pa6 miso. Similar to 5, so less descriptive, you get the idea, agin alternate function 5.
	//Set pa6 to alternate mode miso
	//This is miso so its gotta be an input
	GPIOA->MODER &= ~(GPIO_MODER_MODE6_0); //clear bit 0 for pa6s af
	GPIOA->MODER &= ~(GPIO_MODER_MODE6_1); //clear bit 1 for pa6s af
	//Again lets simplify this. 00000000 in binary is decimal 0. 
	//Lets bit shift a 0 in that position
	GPIOA->MODER |= (0<<GPIO_MODER_MODE6_Pos); //easy peasy
	//And now like pa5 set its lower register for alternate function 5
	GPIOA->AFR[0] |= ( 5 << GPIO_AFRL_AFSEL6_Pos); //I didnt describe it all again.. seems redundant.. hope you got it. 

	//setup pa7 mosi
	GPIOA->MODER |= (1 << GPIO_MODER_MODE7_Pos); //set its mode to output
	GPIOA->AFR[0] |= (5 << GPIO_AFRL_AFSEL7_Pos); //assign it to alt func 5

	//setup pa4 as out chip select pin. 
	//It needs to be general purpose output so looking about thats 0 and 1
	//Same thing for 4 except this time a binary value of 
	GPIOA->MODER |= (GPIO_MODER_MODE4_0); //set bit 0 for pa4s general output
	GPIOA->MODER &= !(GPIO_MODER_MODE4_1);//clear bit 1 for pa4s general output
	//pa4 wont have an alternate function. 
	//Set pa4s output type to push pull
	GPIOA->OTYPER &= ~GPIO_OTYPER_OT4; //Clear the bit to make output push/pull
	//Set the pullup
	GPIOA->PUPDR |= GPIO_PUPDR_PUPD4_0; //Set bit 1
	GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD4_1; //Clear bit 0
	//leaving pa4 at low speed for now.
	GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED4_0; //Set bit 1
	GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED4_1; //Clear bit 0

	RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;   // need a clock for gpio
		

	//SPI needs a clock... Here you go spi, have fun.. 
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;   // enable SPI clock
	SPI1->CR1 = 0;
	SPI1->CR2 = 0;
	SPI1->CFG1 = 0;
	SPI1->CFG2 = 0;
	/*
	Bit 16 IOLOCK: locking the AF configuration of associated IOs
	This bit is set by software and cleared by hardware on next device reset
	0: AF configuration is not locked
	1: AF configuration is locked
	When this bit is set, SPI_CFG2 register content cannot be modified any more.
	This bit must be configured when the SPI is disabled. When SPE=1, it is write protected.
	*/
	//We need to set a few things in the control register to ensure that we CAN configure the spi regs
	//According to the doc the Alternate functions of the GPIO cannot be set, and SPI cannot be
	//configured correctly without unlocking first. 
	SPI1->CR1 &= ~SPI_CR1_IOLOCK;
	//        ^--- Used = here, NOT |= so that whatever was in CR1 is erased and I start from scratch

	//Not changing TCRCINI reg. I dont need it
	//Not changing RCRCINI reg. I dont need it
	//Not changing SSI reg. I dont need it
	//Not changing HDDIR reg. I dont need it
	//Not changing CSUSP reg. I dont need it
	//Not changing CSTART reg yet. I will do that at the bottom after all the config is done
	//Not changing MASRX reg. I dont need it
	//Not changing SPE reg yet. I will do that at the bottom after all the config is done



	/*************CONFIG REGISTER 1**********/
	//Set MBR Master Baud Rate, bits 28-30
	/*000: SPI master clock / 2
	001 : SPI master clock / 4
	010 : SPI master clock / 8
	011 : SPI master clock / 16
	100 : SPI master clock / 32
	101 : SPI master clock / 64
	110 : SPI master clock / 128
	111 : SPI master clock / 256*/
	SPI1->CFG1 = (SPI_CFG1_MBR_0 | SPI_CFG1_MBR_1); //bit0=1 and bit1=1 = 1/16th
	//         ^--- Used = here, NOT |= so that whatever was in CFG1 is erased and I start from scratch

	//Set CRCEN. Turn of CRC enable
	SPI1->CFG1 &= ~SPI_CFG1_CRCEN;
	//         ^^--- Used &= here to preserve what I set before and change other bits

	//Not changing CRCSIZE reg. I dont need it
	//Not changing TXDMAEN reg. I dont need it
	//Not changing RXDMAEN reg. I dont need it
	//Not changing UDRDET reg. I dont need it
	//Not changing UDRCFG reg. I dont need it
	//Not changing FTHLV reg. I dont need it

	//Set DSIZE to 8 bits. This may be wrong, I could probably go 16, but 8 is what I 
	//expect for SD cards so I am leaving it at 8 bits
	/*00000: not used
	00001 : not used
	00010 : not used
	00011 : 4 - bits (looks like 4 bits minumum)
	00100 : 5 - bits
	00101 : 6 - bits
	00110 : 7 - bits
	00111 : 8 - bits
	11101 : 30 - bits
	11110 : 31 - bits
	11111 : 32 - bits*/
	//We can do a bit size calculation easily. Convert a decimal number - 1 into binary...
	//8bits -1 = 7
	//7 in binary = 0111. See, pretty freakin easy. But the define for the chip is already set
	//so Im just going to use the 3rd preset data sizer.
	//or you could also do this SPI1->CFG1 |= data_size you want
	SPI1->CFG1 |= SPI_CFG1_DSIZE_3;
	//         ^^--- Used |= here to preserve what I set before and change other bits
	//Register 1 is done.. Bad ass, let go to register 2.

	/*************CONFIG REGISTER 2**********/
	//NOTE... YOU CANNOT CONFIGURE REGISTER 2 IF THE BIT FOR SPI IS ENABLED!!!
	//Common mistake I see EVERYWHERE is to turn on the spi (set SPI_CR1_SPE = 1)
	//and then try to config it after that.. You cant. Dont try, its a waste of time.. 
	//Turn off spi if you need to change it, then turn it back on.

	//Set AFCNTRthe alternate function GPIO
	//0: the peripheral takes no control of GPIOs while it is disabled
	//1 : the peripheral keeps always control of all associated GPIOs
	//So in other words, when we disable spi for a config change (like I said above) 
	//we want the GPIO controllers in the chip to check this bit to see if its 0 or 1.
	//If its 0, the GPIO controller MIGHT try to control this pin when spi is off.
	//When this bit is 1 the GPIO in the chip MIGHT try to do something with this pin
	//when spi is off, so it s a good idea to set this to 1. I mean I trust my dog not
	//to poop in the house, but sometimes... he does... 
	//BUT HERE IS THE KICKER!!! We are still configuring this spi, which is currently
	//disabled and if we set this bit we cant change anymore stuff in here.. SO we should
	//to set it LAST if we want to use it. I dont, but you might... 
	SPI1->CFG2 &= ~SPI_CFG2_AFCNTR;
	//         ^^--- Used &= here to preserve what I set before and change other bits

	/*Bit 26 SSM: software management of SS signal input
	0 : SS input value is determined by the SS PAD
	1 : SS input value is determined by the SSI bit
	SS signal input has to be managed by software(SSM = 1, SSI = 1) when SS output mode is
	enabled(SSOE = 1) at master mode.*/
	SPI1->CFG2 |= SPI_CFG2_SSM; //Yes I would like to manage my own pin selection... 

	/*Bit 30 SSOM: SS output management in master mode
	This bit is used in master mode when SSOE is enabled. It allows to configure SS output
	between two consecutive data transfers.
	0 : SS is kept at active level till data transfer is completed, it becomes inactive with EOT flag
	1 : SPI data frames are interleaved with SS non active pulses when MIDI[3:0] > 1*/
	//Okay this one has some apparent underlying things to pay attention too. 
	//If SSOM is set to 1, the hardware will control the slave pin toggle. But I dont want that
	//I want to control it myself. But you cant jsut NOT use the SSOE bit either, so It appears
	//you need to turn both bits off, if you want manual control of teh slave select line
	SPI1->CFG2 &= ~SPI_CFG2_SSOM; //clear this bit

	/*Bit 29 SSOE: SS output enable
	This bit is taken into account at master mode only
	0 : SS output is disabled and the SPI can work in multi - master configuration
	1 : SS output is enabled.The SPI cannot work in a multi - master environment.It forces the SS
	pin at inactive level after the transfer in according with SSOM, MIDI, MSSI, SSIOP bits setting*/
	SPI1->CFG2 &= ~SPI_CFG2_SSOE; //clear this bit, because I want to manage the slave select myself.

	/*Bit 28 SSIOP: SS input / output polarity
	0 : low level is active for SS signal
	1 : high level is active for SS signal*/
	//Do you want the auto slave select? and if so do you want it auto high or auto low.. 
	/*****WARNING!! NOTICE!!! ATTENTION!!! CAUTION!!!!!*****
	//I know the reference SAYS that this is the output polarity but....
	//It refers to MODF being set if SSI is set to zero. THERE IS NOT SSI REGISTER!! ITS TALKING
	//ABOUT THIS REGISTER, SSI(op).. I know the description seems pretty innocent, that it only
	//effect the polarity of the slave select, that is not true!! IF THIS IS A ZERO YOU CANNOT
	//SET SPI TO MASTER MODE WHEN YOU CONTROL THE SLAVE SELECT LINE IN YOUR SOFTWARE!!
	Thank you STM for wasting 3 hours of my time.*/
	SPI1->CFG2 |= SPI_CFG2_SSIOP; //SET THIS PIECE OF SHIT!!


	//Set CPOL clock polarity.. Honestly, I dont know what to set here, so I'll leave it at zero
	SPI1->CFG2 &= ~SPI_CFG2_CPOL;
	//Set CPHA clock phase.. leave it at zero
	SPI1->CFG2 &= ~SPI_CFG2_CPHA;
	//Set LSBFRST Least Significant Bit First. 0 = off, 1 = on
	SPI1->CFG2 &= ~SPI_CFG2_LSBFRST;

	//Set MASTER. You guessed it, this sets the spi as master
	SPI1->CFG2 |= SPI_CFG2_MASTER;

	//Set COMM. Full duplex, simplex tx, simple rx, or half duplex.
	/*00: full-duplex
	01: simplex transmitter
	10: simplex receiver
	11: half-duplex*/
	//I need full duplex for my purposes
	//We could jsut set these at simple 0,0 but I wanna go it with the reg.. Makes me feel awesome.
	SPI1->CFG2 &= ~(SPI_CFG2_COMM_0 | SPI_CFG2_COMM_0);

	//Not changing IOSWP reg. I dont need it
	//Not changing MIDI reg. I dont need it
	//Not changing MSSI reg. I dont need it

	/*************ALL CONFIG RESIGTERS SET**********/


	//Now we enable spi... 
	SPI1->CR1 |= SPI_CR1_SPE;// spi enable


	CS_HIGH();			/* Set CS# high */

	for (Timer1 = 10; Timer1; );	/* 10ms */
}


/* Exchange a byte */
static
BYTE xchg_spi(
	BYTE dat	/* Data to send */
)
{
	//And now we can start sending, but this register can only be set when master = 1 and spi enable = 1
	SPI1->CR1 |= SPI_CR1_CSTART;        // transfer start
	SPI1->TXDR = 0x3;
	//SPIx_TXDR = dat;				/* Start an SPI transaction */
	while ((SPI1->SR & SPI_SR_TXC))
	{
		int x = 0;
	}/* Wait for end of the transaction */
	return (BYTE)SPIx_RXDR;		/* Return received byte */
}


/* Receive multiple byte */
static
void rcvr_spi_multi(
	BYTE* buff,		/* Pointer to data buffer */
	UINT btr		/* Number of bytes to receive (even number) */
)
{
	WORD d;


	SPIx_CR1 &= ~_BV(6);
	SPIx_CR1 |= (_BV(6) | _BV(11));	/* Put SPI into 16-bit mode */

	SPIx_TXDR = 0xFFFF;		/* Start the first SPI transaction */
	btr -= 2;
	do {					/* Receive the data block into buffer */
		while ((SPIx_SR & 0x83) != 0x03);	/* Wait for end of the SPI transaction */
		d = SPIx_RXDR;						/* Get received word */
		SPIx_TXDR = 0xFFFF;					/* Start next transaction */
		buff[1] = d; buff[0] = d >> 8; 		/* Store received data */
		buff += 2;
	} while (btr -= 2);
	while ((SPIx_SR & 0x83) != 0x03);		/* Wait for end of the SPI transaction */
	d = SPIx_RXDR;							/* Get last word received */
	buff[1] = d; buff[0] = d >> 8;			/* Store it */

	SPIx_CR1 &= ~(_BV(6) | _BV(11));	/* Put SPI into 8-bit mode */
	SPIx_CR1 |= _BV(6);
}


#if FF_FS_READONLY == 0
/* Send multiple byte */
static
void xmit_spi_multi(
	const BYTE * buff,	/* Pointer to the data */
	UINT btx			/* Number of bytes to send (even number) */
)
{
	WORD d;


	SPIx_CR1 &= ~_BV(6);
	SPIx_CR1 |= (_BV(6) | _BV(11));		/* Put SPI into 16-bit mode */

	d = buff[0] << 8 | buff[1]; buff += 2;
	SPIx_TXDR = d;	/* Send the first word */
	btx -= 2;
	do {
		d = buff[0] << 8 | buff[1]; buff += 2;	/* Word to send next */
		while ((SPIx_SR & 0x83) != 0x03);	/* Wait for end of the SPI transaction */
		SPIx_RXDR;							/* Discard received word */
		SPIx_TXDR = d;						/* Start next transaction */
	} while (btx -= 2);
	while ((SPIx_SR & 0x83) != 0x03);	/* Wait for end of the SPI transaction */
	SPIx_RXDR;							/* Discard received word */

	SPIx_CR1 &= ~(_BV(6) | _BV(11));	/* Put SPI into 8-bit mode */
	SPIx_CR1 |= _BV(6);
}
#endif


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready(	/* 1:Ready, 0:Timeout */
	UINT wt			/* Timeout [ms] */
)
{
	BYTE d;


	Timer2 = wt;
	do {
		d = xchg_spi(0xFF);
		/* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
	} while (d != 0xFF && Timer2);	/* Wait for card goes ready or timeout */

	return (d == 0xFF) ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/

static
void deselect(void)
{
	CS_HIGH();		/* Set CS# high */
	xchg_spi(0xFF);	/* Dummy clock (force DO hi-z for multiple slave SPI) */

}



/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/

static
int select(void)	/* 1:OK, 0:Timeout */
{
	CS_LOW();		/* Set CS# low */
	xchg_spi(0xFF);	/* Dummy clock (force DO enabled) */
	if (wait_ready(500)) return 1;	/* Wait for card ready */

	deselect();
	return 0;	/* Timeout */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/

static
int rcvr_datablock(	/* 1:OK, 0:Error */
	BYTE* buff,			/* Data buffer */
	UINT btr			/* Data block length (byte) */
)
{
	BYTE token;


	Timer1 = 200;
	do {							/* Wait for DataStart token in timeout of 200ms */
		token = xchg_spi(0xFF);
		/* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
	} while ((token == 0xFF) && Timer1);
	if (token != 0xFE) return 0;		/* Function fails if invalid DataStart token or timeout */

	rcvr_spi_multi(buff, btr);		/* Store trailing data to the buffer */
	xchg_spi(0xFF); xchg_spi(0xFF);			/* Discard CRC */

	return 1;						/* Function succeeded */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to the MMC                                         */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0
static
int xmit_datablock(	/* 1:OK, 0:Failed */
	const BYTE * buff,	/* Ponter to 512 byte data to be sent */
	BYTE token			/* Token */
)
{
	BYTE resp;


	if (!wait_ready(500)) return 0;		/* Wait for card ready */

	xchg_spi(token);					/* Send token */
	if (token != 0xFD) {				/* Send data if token is other than StopTran */
		xmit_spi_multi(buff, 512);		/* Data */
		xchg_spi(0xFF); xchg_spi(0xFF);	/* Dummy CRC */

		resp = xchg_spi(0xFF);				/* Receive data resp */
		if ((resp & 0x1F) != 0x05) return 0;	/* Function fails if the data packet was not accepted */
	}
	return 1;
}
#endif


/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/

static
BYTE send_cmd(		/* Return value: R1 resp (bit7==1:Failed to send) */
	BYTE cmd,		/* Command index */
	DWORD arg		/* Argument */
)
{
	BYTE n, res;


	if (cmd & 0x80) {	/* Send a CMD55 prior to ACMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card and wait for ready except to stop multiple block read */
	if (cmd != CMD12) {
		deselect();
		if (!select()) return 0xFF;
	}

	/* Send command packet */
	xchg_spi(0x40 | cmd);				/* Start + command index */
	xchg_spi((BYTE)(arg >> 24));		/* Argument[31..24] */
	xchg_spi((BYTE)(arg >> 16));		/* Argument[23..16] */
	xchg_spi((BYTE)(arg >> 8));			/* Argument[15..8] */
	xchg_spi((BYTE)arg);				/* Argument[7..0] */
	n = 0x01;							/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;			/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;			/* Valid CRC for CMD8(0x1AA) */
	xchg_spi(n);

	/* Receive command resp */
	if (cmd == CMD12) xchg_spi(0xFF);	/* Diacard following one byte when CMD12 */
	n = 10;								/* Wait for response (10 bytes max) */
	do {
		res = xchg_spi(0xFF);
	} while ((res & 0x80) && --n);

	return res;							/* Return received response */
}



/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS MMC_disk_initialize(
	BYTE drv		/* Physical drive number (0) */
)
{
	BYTE n, cmd, ty, ocr[4];


	if (drv) return STA_NOINIT;			/* Supports only drive 0 */
	init_spi();							/* Initialize SPI */

	//if (Stat & STA_NODISK) return Stat;	/* Is card existing in the soket? */

	FCLK_SLOW();
	for (n = 10; n; n--) xchg_spi(0xFF);	/* Send 80 dummy clocks */

	ty = 0;
	if (send_cmd(CMD0, 0) == 1) {			/* Put the card SPI/Idle state */
		Timer1 = 1000;						/* Initialization timeout = 1 sec */
		if (send_cmd(CMD8, 0x1AA) == 1) {	/* SDv2? */
			for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);	/* Get 32 bit return value of R7 resp */
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {				/* Is the card supports vcc of 2.7-3.6V? */
				while (Timer1 && send_cmd(ACMD41, 1UL << 30));	/* Wait for end of initialization with ACMD41(HCS) */
				if (Timer1 && send_cmd(CMD58, 0) == 0) {		/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* Card id SDv2 */
				}
			}
		}
		else {	/* Not SDv2 card */
			if (send_cmd(ACMD41, 0) <= 1) {	/* SDv1 or MMC? */
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 (ACMD41(0)) */
			}
			else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 (CMD1(0)) */
			}
			while (Timer1 && send_cmd(cmd, 0));		/* Wait for end of initialization */
			if (!Timer1 || send_cmd(CMD16, 512) != 0)	/* Set block length: 512 */
				ty = 0;
		}
	}
	CardType = ty;	/* Card type */
	deselect();

	if (ty) {			/* OK */
		FCLK_FAST();			/* Set fast clock */
		Stat &= ~STA_NOINIT;	/* Clear STA_NOINIT flag */
	}
	else {			/* Failed */
		Stat = STA_NOINIT;
	}

	return Stat;
}



/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS MMC_disk_status(
	BYTE drv		/* Physical drive number (0) */
)
{
	if (drv) return STA_NOINIT;		/* Supports only drive 0 */

	return Stat;	/* Return disk status */
}



/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT MMC_disk_read(
	BYTE drv,		/* Physical drive number (0) */
	BYTE* buff,		/* Pointer to the data buffer to store read data */
	LBA_t sector,	/* Start sector number (LBA) */
	UINT count		/* Number of sectors to read (1..128) */
)
{
	DWORD sect = (DWORD)sector;


	if (drv || !count) return RES_PARERR;		/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	if (!(CardType & CT_BLOCK)) sect *= 512;	/* LBA ot BA conversion (byte addressing cards) */

	if (count == 1) {	/* Single sector read */
		if ((send_cmd(CMD17, sect) == 0)	/* READ_SINGLE_BLOCK */
			&& rcvr_datablock(buff, 512)) {
			count = 0;
		}
	}
	else {				/* Multiple sector read */
		if (send_cmd(CMD18, sect) == 0) {	/* READ_MULTIPLE_BLOCK */
			do {
				if (!rcvr_datablock(buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}
	deselect();

	return count ? RES_ERROR : RES_OK;	/* Return result */
}



/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0
DRESULT MMC_disk_write(
	BYTE drv,			/* Physical drive number (0) */
	const BYTE * buff,	/* Ponter to the data to write */
	LBA_t sector,		/* Start sector number (LBA) */
	UINT count			/* Number of sectors to write (1..128) */
)
{
	DWORD sect = (DWORD)sector;


	if (drv || !count) return RES_PARERR;		/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check drive status */
	if (Stat & STA_PROTECT) return RES_WRPRT;	/* Check write protect */

	if (!(CardType & CT_BLOCK)) sect *= 512;	/* LBA ==> BA conversion (byte addressing cards) */

	if (count == 1) {	/* Single sector write */
		if ((send_cmd(CMD24, sect) == 0)	/* WRITE_BLOCK */
			&& xmit_datablock(buff, 0xFE)) {
			count = 0;
		}
	}
	else {				/* Multiple sector write */
		if (CardType & CT_SDC) send_cmd(ACMD23, count);	/* Predefine number of sectors */
		if (send_cmd(CMD25, sect) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!xmit_datablock(buff, 0xFC)) break;
				buff += 512;
			} while (--count);
			if (!xmit_datablock(0, 0xFD)) count = 1;	/* STOP_TRAN token */
		}
	}
	deselect();

	return count ? RES_ERROR : RES_OK;	/* Return result */
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

DRESULT MMC_disk_ioctl(
	BYTE drv,		/* Physical drive number (0) */
	BYTE cmd,		/* Control command code */
	void* buff		/* Pointer to the conrtol data */
)
{
	DRESULT res;
	BYTE n, csd[16];
	DWORD st, ed, csize;
	LBA_t* dp;


	if (drv) return RES_PARERR;					/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	res = RES_ERROR;

	switch (cmd) {
	case CTRL_SYNC:		/* Wait for end of internal write process of the drive */
		if (select()) res = RES_OK;
		break;

	case GET_SECTOR_COUNT:	/* Get drive capacity in unit of sector (DWORD) */
		if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
			if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
				csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
				*(LBA_t*)buff = csize << 10;
			}
			else {					/* SDC ver 1.XX or MMC ver 3 */
				n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
				csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
				*(LBA_t*)buff = csize << (n - 9);
			}
			res = RES_OK;
		}
		break;

	case GET_BLOCK_SIZE:	/* Get erase block size in unit of sector (DWORD) */
		if (CardType & CT_SD2) {	/* SDC ver 2.00 */
			if (send_cmd(ACMD13, 0) == 0) {	/* Read SD status */
				xchg_spi(0xFF);
				if (rcvr_datablock(csd, 16)) {				/* Read partial block */
					for (n = 64 - 16; n; n--) xchg_spi(0xFF);	/* Purge trailing data */
					*(DWORD*)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		}
		else {					/* SDC ver 1.XX or MMC */
			if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {	/* Read CSD */
				if (CardType & CT_SD1) {	/* SDC ver 1.XX */
					*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				}
				else {					/* MMC */
					*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;

	case CTRL_TRIM:	/* Erase a block of sectors (used when _USE_ERASE == 1) */
		if (!(CardType & CT_SDC)) break;				/* Check if the card is SDC */
		if (disk_ioctl(drv, MMC_GET_CSD, csd)) break;	/* Get CSD */
		if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break;	/* Check if sector erase can be applied to the card */
		dp = buff; st = (DWORD)dp[0]; ed = (DWORD)dp[1];	/* Load sector block */
		if (!(CardType & CT_BLOCK)) {
			st *= 512; ed *= 512;
		}
		if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) {	/* Erase sector block */
			res = RES_OK;	/* FatFs does not check result of this command */
		}
		break;

	default:
		res = RES_PARERR;
	}

	deselect();

	return res;
}



/*-----------------------------------------------------------------------*/
/* Device timer function                                                 */
/*-----------------------------------------------------------------------*/
/* This function must be called from timer interrupt routine in period
/  of 1 ms to generate card control timing.
*/

void disk_timerproc(void)
{
	WORD n;
	BYTE s;


	n = Timer1;						/* 1kHz decrement timer stopped at 0 */
	if (n) Timer1 = --n;
	n = Timer2;
	if (n) Timer2 = --n;

	s = Stat;
	if (MMC_WP) {	/* Write protected */
		s |= STA_PROTECT;
	}
	else {		/* Write enabled */
		s &= ~STA_PROTECT;
	}
	if (MMC_CD) {	/* Card is in socket */
		s &= ~STA_NODISK;
	}
	else {		/* Socket empty */
		s |= (STA_NODISK | STA_NOINIT);
	}
	Stat = s;
}

