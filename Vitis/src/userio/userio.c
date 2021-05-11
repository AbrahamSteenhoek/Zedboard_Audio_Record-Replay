/******************************************************************************
 * @file userio.c
 *
 * @authors Elod Gyorgy
 *
 * @date 2015-Jan-15
 *
 * @copyright
 * (c) 2015 Copyright Digilent Incorporated
 * All Rights Reserved
 *
 * This program is free software; distributed under the terms of BSD 3-clause
 * license ("Revised BSD License", "New BSD License", or "Modified BSD License")
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name(s) of the above-listed copyright holder(s) nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * @desciption
 *
 * @note
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who          Date        Changes
 * ----- ------------ ----------- --------------------------------------------
 * 1.00  Elod Gyorgy  2015-Jan-15 First release
 *
 * </pre>
 *
 *****************************************************************************/

#include <stdio.h>
#include "xparameters.h"
#include "userio.h"
#include "../record_replay.h"

#define USERIO_DEVICE_ID 	0

extern volatile AudioSystem_t AudioDevice;

const u8 SW[NUM_SWS] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

extern struct GPIO_manager GPIO;
u32* GPIO_BTNS = (u32*)XPAR_AXI_GPIO_VBTNS_BASEADDR;
u32* GPIO_SWS = (u32*)XPAR_AXI_GPIO_VSWS_BASEADDR;
u32* GPIO_LEDS = (u32*)XPAR_AXI_GPIO_LEDS_BASEADDR;

void fnUpdateLedsFromSwitches(XGpio *psGpio);

XStatus fnInitUserIO(XGpio *psGpio)
{
	/* Initialize the GPIO driver. If an error occurs then exit */
	RETURN_ON_FAILURE(XGpio_Initialize(psGpio, USERIO_DEVICE_ID));

	/*
	 * Perform a self-test on the GPIO.  This is a minimal test and only
	 * verifies that there is not any bus error when reading the data
	 * register
	 */
	RETURN_ON_FAILURE(XGpio_SelfTest(psGpio));

	/*
	 * Setup direction register so the switches and buttons are inputs and the LED is
	 * an output of the GPIO
	 */
	XGpio_SetDataDirection(psGpio, BTN_SW_CHANNEL, BTNS_SWS_MASK);

	fnUpdateLedsFromSwitches(psGpio);

	/*
	 * Enable the GPIO channel interrupts so that push button can be
	 * detected and enable interrupts for the GPIO device
	 */
	XGpio_InterruptEnable(psGpio, BTN_SW_INTERRUPT);
	XGpio_InterruptGlobalEnable(psGpio);

	return XST_SUCCESS;
}

void fnUpdateLedsFromSwitches(XGpio *psGpio)
{
	static u32 dwPrevButtons = 0;
	u32  dwBtn;
	u32 dwBtnSw;

	dwBtnSw = XGpio_DiscreteRead(psGpio, BTN_SW_CHANNEL);
	dwBtn = dwBtnSw & (BTNU_MASK|BTNR_MASK|BTND_MASK|BTNL_MASK|BTNC_MASK|BTN8_MASK|BTN9_MASK);
	if (dwBtn==0){//No buttons pressed?
		AudioDevice.fUserIOEvent = 0;
		dwPrevButtons = dwBtn;
		return;
	}
	// Has anything changed?
	if ((dwBtn ^ dwPrevButtons))
	{

		u32 dwChanges = 0;


		dwChanges = dwBtn ^ dwPrevButtons;
		if (dwChanges & BTNU_MASK) {
			AudioDevice.chBtn = 'u';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTNU");
			}
		}
		if (dwChanges & BTNR_MASK) {
			AudioDevice.chBtn = 'r';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTNR");
			}
		}
		if (dwChanges & BTND_MASK) {
			AudioDevice.chBtn = 'd';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTND");
			}
		}
		if (dwChanges & BTNL_MASK) {
			AudioDevice.chBtn = 'l';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTNL");
			}
		}
		if (dwChanges & BTNC_MASK) {
			AudioDevice.chBtn = 'c';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTNC");
			}
		}
		if (dwChanges & BTN8_MASK) {
			AudioDevice.chBtn = '8';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTN8");
			}
		}
		if (dwChanges & BTN9_MASK) {
			AudioDevice.chBtn = '9';
			if(AudioDevice.u8Verbose) {
				xil_printf("\r\nBTN9");
			}
		}
		// Keep values in mind
		//dwPrevSwitches = dwSw;
		AudioDevice.fUserIOEvent = 1;
		dwPrevButtons = dwBtn;
	}
}

