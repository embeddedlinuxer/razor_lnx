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
///////////////////////////////////////////////////////////////////////////
// HISTORY
///////////////////////////////////////////////////////////////////////////
// ver 1.02.09 | Jan-29-2021 | Daniel Koh | SHIPPED 8843, 8844, 8845, 8846
// ver 1.02.10 | Mar-03-2021 | Daniel Koh | Changed logData, REG_STREAM upload fix
// ver 1.02.11 | Jun-01-2021 | Daniel Koh | la_offet added to 0x10 ModbusRTU.c
// ver 1.02.12 | Jun-28-2021 | Daniel Koh |	fixed modbus CRC for calibration 
// ver 1.02.13 | Jul-06-2021 | Daniel Koh | completely replaced ancient-year-old sdk with the latest sdk 
//

#undef MENU_H_

#include "Globals.h"

extern void setupwatchdog(void);
extern void delayTimerSetup(void);
extern void Init_Data_Buffer(void);
extern void resetGlobalVars(void);

void Init_BoardClocks(void);
static inline void Init_USB_HighSpeed(void);
static inline void Init_Counter_3(void);
static inline void initHardwareObjects(void);
static inline void initSoftwareObjects(void);
static inline void startClocks(void);
static inline void enableUpgradeMode(void);
static inline void Init_All(void);

int main (void)
{
	/* Configure the suspend source register.
       DSP as the source of the emulation suspend for EMAC, I2C, UART SPI and Timer  */
	sysRegs->SUSPSRC &= ( (1 << 27) | (1 << 22) | (1 << 20) | (1 << 5) | (1 << 16));

    /// INITIALIZE EMIF FOR CS4 FLASH MEMORY REGION
    CSL_FINST(emifaRegs->CE4CFG,EMIFA_CE4CFG_ASIZE,16BIT);
    CSL_FINST(emifaRegs->NANDFCR,EMIFA_NANDFCR_CS4NAND,NAND_ENABLE);

    /// INITIALIZE C6748 SPECIFcd IC BOARD CLOCKS
    Init_BoardClocks();

    /// INITIALIZE POWER AND SLEEP CONTROLLER
	Init_PSC();

    /// INITIALIZE PIN MUXING
	Init_PinMux();

    /// INITIALIZE EVERYTHING ELSE
	Init_All();

    /// INITIALIZE USB HIGHSPEED 
    Init_USB_HighSpeed();

    /// osal delay timer reset
    delayTimerSetup();

	/// set upgrade flags
	enableUpgradeMode();
	
    /// SETUP WATCHDOG
    setupWatchDog();
	
    /// START TI-RTOS KERNEL
	BIOS_start();

	return 0;
}


static inline void enableUpgradeMode(void)
{
	/// enable upgrade mode
    isPdiUpgradeMode = TRUE;
    isUploadCsv = TRUE;
    isDownloadCsv = TRUE;
    isUpgradeFirmware = TRUE;
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
	Init_Counter_3();

    // CONFIGURE UART BAUDRATE & PARITY OPTIONS
	Config_Uart(REG_BAUD_RATE.calc_val,UART_PARITY_NONE);
                    
	// Initialize software objects
	initSoftwareObjects();

	// START VARIOUS CLOCKS 
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

static inline void startClocks(void)
{
	Clock_start(Update_Relays_Clock);
	Clock_start(Capture_Sample_Clock);
    Clock_start(I2C_LCD_Clock);
    Clock_start(I2C_Start_Pulse_MBVE_Clock);
    Clock_start(Process_Menu_Clock);
}

static inline void Init_Counter_3(void)
{
    int key;

    key = Hwi_disable();

	CSL_TmrHwSetup hwSetup = CSL_TMR_HWSETUP_DEFAULTS; 
	CSL_TmrEnamode TimeCountMode = CSL_TMR_ENAMODE_ENABLE; 

	CSL_FINST(tmr3Regs->TGCR, TMR_TGCR_TIMHIRS, RESET_ON); 
	CSL_FINST(tmr3Regs->TGCR, TMR_TGCR_TIMLORS, RESET_ON);
    CSL_FINST(tmr3Regs->TCR, TMR_TCR_ENAMODE_HI, DISABLE);

    /// set timer mode to 64-bit
	hwSetup.tmrTimerMode     = CSL_TMR_TIMMODE_GPT; 

    /// Set clock source to external
	hwSetup.tmrClksrcLo     = CSL_TMR_CLKSRC_TMRINP; 
	CSL_tmrHwSetup(tmr3Regs, &hwSetup); 

    /// take timer out of reset
	CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMHIRS,RESET_OFF);
    CSL_FINST(tmr3Regs->TGCR,TMR_TGCR_TIMLORS,RESET_OFF);

    /// reset counter value to zero (upper & lower)
    tmr3Regs->CNTLO = 0;
    tmr3Regs->CNTHI = 0;

    /// timer period set to max
    tmr3Regs->PRDLO = 0xFFFFFFFF;
    tmr3Regs->PRDHI = 0xFFFFFFFF;

    /// Enable counter
	CSL_tmrHwControl(tmr3Regs, CSL_TMR_CMD_START_TIMLO, (void *)&TimeCountMode);

    Hwi_restore(key);

    /// start counter timer
    Timer_start(counterTimerHandle);
}

static inline void Init_USB_HighSpeed(void)
{
    int key;

    key = Hwi_disable();
    CSL_FINS(usbRegs->POWER,USB_OTG_POWER_HSEN,1);
    Hwi_restore(key);
}
