#include "system.h"
#include "altera_avalon_spi.h"
#include "altera_avalon_spi_regs.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"
#include <stdio.h>
#include <unistd.h>
#include "gc.h"
#define RES_PIN 1
#define DC_PIN 2
unsigned long matrix [128][128];
void OLED_Command_128128RGB(unsigned char c)        // send command to OLED
{
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, DC_PIN);
	unsigned long ret = alt_avalon_spi_command(SPI_0_BASE,0 ,
	                              1, &c,
	                              0, NULL,
	                              0);
	if(ret < 0)
		 alt_printf("ERROR SPI TX RET = %x \n" , ret);
}

void OLED_Data_128128RGB(unsigned char d)        // send data to OLED
{
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, DC_PIN);
	unsigned long ret = alt_avalon_spi_command(SPI_0_BASE,0 ,
	                              1, &d,
	                              0, NULL,
	                              0);

	if(ret < 0)
		 alt_printf("ERROR SPI TX RET = %x \n" , ret);
}

void OLED_SetColumnAddress_128128RGB(unsigned char x_start, unsigned char x_end)    // set column address start + end
{
   OLED_Command_128128RGB(0x15);
   OLED_Data_128128RGB(x_start);
   OLED_Data_128128RGB(x_end);
}

void OLED_SetRowAddress_128128RGB(unsigned char y_start, unsigned char y_end)    // set row address start + end
{
   OLED_Command_128128RGB(0x75);
   OLED_Data_128128RGB(y_start);
   OLED_Data_128128RGB(y_end);
}

void OLED_WriteMemoryStart_128128RGB(void)    // write to RAM command
{
    OLED_Command_128128RGB(0x5C);
}

void OLED_Pixel_128128RGB(unsigned long color)    // write one pixel of a given color
{
	OLED_Data_128128RGB((color>>16));
	OLED_Data_128128RGB((color>>8));
	OLED_Data_128128RGB(color);
}

/*===============================*/
/*===== LOW LEVEL FUNCTIONS =====*/
/*============= END =============*/
/*===============================*/




// SSD1351 Commands
#define SSD1351_CMD_SETCOLUMN 		0x15
#define SSD1351_CMD_SETROW    		0x75
#define SSD1351_CMD_WRITERAM   		0x5C
#define SSD1351_CMD_READRAM   		0x5D
#define SSD1351_CMD_SETREMAP 		0xA0
#define SSD1351_CMD_STARTLINE 		0xA1
#define SSD1351_CMD_DISPLAYOFFSET 	0xA2
#define SSD1351_CMD_DISPLAYALLOFF 	0xA4
#define SSD1351_CMD_DISPLAYALLON  	0xA5
#define SSD1351_CMD_NORMALDISPLAY 	0xA6
#define SSD1351_CMD_INVERTDISPLAY 	0xA7
#define SSD1351_CMD_FUNCTIONSELECT 	0xAB
#define SSD1351_CMD_DISPLAYOFF 		0xAE
#define SSD1351_CMD_DISPLAYON     	0xAF
#define SSD1351_CMD_PRECHARGE 		0xB1
#define SSD1351_CMD_DISPLAYENHANCE	0xB2
#define SSD1351_CMD_CLOCKDIV 		0xB3
#define SSD1351_CMD_SETVSL 		0xB4
#define SSD1351_CMD_SETGPIO 		0xB5
#define SSD1351_CMD_PRECHARGE2 		0xB6
#define SSD1351_CMD_SETGRAY 		0xB8
#define SSD1351_CMD_USELUT 		0xB9
#define SSD1351_CMD_PRECHARGELEVEL 	0xBB
#define SSD1351_CMD_VCOMH 		0xBE
#define SSD1351_CMD_CONTRASTABC		0xC1
#define SSD1351_CMD_CONTRASTMASTER	0xC7
#define SSD1351_CMD_MUXRATIO            0xCA
#define SSD1351_CMD_COMMANDLOCK         0xFD
#define SSD1351_CMD_HORIZSCROLL         0x96
#define SSD1351_CMD_STOPSCROLL          0x9E
#define SSD1351_CMD_STARTSCROLL         0x9F
#define SSD1351_COLORORDER_RGB

