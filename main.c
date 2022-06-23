/*------------------------------------------------------------------------
* This Information is proprietary to Phase Dynamics Inc, Richardson, Texas
* and MAY NOT be copied by any method or incorporated into another program
* without the express written consent of Phase Dynamics Inc. This information
* or any portion thereof remains the property of Phase Dynamics Inc.
* The information contained herein is believed to be accurate and Phase
* Dynamics Inc assumes no responsibility or liability for its use in any way
* and conveys no license or title under any patent or copyright and makes
* no representation or warranty that this Information is free from patent
* or copyright infringement.
*
* Copyright (c) 2018 Phase Dynamics Inc. ALL RIGHTS RESERVED.
*------------------------------------------------------------------------*/
/*------------------------------------------------------------------------
* Main.c
*-------------------------------------------------------------------------*/
///////////////////////////////////////////////////////////////////////////
// HISTORY  
///////////////////////////////////////////////////////////////////////////
// ver 1.02.09 | Jan-29-2021 | Daniel Koh | SHIPPED 8843, 8844, 8845, 8846
// ver 1.02.10 | Mar-03-2021 | Daniel Koh | Changed logData, REG_STREAM upload fix 
// ver 1.02.11 | JUN-03-2021 | Daniel Koh | added support for la_offset to ModbusRTU.c 
// ver 1.02.12 | JUN-09-2021 | Daniel Koh | Bug#108: Incorrect CRC calculation due to missing la_offset (serial number)
// ver 1.02.13 | JUL-30-2021 | Daniel Koh | Replaced stone-age MCSDK_1_01 with the latest pdk_omapl138_1_0_11 sdk 
// ver 1.02.15 | AUG-19-2021 | Daniel Koh | bugfix : trimming analog output 
// ver 1.02.19 | Oct-29-2021 | Daniel Koh | fixed i2c walking problem
// ver 1.02.20 | Nov-29-2021 | Daniel Koh | stable with Usb
// ver 1.02.21 | Feb-10-2022 | Daniel Koh | Bug#115
///////////////////////////////////////////////////////////////////////////

#undef MENU_H_

#include <ti/board/board.h>
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "device.h"
#include "Globals.h"

#define NANDWIDTH_16
#define C6748_LCDK

extern void delayTimerSetup(void);
extern void Init_Data_Buffer(void);
extern void resetGlobalVars(void);
extern void upgradeFirmwareTask(void);

static inline void Init_BoardClocks(void);
static inline void initTimer3(void);
static inline void initHardwareObjects(void);
static inline void initSoftwareObjects(void);
static inline void Init_All(void);

int main (void)
{
    /* suspend source register */
    SYSTEM->SUSPSRC &= ((1 << 27) | (1 << 22) | (1 << 20) | (1 << 5) | (1 << 16));

    /* initialize cs4 memory region */
    CSL_FINST(emifaRegs->CE4CFG,EMIFA_CE4CFG_ASIZE,16BIT);
    CSL_FINST(emifaRegs->NANDFCR,EMIFA_NANDFCR_CS4NAND,NAND_ENABLE);

    /* initialize c6748 specific board */
    Init_BoardClocks();

    /* initialize psc */
	Init_PSC();

    /* pin muxing */
	Init_PinMux();

    /* initialize everything else */
	Init_All();

    /* osal delay timer reset */
    delayTimerSetup();

	/* setup watchdog */
	setupWatchdog();

    /* START TI-RTOS KERNEL */
	BIOS_start();

	return 0;
}


static inline void Init_All(void)
{
    // RESTORE ALL VALUES FROM NAND FLASH MEMORY
	Restore_Vars_From_NAND();

    // INITIAL FLASHING OR FACTORY RESET LEVEL
	if (!COIL_LOCKED_SOFT_FACTORY_RESET.val)
	{	
        if (!COIL_LOCKED_HARD_FACTORY_RESET.val) 
        {
            initializeAllRegisters();
        }
		reloadFactoryDefault();
		Store_Vars_in_NAND();
	}
	else resetGlobalVars(); 

	// INITIALIZE HARDWARES 
	initHardwareObjects();

	// INITIALIZE TIMER COUNTER
	initTimer3();

    // CONFIGURE UART BAUDRATE & PARITY OPTIONS
	Config_Uart(REG_BAUD_RATE.calc_val,UART_PARITY_NONE);

	// Initialize software objects
	initSoftwareObjects();

	/* start clock */
	startClocks();
}


void Init_BoardClocks(void)
{
    Uint32 i;
    for (i=0;i<5000000;i++);

    Board_moduleClockSyncReset(CSL_PSC_USB20);
    Board_moduleClockSyncReset(CSL_PSC_GPIO);
    Board_moduleClockSyncReset(CSL_PSC_I2C1);

    Board_moduleClockEnable(CSL_PSC_USB20); // "USB0_REFCLKIN" CLOCK SHARES WITH UART1
    Board_moduleClockEnable(CSL_PSC_GPIO);
    Board_moduleClockEnable(CSL_PSC_I2C1);

    Board_moduleClockEnable(CSL_PSC_UART2);
}


/// NOTE //////////////////////////////////////////////////////
/// A lot of the CSL macros were causing inexplicable problems
/// writing to the RTC, as well as FINST(...,RUN) causing a freeze
/// on power cycle. If it's a bug in CSL it's not immediately
/// obvious... need to look into this when I have more time.
/// Still waiting on a processor board with a working RTC battery.
//////////////////////////////////////////////////////////////

static inline void initSoftwareObjects(void)
{
	Init_Data_Buffer();
}


static inline void initHardwareObjects(void)
{
	// Setup i2c registers and start i2c running (PDI_I2C.c)
	Init_I2C();

	// initialize lcd driver (PDI_I2C.c)
	Init_LCD();

	// Initialize buttons (PDI_I2C.c)
	Init_MBVE();

	// Setup uart registers and start the uart running (ModbusRTU.c)
	Init_Uart();

	// Initialize modbus buffers and counters (ModbusRTU.c)
	Init_Modbus();
}

static inline void initTimer3(void)
{
    int key;

    key = Hwi_disable();

    CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMHIRS,RESET_ON);
    CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMLORS,RESET_ON);
    CSL_FINST(tmr3Regs->TCR,TMR_TCR_ENAMODE_LO,DISABLE);

    /// set timer mode to 64-bit
    CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMMODE,RESETVAL);

    /// set clock source to external
    CSL_FINST(tmr3Regs->TCR,TMR_TCR_CLKSRC_LO,MAX);

    /// take timer out of reset
    CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMHIRS,RESET_OFF);
    CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMLORS,RESET_OFF);

    /// reset counter value to zero (upper & lower)
    tmr3Regs->CNTLO = 0;
    tmr3Regs->CNTHI = 0;

    /// timer period set to max
    tmr3Regs->PRDLO = 0xFFFFFFFF;
    tmr3Regs->PRDHI = 0xFFFFFFFF;

	/// enable frequency
	ctrlGpioPin(97, GPIO_CTRL_SET_OUT_DATA, TRUE, NULL); // set on 

    // Enable counter
    CSL_FINST(tmr3Regs->TCR,TMR_TCR_ENAMODE_LO,ENABLE);

    Hwi_restore(key);
}
