/************************************************************************/
/*																		*/
/*	demo.c	--	Zedboard DMA Demo				 						*/
/*																		*/
/************************************************************************/
/*	Author: Sam Lowe											*/
/*	Copyright 2015, Digilent Inc.										*/
/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*		This file contains code for running a demonstration of the		*/
/*		DMA audio inputs and outputs on the Zedboard.					*/
/*																		*/
/*																		*/
/************************************************************************/
/*  Notes:																*/
/*																		*/
/*		- The DMA max burst size needs to be set to 16 or less			*/
/*																		*/
/************************************************************************/
/*  Revision History:													*/
/* 																		*/
/*		8/23/2016(SamL): Created										*/
/*																		*/
/************************************************************************/


#include "record_replay.h"


#include "audio/audio.h"
#include "dma/dma.h"
#include "intc/intc.h"
#include "userio/userio.h"
#include "iic/iic.h"

/***************************** Include Files *********************************/

#include "xaxidma.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "xdebug.h"
#include "xiic.h"
#include "xaxidma.h"



#ifdef XPAR_INTC_0_DEVICE_ID
 #include "xintc.h"
 #include "microblaze_sleep.h"
#else
 #include "xscugic.h"
#include "sleep.h"
#include "xil_cache.h"
#endif
#include "xtime_l.h"
#include <stdio.h>

/************************** Constant Definitions *****************************/

/*
 * Device hardware build related constants.
 */

// Audio constants
// Number of seconds to record/playback
#define NR_SEC_TO_REC_PLAY		60*3 // 3 mins

// ADC/DAC sampling rate in Hz
//#define AUDIO_SAMPLING_RATE		1000
#define AUDIO_SAMPLING_RATE	  96000

// Number of samples to record/playback max size
#define NR_AUDIO_SAMPLES		(NR_SEC_TO_REC_PLAY*AUDIO_SAMPLING_RATE)

/* Timeout loop counter for reset
 */
#define RESET_TIMEOUT_COUNTER	10000

#define TEST_START_VALUE	0x0

// maximum time allowed for a recording
#define NR_MAX_REC_TIME			10
#define NR_MAX_AUDIO_SAMPLES	( NR_MAX_REC_TIME * AUDIO_SAMPLING_RATE )


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/
#if (!defined(DEBUG))
extern void xil_printf(const char *format, ...);
#endif


/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */

static XIic sIic;
static XAxiDma sAxiDma;		/* Instance of the XAxiDma */

static XGpio sUserIO;

#ifdef XPAR_INTC_0_DEVICE_ID
 static XIntc sIntc;
#else
 static XScuGic sIntc;
#endif