#define SSD1351WIDTH 128
#define SSD1351HEIGHT 128  // SET THIS TO 96 FOR 1.27"!
/*********************************/
/******** INITIALIZATION *********/
/************ START **************/
/*********************************/
void OLED_Init_128128RGB(void)      //OLED initialization
{
    IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, RES_PIN);
	alt_busy_sleep(500*1000);
    IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, RES_PIN);
	alt_busy_sleep(500*1000);
    IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, RES_PIN);
	alt_busy_sleep(500*1000);

    OLED_Command_128128RGB(0xFD);	// Command lock setting
    OLED_Data_128128RGB(0x12);		// unlock
    OLED_Command_128128RGB(0xFD);	// Command lock setting
    OLED_Data_128128RGB(0xB1);		// unlock
    OLED_Command_128128RGB(0xAE);
    OLED_Command_128128RGB(0xB3);	// clock & frequency
    OLED_Data_128128RGB(0xF1);		// clock=Diviser+1 frequency=fh
    OLED_Command_128128RGB(216);	// Duty
    OLED_Data_128128RGB(0x7F);		// OLED _END+1
    OLED_Command_128128RGB(0xA2);  	// Display offset
    OLED_Data_128128RGB(0x00);
    OLED_Command_128128RGB(0xA1);	// Set display start line
    OLED_Data_128128RGB(0x00);		// 0x00 start line
    OLED_Command_128128RGB(0xA0);	// Set Re-map, color depth
    OLED_Data_128128RGB(0xB4);		// 8-bit 262K
    OLED_Command_128128RGB(0xB5);	// set GPIO
    OLED_Data_128128RGB(0x00);		// disabled
    OLED_Command_128128RGB(0xAB);	// Function Set
    OLED_Data_128128RGB(0x01);		// 8-bit interface, internal VDD regulator
    OLED_Command_128128RGB(0xB4);	// set VSL
    OLED_Data_128128RGB(0xA0);		// external VSL
    OLED_Data_128128RGB(0xB5);
    OLED_Data_128128RGB(0x55);
    OLED_Command_128128RGB(0xC1);	// Set contrast current for A,B,C
    OLED_Data_128128RGB(0xff);		// Color A
    OLED_Data_128128RGB(0xff);		// Color B
    OLED_Data_128128RGB(0xff);		// Color C
    OLED_Command_128128RGB(0xff);	// Set master contrast
    OLED_Data_128128RGB(0x0F);		//
    OLED_Command_128128RGB(0xB9);	// use linear grayscale LUT
    OLED_Command_128128RGB(0xB1);	// Set pre & dis-charge
    OLED_Data_128128RGB(0x32);		// pre=1h, dis=1h
    OLED_Command_128128RGB(0xBB);	// Set precharge voltage of color A,B,C
    OLED_Data_128128RGB(0x07);		//
    OLED_Command_128128RGB(0xB2);       // display enhancement
    OLED_Data_128128RGB(0xa4);
    OLED_Data_128128RGB(0x00);
    OLED_Data_128128RGB(0x00);
    OLED_Command_128128RGB(0xB6);	// precharge period
    OLED_Data_128128RGB(0x01);
    OLED_Command_128128RGB(0xBE);	// Set VcomH
    OLED_Data_128128RGB(0x07);
    OLED_Command_128128RGB(0xA6);	// Normal display
    OLED_Command_128128RGB(0xAF);	// Display on
}

void OLED_HWBeginAccess(void *hDevice)
{
}

void OLED_HWEndAccess(void *hDevice)
{
}
char gfInitialized;
void OLED_HWSetPixel(GCBITMAP *pBitmap, long x, long y, GCCOLOR color)
{
	*((unsigned long*)(pBitmap->handle + (x + (y * pBitmap->width)) * 4)) = color;
}

GCCOLOR OLED_HWGetPixel(GCBITMAP *pBitmap, long x, long y)
{
    return *((unsigned long*)(pBitmap->handle + (x * 4 + (y * pBitmap->width) * 4)));
}