/*
 * Default interrupt service routine
 * Lights up LEDs above active switches. Pressing any of the buttons inverts LEDs.
 */
void fnUserIOIsr(void *pvInst)
{
	XGpio *psGpio = (XGpio*)pvInst;

	/*
	 * Disable the interrupt
	 */
	XGpio_InterruptGlobalDisable(psGpio);




	/*
	 * Check if the interrupt interests us
	 */
	if ((XGpio_InterruptGetStatus(psGpio) & BTN_SW_INTERRUPT) !=
			BTN_SW_INTERRUPT) {
		XGpio_InterruptGlobalEnable(psGpio);
		return;
	}

	fnUpdateLedsFromSwitches(psGpio);



	 /* Clear the interrupt such that it is no longer pending in the GPIO */

	XGpio_InterruptClear(psGpio, BTN_SW_INTERRUPT);

	/*
	* Enable the interrupt
	*/
	XGpio_InterruptGlobalEnable(psGpio);
}

void CheckInputs()
{
	GPIO.btn_state = *GPIO_BTNS;
	const u8 btn_state = GPIO.btn_state;

	// detect button event if button state has changed from 0 -> 1
	GPIO.btn_event = ( btn_state != 0x00 && GPIO.prev_btn_state == 0x00 ) ? 1 : 0;

	if ( btn_state & BTNU_MASK )
		GPIO.btn_pressed = BTNU;
	else if ( btn_state & BTND_MASK )
		GPIO.btn_pressed = BTND;
	else if ( btn_state & BTNC_MASK )
		GPIO.btn_pressed = BTNC;
	else if ( btn_state & BTNL_MASK )
		GPIO.btn_pressed = BTNL;
	else if ( btn_state & BTNR_MASK )
		GPIO.btn_pressed = BTNR;
	else if ( btn_state & BTN8_MASK )
		GPIO.btn_pressed = BTN8;
	else if ( btn_state & BTN9_MASK )
		GPIO.btn_pressed = BTN9;

	GPIO.sws_pressed = *GPIO_SWS;

	GPIO.prev_btn_state = btn_state;

}

// Debounce for virtual buttons]
// If button is pressed, returns value GPIO BTN register. Otherwise, returns 0
u8 BtnPressed()
{
    static u32 btn_history = 0;
    static const u32 BTN_PRESS_MASK = 0xFF000FFF;
    u8 btn_pressed = 0;
    const u8 btn_state = *GPIO_BTNS;

    btn_history = ( btn_history << 1 );
    btn_history |= ( btn_state != 0x00 );

    // A valid button has a period of all 0's, a period of throwaway values, and a period of 1's
    // 0x00XXXFFF
    if( ( btn_history & BTN_PRESS_MASK ) == 0x00000FFF )
    {
        btn_pressed = btn_state;
        btn_history = 0xFFFFFFFF;
    } 

    return btn_pressed;
}

u8 BtnReleased()
{
	static u8 prev_btn_state = 0;
	u8 return_val = 0x00;
	u8 btn_state = *GPIO_BTNS;

	if ( btn_state == 0x00 && prev_btn_state != 0x00 )
	{
		return_val = prev_btn_state;
	}
	else
		return_val = 0x00;

	prev_btn_state = btn_state;

	return return_val;
}

u8 SwPressed()
{
    static u32 sw_history = 0;
    static const u32 SWS_PRESS_MASK = 0xFF000FFF;
    u8 sw_pressed = 0;
    const u8 sw_state = *GPIO_SWS;

    sw_history = ( sw_history << 1 );
    sw_history |= ( sw_state != 0x00 );

    // A valid button has a period of all 0's, a period of throwaway values, and a period of 1's
    // 0x00XXXFFF
    if( ( sw_history & SWS_PRESS_MASK ) == 0x00000FFF )
    {
        sw_pressed = sw_state;
        sw_history = 0xFFFFFFFF;
    } 

    return sw_pressed;
}