//
// Interrupt vector table
#ifdef XPAR_INTC_0_DEVICE_ID
const ivt_t ivt[] = {
	//IIC
	{XPAR_AXI_INTC_0_AXI_IIC_0_IIC2INTC_IRPT_INTR, (XInterruptHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_AXI_INTC_0_AXI_DMA_0_S2MM_INTROUT_INTR, (XInterruptHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_AXI_INTC_0_AXI_DMA_0_MM2S_INTROUT_INTR, (XInterruptHandler)fnMM2SInterruptHandler, &sAxiDma},
	//User I/O (buttons, switches, LEDs)
	{XPAR_AXI_INTC_0_AXI_GPIO_0_IP2INTC_IRPT_INTR, (XInterruptHandler)fnUserIOIsr, &sUserIO}
};
#else
const ivt_t ivt[] = {
	//IIC
	{XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR, (Xil_ExceptionHandler)XIic_InterruptHandler, &sIic},
	//DMA Stream to MemoryMap Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR, (Xil_ExceptionHandler)fnS2MMInterruptHandler, &sAxiDma},
	//DMA MemoryMap to Stream Interrupt handler
	{XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR, (Xil_ExceptionHandler)fnMM2SInterruptHandler, &sAxiDma},
	//User I/O (buttons, switches, LEDs)
	{XPAR_FABRIC_AXI_GPIO_0_IP2INTC_IRPT_INTR, (Xil_ExceptionHandler)fnUserIOIsr, &sUserIO}
};
#endif


void CheckDMAStatus()
{
		// Checking the DMA S2MM event flag
	if (AudioDevice.fDmaS2MMEvent)
	{
		xil_printf("\r\nRecording Done...");

		// Disable Stream function to send data (S2MM)
		Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
		Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);
		//Flush cache
		//Flush cache


		//microblaze_flush_dcache();
		//Xil_DCacheInvalidateRange((u32) MEM_BASE_ADDR, 5*NR_AUDIO_SAMPLES);
		//microblaze_invalidate_dcache();
		// Reset S2MM event and record flag
		AudioDevice.fDmaS2MMEvent = 0;
		AudioDevice.fAudioRecord = 0;
	}

	// Checking the DMA MM2S event flag
	if (AudioDevice.fDmaMM2SEvent)
	{
		xil_printf("\r\nPlayback Done...");

		// Disable Stream function to send data (S2MM)
		Xil_Out32(I2S_STREAM_CONTROL_REG, 0x00000000);
		Xil_Out32(I2S_TRANSFER_CONTROL_REG, 0x00000000);
		//Flush cache
//					//microblaze_flush_dcache();
		//Xil_DCacheFlushRange((u32) MEM_BASE_ADDR, 5*NR_AUDIO_SAMPLES);
		// Reset MM2S event and playback flag
		AudioDevice.fDmaMM2SEvent = 0;
		AudioDevice.fAudioPlayback = 0;
	}

	// Checking the DMA Error event flag
	if (AudioDevice.fDmaError)
	{
		xil_printf("\r\nDma Error...");
		xil_printf("\r\nDma Reset...");


		AudioDevice.fDmaError = 0;
		AudioDevice.fAudioPlayback = 0;
		AudioDevice.fAudioRecord = 0;
	}
}

int init_system()
{
	*GPIO_LEDS = 0xAA;
	int Status;

	AudioDevice.u8Verbose = 1;

	//Xil_DCacheDisable();

	xil_printf("\r\n--- Entering main() --- \r\n");


	//
	//Initialize the interrupt controller

	Status = fnInitInterruptController(&sIntc);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing interrupts");
		return XST_FAILURE;
	}


	// Initialize IIC controller
	Status = fnInitIic(&sIic);
	if(Status != XST_SUCCESS) {
		xil_printf("Error initializing I2C controller");
		return XST_FAILURE;
	}

    // Initialize User I/O driver
    Status = fnInitUserIO(&sUserIO);
    if(Status != XST_SUCCESS) {
    	xil_printf("User I/O ERROR");
    	return XST_FAILURE;
    }


	//Initialize DMA
	Status = fnConfigDma(&sAxiDma);
	if(Status != XST_SUCCESS) {
		xil_printf("DMA configuration ERROR");
		return XST_FAILURE;
	}


	//Initialize Audio I2S
	Status = fnInitAudio();
	if(Status != XST_SUCCESS) {
		xil_printf("Audio initializing ERROR");
		return XST_FAILURE;
	}

	return Status;
}


XTime start_rec, end_rec;

double GetTime( XTime start, XTime end )
{
	uint64_t time_diff = end - start;
	double time_elapsed = ( ((double)time_diff) / COUNTS_PER_SECOND );

	return time_elapsed;
}

double TestButtons()
{
	u8 btn_val = BtnPressed();

	if ( btn_val != 0x00 )
	{
		XTime_GetTime( &start_rec );
		// xil_printf("\r          \r");
		switch( btn_val )
		{
		case BTNU:
			xil_printf( "BTNU pressed\r\n" );
			break;
		case BTND:
			xil_printf( "BTND pressed\r\n" );
			break;
		case BTNC:
			xil_printf( "BTNC pressed\r\n" );
			break;
		case BTNL:
			xil_printf( "BTNL pressed\r\n" );
			break;
		case BTNR:
			xil_printf( "BTNR pressed\r\n" );
			break;
		}
	}

	btn_val = BtnReleased();
	if ( btn_val != 0x00 )
	{
		XTime_GetTime( &end_rec );
		// xil_printf("\r          \r");
		switch( btn_val )
		{
		case BTNU:
			xil_printf( "BTNU released\r\n" );
			break;
		case BTND:
			xil_printf( "BTND released\r\n" );
			break;
		case BTNC:
			xil_printf( "BTNC released\r\n" );
			break;
		case BTNL:
			xil_printf( "BTNL released\r\n" );
			break;
		case BTNR:
			xil_printf( "BTNR released\r\n" );
			break;
		}

		double time_elapsed = GetTime( start_rec, end_rec );
		printf( "BTN held for %f seconds\r\n", time_elapsed );
		return time_elapsed;
	}
}