void OLED_HWColorFill(GCBITMAP *pBitmap, long x, long y, long cx, long cy, GCCOLOR color)
{
	int dx, dy;
	for (dx = x; dx < x + cx; dx++)
		for(dy = y; dy < y + cy; dy++)
		{
			*((unsigned long*)(pBitmap->handle + (dx + (dy * pBitmap->width)) * 4)) = color;
		}
}

typedef struct GCHWBITMAP
{
    unsigned long width;
    unsigned long height;
    unsigned long address;
} GCHWBITMAP, *PGCHWBITMAP;

GCBOOL OLED_HWSetupDevice(GCDEVICE *pDevice, GCBITMAP *pDisplaySurface, GCHWBITMAP *param)
{
    pDevice->HWBeginAccess = (HWBEGINACCESS) OLED_HWBeginAccess;
    pDevice->HWEndAccess = (HWENDACCESS) OLED_HWEndAccess;
    pDevice->HWGetPixel = (HWGETPIXEL) OLED_HWGetPixel;
    pDevice->HWSetPixel = (HWSETPIXEL) OLED_HWSetPixel;
    pDevice->HWColorFill =  (HWCOLORFILL)OLED_HWColorFill;
    pDevice->HWCopyBits = (HWCOPYBITS) NULL;
    pDevice->HWConstantAlphaBlend = (HWCONSTANTALPHABLEND)NULL;// HWConstantAlphaBlend;
    pDevice->HWTransparentBlt = (HWTRANSPARENTBLT)NULL;
    pDevice->HWDimBuffer = (HWDIMBUFFER)NULL;
    pDevice->HWFrameBytes = (HWFRAMEBYTES)NULL;
    pDevice->HWPresentBase = (HWPRESENTBASE)NULL;
    pDevice->hDevice = pDisplaySurface;
    GCInitializeBitmap(pDisplaySurface, param->width, param->height, param->address, GCDeviceDisplayOnly);
    return GCTRUE;
}

GCBOOL GCCreateOLEDDeviceBitmap (
    GC *pGC,
    unsigned long width,
    unsigned long height,
    unsigned long address)
{
    GCHWBITMAP gchwbitmap = {width, height, address};
    return GCInitialize((HWSETUPDEVICE)OLED_HWSetupDevice, &gchwbitmap, pGC);
}

void GCInitializeOLEDDriver(GC *pGC)
{
	// stop the SPI framebuffer transmitter
    IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x4);

	GCCreateOLEDDeviceBitmap(pGC, 128, 128, &matrix);

	// reset the video ip
    IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x9);
	alt_busy_sleep(500);
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x9);

    // stop Frame Reader
    IOWR(0x8080000, 0, 0);
    // Frame 0 Base Address
    IOWR(0x8080000, 4, matrix);
    // Frame 0 Words
    IOWR(0x8080000, 5,  128*128);
    // Frame 0 Single Cycle Color Patterns
    IOWR(0x8080000, 6,  128*128);
    // Frame 0 Width
    IOWR(0x8080000, 8, 128);
    // Frame 0 Height
    IOWR(0x8080000, 9, 128);
    //// Frame 0 Interlaced
    IOWR(0x8080000, 10, 0);
    // Frame Select 
    IOWR(0x8080000, 3, 0);

	OLED_Init_128128RGB();
    OLED_SetColumnAddress_128128RGB(0x00, 0x7F);
    OLED_SetRowAddress_128128RGB(0x00, 0x7F);
    OLED_WriteMemoryStart_128128RGB();
    IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, DC_PIN);
    int y;
    //for (y = 0; y <(128*128)-6; y++)
    for (y = 0; y <(128*128)-9; y++)
	{
		OLED_Pixel_128128RGB(-1);
	}
	// start the SPI framebuffer transmitter
    // start Frame Reader
    IOWR(0x8080000, 0, 1);
    IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_0_BASE, 0x4);
}

void GCShutdownOLEDDriver(GC *pGC)
{
	alt_busy_sleep(500*10000);
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_0_BASE, 0x4);
	IOWR(0x8080000, 0, 0);
}