/*****************************************************************************/
/**
*
* Main function
*
* This function is the main entry of the interrupt test. It does the following:
*	Initialize the interrupt controller
*	Initialize the IIC controller
*	Initialize the User I/O driver
*	Initialize the DMA engine
*	Initialize the Audio I2S controller
*	Enable the interrupts
*	Wait for a button event then start selected task
*	Wait for task to complete
*
* @param	None
*
* @return
*		- XST_SUCCESS if example finishes successfully
*		- XST_FAILURE if example fails.
*
* @note		None.
*
******************************************************************************/
int main(void)
{
	init_system();

	// Enable all interrupts in our interrupt vector table
	// Make sure all driver instances using interrupts are initialized first
	fnEnableInterrupts(&sIntc, &ivt[0], sizeof(ivt)/sizeof(ivt[0]));


    xil_printf("\r\nInitialization done\r\n");
    xil_printf("\r\nControls:\r\n");
	xil_printf("BTNL: Record on MIC IN\r\n");
	xil_printf("BTNR: Play on LINE OUT\r\n");
	xil_printf("BTN8: Advance Replay Index\r\n" );
	xil_printf("BTND: Rewind Replay Index\r\n" );
    // xil_printf("\tBTNL: Play recording on LINE OUT\r\n");
    // xil_printf("\tBTNU: Record from MIC IN\r\n");
    // xil_printf("\tBTND: Play recording on HPH OUT\r\n");
    // xil_printf("\tBTNR: Record from LINE IN\r\n\r\n");

	// double user_float = 0;
	// char user_input[10];

	// while( 1 )
	// {
	// 	xil_printf("Input a float\r\n");
	// 	scanf( "%s", &user_input );
	// 	printf( "Got a float string: %s\r\n", user_input );
  	// 	user_float = atof( user_input );
	// 	printf( "Got a float: %f\r\n", user_float );

	// 	u32 temp = user_float * 10;
	// 	user_float = temp;
	// 	user_float /= 10;
	// 	printf( "Got a float (rounded): %f\r\n", user_float );

	// }
	// int user_num;

	u32 sample_size = 0;
    while(1)
	{
		CheckDMAStatus();

		u8 btn_val = BtnPressed();
		if ( btn_val != 0x00 )
		{
			switch( btn_val )
			{
				// record on MIC IN
				case BTNL:
		 			xil_printf("BTNL\r\n");
		 			if ( AudioDeviceAvailable() )
		 			{
		 				// xil_printf("Start Recording...\r\n");
						xil_printf( "Specify length of recording (sec): " );
						double user_float = 0;
						char user_input[10];
						scanf( "%s", user_input );
						user_float = atof( user_input );

						sample_size = ( ( user_float * 10 ) * AUDIO_SAMPLING_RATE ) / 10;

						xil_printf( "Recording at index %d", RECORD_INDEX );
		 				fnSetMicInput();

		 				fnRecordNewSample(sAxiDma,sample_size);
		 				AudioDevice.fAudioRecord = 1;
		 			}
		 			else
		 			{
		 				if (AudioDevice.fAudioRecord)
		 				{
		 					xil_printf("\r\nStill Recording...\r\n");
		 				}
		 				else
		 				{
		 					xil_printf("\r\nStill Playing back...\r\n");
		 				}
		 			}
					break;
				// play on line out
				case BTNR:
		 			xil_printf("BTNR\r\n");
		 			if ( AudioDeviceAvailable() )
		 			{
		 				xil_printf("\r\nStart Playback...\r\n");
		 				fnSetLineOutput();
		 				fnReplayNewSample(sAxiDma,sample_size);
		 				AudioDevice.fAudioPlayback = 1;
		 			}
		 			else
		 			{
		 				if (AudioDevice.fAudioRecord)
		 				{
		 					xil_printf("\r\nStill Recording...\r\n");
		 				}
		 				else
		 				{
		 					xil_printf("\r\nStill Playing back...\r\n");
		 				}
		 			}
					break;
				case BTN8://TODO /Make btnu
		 			xil_printf("BTN8\r\n");
					AdvanceReplayIndex();
					break;
				case BTND:
		 			xil_printf("BTND\r\n");
					RewindReplayIndex();
					break;
				case BTNU://TODO /Make btn8
					xil_printf("BTNU\r\n");
					//volumne up
					break;
				case BTN9://TODO
					xil_printf("BTN9\r\n");
					//concat
					break;
				default:
					break;
			}
		}

	}

	xil_printf("\r\n--- Exiting main() --- \r\n");


	return XST_SUCCESS;

}
