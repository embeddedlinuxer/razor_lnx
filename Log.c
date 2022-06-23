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
* Razor USB does the following things.
*
* 1. Firmware upgrade (menu 3.9)
* 2. CSV file upload (menu 3.8)
* 3. CSV file download (menu 3.8)
* 4. Data logging (menu 2.3.1)
* 
* However, data logging is not stable and most likely will stop logging after running some time due to
* USB failures. TI RTOS (SYS/BIOS) usb tranmission speed is extremly slow in host 
* mode, which is, "I think", the root cause of our data logging failures.
*------------------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ti/drv/usb/example/common/hardware.h>
#include <ti/fs/fatfs/FATFS.h>
#include "fatfs_port_usbmsc.h"
#include "usbhmsc.h"
#include "usb_osal.h"
#include "Globals.h"
#include "Menu.h"

#define USB3SS_EN
#define NANDWIDTH_16
#define OMAPL138_LCDK
#define USB_INSTANCE    	0
#define SOC_CACHELINE_SIZE  (64U)

#define MAX_ENTRY_SIZE  	50 
#define MAX_HEAD_SIZE   	110 
#define USB_BLOCK_SIZE		512
#define MAX_DATA_SIZE  		USB_BLOCK_SIZE*400 // 200 KB
#define MAX_CSV_SIZE   		USB_BLOCK_SIZE*24

extern void TimerWatchdogReactivate(unsigned int baseAddr);

static USB_Handle usb_handle;
static USB_Params usb_host_params;
static FIL logWriteObject  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));
static char LOG_HEAD[MAX_HEAD_SIZE]  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));
static char TEMP_BUF[USB_BLOCK_SIZE]  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));
static char DATA_BUF[MAX_DATA_SIZE]  __attribute__ ((aligned (SOC_CACHELINE_SIZE)));
static char logFile[] = "0:PDI/LOG_01_01_2019.csv";
static int time_counter = 1;
static int prev_sec = 0;
static Uint8 read_counter = 0;
static unsigned int g_ulMSCInstance = 0; 

static Uint8 current_day = 99;
static int USB_RTC_SEC = 0; 
static int USB_RTC_MIN = 0; 
static int USB_RTC_HR = 0; 
static int USB_RTC_DAY = 0; 
static int USB_RTC_MON = 0; 
static int USB_RTC_YR = 0; 

/* ========================================================================== */
/*                                Prototypes                                  */
/* ========================================================================== */

void usbHostIntrConfig(USB_Params* usbParams);
void MSCCallback(uint32_t ulInstance, uint32_t ulEvent, void *pvData);
void usbCoreIntrHandler(uint32_t* pUsbParam);

/*****************************************************************************
*
* Hold the current state for the application.
*
****************************************************************************/
typedef enum
{
    // No device is present.
    STATE_NO_DEVICE,

    // Mass storage device is being enumerated.
    STATE_DEVICE_ENUM, 

    // Mass storage device is ready.
    STATE_DEVICE_READY,

    // An unsupported device has been attached.
    STATE_UNKNOWN_DEVICE,

    // A power fault has occurred.
    STATE_POWER_FAULT

} tState;

volatile tState g_eState;


/*****************************************************************************
*
* FAT fs variables.
*
*****************************************************************************/
/* USBMSC function table for USB implementation */

FATFS_DrvFxnTable FATFS_drvFxnTable = {
    FATFSPortUSBDiskClose,      /* closeDrvFxn */
    FATFSPortUSBDiskIoctl,      /* controlDrvFxn */
    FATFSPortUSBDiskInitialize, /* initDrvFxn */
    FATFSPortUSBDiskOpen,       /* openDrvFxn */
    FATFSPortUSBDiskWrite,      /* writeDrvFxn */
    FATFSPortUSBDiskRead        /* readDrvFxn */
};

/* FATFS configuration structure */
FATFS_HwAttrs FATFS_initCfg[_VOLUMES] =
{
    {0U}, {1U}, {2U}, {3U}
};

/* FATFS objects */
FATFS_Object FATFS_objects[_VOLUMES];

/* FATFS configuration structure */
const FATFS_Config FATFS_config[_VOLUMES + 1] = {
    {
        &FATFS_drvFxnTable,
        &FATFS_objects[0],
        &FATFS_initCfg[0]
    },

    {
         &FATFS_drvFxnTable,
         &FATFS_objects[1],
         &FATFS_initCfg[1]
    },

    {
         &FATFS_drvFxnTable,
         &FATFS_objects[2],
         &FATFS_initCfg[2]
    },

    {
         &FATFS_drvFxnTable,
         &FATFS_objects[3],
         &FATFS_initCfg[3]
    },
    {NULL, NULL, NULL}
};

FATFS_Handle fatfsHandle = NULL;
uint32_t     g_fsHasOpened = 0;


void usbHostIntrConfig(USB_Params* usbParams)
{
    HwiP_Handle hwiHandle = NULL;
    OsalRegisterIntrParams_t interruptRegParams;

    /* Initialize with defaults */
    Osal_RegisterInterrupt_initParams(&interruptRegParams);

    /* Populate the interrupt parameters */
    interruptRegParams.corepacConfig.name=NULL;
    interruptRegParams.corepacConfig.corepacEventNum=SYS_INT_USB0; /* Event going in to CPU */
    interruptRegParams.corepacConfig.intVecNum= OSAL_REGINT_INTVEC_EVENT_COMBINER; /* Host Interrupt vector */
    interruptRegParams.corepacConfig.isrRoutine = (void (*)(uintptr_t))usbCoreIntrHandler;
    interruptRegParams.corepacConfig.arg = (uintptr_t)usbParams;

    Osal_RegisterInterrupt(&interruptRegParams,&hwiHandle);
    USB_irqConfig(usbParams->usbHandle, usbParams);
}

/*****************************************************************************
*
* This is the callback from the MSC driver.
*
* \param ulInstance is the driver instance which is needed when communicating
* with the driver.
* \param ulEvent is one of the events defined by the driver.
* \param pvData is a pointer to data passed into the initial call to register
* the callback.
*
* This function handles callback events from the MSC driver.  The only events
* currently handled are the MSC_EVENT_OPEN and MSC_EVENT_CLOSE.  This allows
* the main routine to know when an MSC device has been detected and
* enumerated and when an MSC device has been removed from the system.
*
* \return Returns \e true on success or \e false on failure.
*
*****************************************************************************/
void
MSCCallback(uint32_t ulInstance, uint32_t ulEvent, void *pvData)
{
	char dummy[] = "100";
	snprintf(dummy,2,"%d",USB_RTC_SEC);

    /*
    * Determine the event.
    */
    switch(ulEvent)
    {
        // Called when the device driver has successfully enumerated an MSC
        case MSC_EVENT_OPEN:
        {
            // Proceed to the enumeration state.
            g_eState = STATE_DEVICE_ENUM;

            break;
        }

        // Called when the device driver has been unloaded due to error or
        // the device is no longer present.
        case MSC_EVENT_CLOSE:
        {
            // Go back to the "no device" state and wait for a new connection.
			g_eState = STATE_NO_DEVICE;
            g_fsHasOpened = 0;
            usbStatus = 0;
            FATFS_close(fatfsHandle);
            resetUsbStaticVars();

            break;
        }

        default:
        {
            break;
        }
    }
}

/* main entry point for USB host core interrupt handler with USB Wrapper setup
* Matching interrupt call-back function API */
void usbCoreIntrHandler(uint32_t* pUsbParam)
{
    USB_coreIrqHandler(((USB_Params*)pUsbParam)->usbHandle, (USB_Params*)pUsbParam);
}


void resetCsvStaticVars(void)
{
	isUpdateDisplay = FALSE;
	isWriteRTC = FALSE;
	isDownloadCsv = FALSE;
	isScanCsvFiles = FALSE;
	isUploadCsv = FALSE;
	isResetPower = FALSE;
	isCsvDownloadSuccess = FALSE;
	isScanSuccess = FALSE;
	isLogData = FALSE;

	/// disable usb access flags
	CSV_FILES[0] = '\0';
	csvCounter = 0;
}

void resetUsbStaticVars(void)
{
	current_day = 99;
	usbStatus = 0;
	read_counter = 0;
}

void errorUsb(FRESULT fr)
{
	resetCsvStaticVars();
	resetUsbStaticVars();

    /// find out why it failed
         if (fr == FR_DISK_ERR) usbStatus = 2;
    else if (fr == FR_INT_ERR) usbStatus = 3;
    else if (fr == FR_NOT_READY) usbStatus = 4;
    else if (fr == FR_NO_FILE) usbStatus = 5;
    else if (fr == FR_NO_PATH) usbStatus = 6;
    else if (fr == FR_INVALID_NAME) usbStatus = 7;
    else if (fr == FR_DENIED) usbStatus = 8;
    else if (fr == FR_INVALID_OBJECT) usbStatus = 9;
    else if (fr == FR_WRITE_PROTECTED) usbStatus = 10;
    else if (fr == FR_INVALID_DRIVE) usbStatus = 11;
    else if (fr == FR_NOT_ENABLED) usbStatus = 12;
    else if (fr == FR_NO_FILESYSTEM) usbStatus = 13;
    else if (fr == FR_TIMEOUT) usbStatus = 14;
    else if (fr == FR_LOCKED) usbStatus = 15;
    else if (fr == FR_NOT_ENOUGH_CORE) usbStatus = 16;
    else usbStatus = 2;

    return;
}

void logData(void)
{
	static int i = 0;
	static char entry[MAX_ENTRY_SIZE];
	static double LOG_REGS[20];

	entry[0] = '\0';
	LOG_REGS[0] = '\0';
	LOG_HEAD[0] = '\0';
	DATA_BUF[0] = '\0';
	read_counter = 0;

	while (1)
	{
		Semaphore_pend(logData_sem, BIOS_WAIT_FOREVER);
		
   		if (REG_RTC_SEC != prev_sec)
		{
    		FRESULT fr;
			prev_sec = REG_RTC_SEC;
			time_counter++;

			if (time_counter % REG_LOGGING_PERIOD == 0)
			{
				time_counter = 0;

				/* update temp time */
				USB_RTC_SEC = REG_RTC_SEC;
				if (USB_RTC_MIN != REG_RTC_MIN) USB_RTC_MIN = REG_RTC_MIN;
				if (USB_RTC_HR != REG_RTC_HR)   USB_RTC_HR = REG_RTC_HR;
				if (USB_RTC_DAY != REG_RTC_DAY) USB_RTC_DAY = REG_RTC_DAY;
				if (USB_RTC_MON != REG_RTC_MON) USB_RTC_MON = REG_RTC_MON;
				if (USB_RTC_YR != REG_RTC_YR)   USB_RTC_YR = REG_RTC_YR;

   				/* need a new file? */
   				if (current_day != USB_RTC_DAY) 
   				{   
       				current_day = USB_RTC_DAY;

        			fr = f_mkdir("0:PDI");

        			if ((fr != FR_EXIST) && (fr != FR_OK)) errorUsb(fr);
					else
					{
        				sprintf(logFile,"0:PDI/LOG_%02d_%02d_20%02d.csv",USB_RTC_MON, USB_RTC_DAY, USB_RTC_YR); 

        				if (f_open(&logWriteObject, logFile, FA_WRITE | FA_OPEN_EXISTING) == FR_OK) f_close(&logWriteObject);
						else
						{
       						if (f_open(&logWriteObject, logFile, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) errorUsb(fr);
							else
							{
								sprintf(LOG_HEAD,"\nFirmware:,%5s\nSerial Number:,%5d\n\nDate,Time,Alarm,Stream,Watercut,Watercut_Raw,", FIRMWARE_VERSION, REG_SN_PIPE);

       							f_puts(LOG_HEAD,&logWriteObject);
       							f_sync(&logWriteObject);
       	
								sprintf(LOG_HEAD,"Temp(C),Avg_Temp(C),Temp_Adj,Freq(Mhz),Oil_Index,RP(V),Oil_PT,Oil_P0,Oil_P1,");
								f_puts(LOG_HEAD,&logWriteObject);
       							f_sync(&logWriteObject);
       	
								sprintf(LOG_HEAD,"Density,Oil_Freq_Low,Oil_Freq_Hi,AO_LRV,AO_URV,AO_MANUAL_VAL,Relay_Setpoint\n");
       							f_puts(LOG_HEAD,&logWriteObject);

       							f_close(&logWriteObject);

								TimerWatchdogReactivate(CSL_TMR_1_REGS);
							}	
        				}
		   			}
				}   

    			/* get data */	
    			LOG_REGS[0] = DIAGNOSTICS;
        		LOG_REGS[1] = REG_STREAM.calc_val;
        		LOG_REGS[2] = REG_WATERCUT.calc_val;
        		LOG_REGS[3] = REG_WATERCUT_RAW;
        		LOG_REGS[4] = REG_TEMP_USER.calc_val;
        		LOG_REGS[5] = REG_TEMP_AVG.calc_val;
        		LOG_REGS[6] = REG_TEMP_ADJUST.calc_val;
        		LOG_REGS[7] = REG_FREQ.calc_val;
        		LOG_REGS[8] = REG_OIL_INDEX.calc_val;
        		LOG_REGS[9] = REG_OIL_RP;
        		LOG_REGS[10] = REG_OIL_PT;
        		LOG_REGS[11] = REG_OIL_P0.calc_val;
        		LOG_REGS[12] = REG_OIL_P1.calc_val;
        		LOG_REGS[13] = REG_OIL_DENSITY.calc_val;
        		LOG_REGS[14] = REG_OIL_FREQ_LOW.calc_val;
        		LOG_REGS[15] = REG_OIL_FREQ_HIGH.calc_val;
        		LOG_REGS[16] = REG_AO_LRV.calc_val;
        		LOG_REGS[17] = REG_AO_URV.calc_val;
        		LOG_REGS[18] = REG_AO_MANUAL_VAL;
        		LOG_REGS[19] = REG_RELAY_SETPOINT.calc_val;

    			/* get date */	
    			sprintf(TEMP_BUF,"%02d-%02d-20%02d,%02d:%02d:%02d,",USB_RTC_MON,USB_RTC_DAY,USB_RTC_YR,USB_RTC_HR,USB_RTC_MIN,USB_RTC_SEC);

    			/* get data */	
				for (i=0;i<20;i++) 
				{
					sprintf(entry,"%g,",LOG_REGS[i]);
					strcat(TEMP_BUF,entry);
				}

				strcat(TEMP_BUF,"\n");
				strcat(DATA_BUF,TEMP_BUF);

				TimerWatchdogReactivate(CSL_TMR_1_REGS);

				if (read_counter > 5)
				{
					strcat(DATA_BUF,"\0");
   					f_open(&logWriteObject, logFile, FA_WRITE | FA_OPEN_EXISTING);
  					f_lseek(&logWriteObject,f_size(&logWriteObject));
					f_puts(DATA_BUF,&logWriteObject);
   					f_close(&logWriteObject);

    				DATA_BUF[0] = '\0';
					read_counter = 0;
					TimerWatchdogReactivate(CSL_TMR_1_REGS);
				}
				else read_counter++;
			}
		}
	}
}


void downloadCsv(void)
{
	isDownloadCsv = FALSE;

	FRESULT fr;	
	FIL csvWriteObject;
	char csvFileName[50] = {0};
	char CSV_BUF[MAX_CSV_SIZE] = {0};
    char lcdModelCode[] = "INCDYNAMICSPHASE";
	int i, data_index;

	/* get file name */
    sprintf(csvFileName,"0:R%06d.csv",REG_SN_PIPE);

    fr = f_open(&csvWriteObject, csvFileName, FA_WRITE | FA_CREATE_ALWAYS); 
	usb_osalDelayMs(1000);
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
	if (fr != FR_OK) return;

	/* model code */
    for (i=0;i<4;i++)
    {
        lcdModelCode[i*4+3] = (REG_MODEL_CODE[i] >> 24) & 0xFF;
        lcdModelCode[i*4+2] = (REG_MODEL_CODE[i] >> 16) & 0xFF;
        lcdModelCode[i*4+1] = (REG_MODEL_CODE[i] >> 8)  & 0xFF;
        lcdModelCode[i*4+0] = (REG_MODEL_CODE[i] >> 0)  & 0xFF;
    }

	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// integer
    sprintf(CSV_BUF+strlen(CSV_BUF),"Serial,,201,int,1,RW,1,%d\n",REG_SN_PIPE); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO Dampen,,203,int,1,RW,1,%d\n",REG_AO_DAMPEN); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Slave Address,,204,int,1,RW,1,%d\n",REG_SLAVE_ADDRESS); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Stop Bits,,205,int,1,RW,1,%d\n",REG_STOP_BITS); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Density Mode,,206,int,1,RW,1,%d\n",REG_DENSITY_MODE); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Model Code,,219,int,1,RW,1,%s\n",lcdModelCode); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Logging Period,,223,int,1,RW,1,%d\n",REG_LOGGING_PERIOD); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO Alarm Mode,,227,int,1,RW,1,%d\n",REG_AO_ALARM_MODE); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Phase Hold Over,,228,int,1,RW,1,%d\n",REG_PHASE_HOLD_CYCLES); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Relay Delay,,229,int,1,RW,1,%d\n",REG_RELAY_DELAY); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO Mode,,230,int,1,RW,1,%d\n",REG_AO_MODE); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Density Correction Mode,,231,int,1,RW,1,%d\n",REG_OIL_DENS_CORR_MODE);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Relay Mode,,232,int,1,RW,1,%d\n",REG_RELAY_MODE); 
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// float or double
	sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Adjust,,15,float,1,RW,1,%015.7f\n",REG_OIL_ADJUST.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Temp Adjust,,31,float,1,RW,1,%015.7f\n",REG_TEMP_ADJUST.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Proc Avg,,35,float,1,RW,1,%015.7f\n",REG_PROC_AVGING.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Index,,37,float,1,RW,1,%015.7f\n",REG_OIL_INDEX.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil P0,,39,float,1,RW,1,%015.7f\n",REG_OIL_P0.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil P1,,41,float,1,RW,1,%015.7f\n",REG_OIL_P1.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Low,,43,float,1,RW,1,%015.7f\n",REG_OIL_FREQ_LOW.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil High,,45,float,1,RW,1,%015.7f\n",REG_OIL_FREQ_HIGH.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Sample Period,,47,float,1,RW,1,%05.1f\n",REG_SAMPLE_PERIOD.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO LRV,,49,float,1,RW,1,%015.7f\n",REG_AO_LRV.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO URV,,51,float,1,RW,1,%015.7f\n",REG_AO_URV.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Baud Rate,,55,float,1,RW,1,%010.1f\n",REG_BAUD_RATE.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Calc Max,,67,float,1,RW,1,%015.7f\n",REG_OIL_CALC_MAX); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Dual Curve Cutoff,,69,float,1,RW,1,%015.7f\n",REG_OIL_PHASE_CUTOFF);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Stream,,73,float,1,RW,1,%010.1f\n",REG_STREAM.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO Trim Low,,107,float,1,RW,1,%015.7f\n",REG_AO_TRIMLO); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AO Trim High,,109,float,1,RW,1,%015.7f\n",REG_AO_TRIMHI); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Density Adj,,111,float,1,RW,1,%015.7f\n",REG_DENSITY_ADJ);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Density Unit,,113,float,1,RW,1,%010.0f\n",REG_DENSITY_UNIT.val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"D3,,117,float,1,RW,1,%015.7f\n",REG_DENSITY_D3.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"D2,,119,float,1,RW,1,%015.7f\n",REG_DENSITY_D2.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"D1,,121,float,1,RW,1,%015.7f\n",REG_DENSITY_D1.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"D0,,123,float,1,RW,1,%015.7f\n",REG_DENSITY_D0.calc_val);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Dens Calibration Val,,125,float,1,RW,1,%015.7f\n",REG_DENSITY_CAL_VAL.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Relay Setpoint,,151,float,1,RW,1,%015.7f\n",REG_RELAY_SETPOINT.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Density Manual,,161,float,1,RW,1,%015.7f\n",REG_OIL_DENSITY_MANUAL); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Density AI LRV,,163,float,1,RW,1,%015.7f\n",REG_OIL_DENSITY_AI_LRV.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Density AI URV,,165,float,1,RW,1,%015.7f\n",REG_OIL_DENSITY_AI_URV.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AI Trim Low,,169,float,1,RW,1,%015.7f\n",REG_AI_TRIMLO); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"AI Trim High,,171,float,1,RW,1,%015.7f\n",REG_AI_TRIMHI); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil T0,,179,float,1,RW,1,%015.7f\n",REG_OIL_T0.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil T1,,181,float,1,RW,1,%015.7f\n",REG_OIL_T1.calc_val); 
    sprintf(CSV_BUF+strlen(CSV_BUF),"PDI Temp Adj,,781,float,1,RW,1,%015.7f\n",PDI_TEMP_ADJ);
    sprintf(CSV_BUF+strlen(CSV_BUF),"PDI Freq F0,,783,float,1,RW,1,%015.7f\n",PDI_FREQ_F0);
    sprintf(CSV_BUF+strlen(CSV_BUF),"PDI Freq F1,,785,float,1,RW,1,%015.7f\n",PDI_FREQ_F1);
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// extended 60K
    sprintf(CSV_BUF+strlen(CSV_BUF),"Number of Oil Temperature Curves,,60001,float,1,RW,1,%015.7f\n",REG_TEMP_OIL_NUM_CURVES);
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Temperature List,,60003,float,1,RW,10,%015.7f,%015.7f,%015.7f,%015.7f,%015.7f,%015.7f,%015.7f,%015.7f,%015.7f,%015.7f\n",REG_TEMPS_OIL[0],REG_TEMPS_OIL[1],REG_TEMPS_OIL[2],REG_TEMPS_OIL[3],REG_TEMPS_OIL[4],REG_TEMPS_OIL[5],REG_TEMPS_OIL[6],REG_TEMPS_OIL[7],REG_TEMPS_OIL[8],REG_TEMPS_OIL[9]);
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	for (data_index=0;data_index<10;data_index++)
    sprintf(CSV_BUF+strlen(CSV_BUF),"Oil Curve %d,,%d,float,1,RW,4,%015.7f,%015.7f,%015.7f,%015.7f\n",data_index,60023+data_index*8,REG_COEFFS_TEMP_OIL[data_index][0],REG_COEFFS_TEMP_OIL[data_index][1],REG_COEFFS_TEMP_OIL[data_index][2],REG_COEFFS_TEMP_OIL[data_index][3]);
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// long int	
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Measurement Section,,301,long,1,RW,1,%d\n",REG_MEASSECTION_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Back Board,,303,long,1,RW,1,%d\n",REG_BACKBOARD_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Safety Barrier,,305,long,1,RW,1,%d\n",REG_SAFETYBARRIER_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Power Supply,,307,long,1,RW,1,%d\n",REG_POWERSUPPLY_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Processor Board,,309,long,1,RW,1,%d\n",REG_PROCESSOR_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Display Board,,311,long,1,RW,1,%d\n",REG_DISPLAY_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - RF Board,,313,long,1,RW,1,%d\n",REG_RF_SN);
    sprintf(CSV_BUF+strlen(CSV_BUF),"SN - Assembly,,315,long,1,RW,1,%d\n",REG_ASSEMBLY_SN);
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// hardware part serial number
	for (data_index=0;data_index<8;data_index++)
    sprintf(CSV_BUF+strlen(CSV_BUF),"Electronic SN %d,,%d,long,1,RW,1,%d\n",data_index,317+2*data_index,REG_ELECTRONICS_SN[data_index]);
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// stream dependent data
	for (data_index=0;data_index<60;data_index++)
    sprintf(CSV_BUF+strlen(CSV_BUF),"Stream Oil Adjust %d,,%d,float,1,RW,1,%015.7f\n",data_index,63647+2*data_index,STREAM_OIL_ADJUST[data_index]);
	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// write
	fr = f_puts(CSV_BUF,&csvWriteObject);
	if (fr == EOF)
	{
		errorUsb(fr);
		return;
	}

	fr = f_sync(&csvWriteObject);
	if (fr != FR_OK)
	{
		errorUsb(fr);
		return;
	}

	for (i=0;i<1000;i++);
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// close file
	fr = f_close(&csvWriteObject);
	if (fr != FR_OK)
	{
		errorUsb(fr);
		return;
	}

	/// set global var true
    isCsvDownloadSuccess = TRUE;
    
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
    return;
}


void scanCsvFiles(void)
{
	isScanCsvFiles = FALSE;

	FRESULT fr;
	int i;
    static DIR dir;
    static FILINFO fno;
	const char path[] = "0:";
	csvCounter = 0;
	CSV_FILES[0] = '\0';

	/// disable all interrupts
	Swi_disable();
	usb_osalDelayMs(10);

	/// opendir
	if (f_opendir(&dir, path) != FR_OK) 
	{
		Swi_enable();
		return;
	}
	usb_osalDelayMs(10);

	/// read file names
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) {} // directory
		else // file
		{ 
			if (strstr(fno.fname, ".csv") != NULL) 
			{
				strcat(CSV_FILES,fno.fname);
				csvCounter++;
				isScanSuccess = TRUE;
			}
        }
		
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
    }

	usb_osalDelayMs(10);

	/// close dir
	f_closedir(&dir);
	usb_osalDelayMs(10);

	/// enable all interrupts back
	Swi_enable();
	usb_osalDelayMs(10);

    return;
}


void uploadCsv(void)
{
	isUploadCsv = FALSE;

    FRESULT fr;
	FIL fil;
	int i,id;
	char line[1024] = {0};
	char csvFileName[50] = {0};

	/// get file name
	sprintf(csvFileName,"0:%s.csv",CSV_FILES);
	fr = f_open(&fil, csvFileName, FA_READ);
	for (i=0;i<1000;i++);
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
	if (fr != FR_OK) return;

	disableAllClocksAndTimers();

	Swi_disable();

	/// read line
    while (f_gets(line, sizeof(line), &fil)) 
	{
		int i = 0; 
        char * splitValue[17];
        double value[17];

		/// remove trailing \n
		line[strcspn( line,"\n")] = '\0';

		/// split line
        char* ptr = strtok(line, ",");
        while (ptr != NULL)
        {   
            splitValue[i] = ptr;
			if (i==1) id = atoi(ptr);
            else value[i] = atof(ptr);
            ptr = strtok(NULL, ",");

            i++; 
        } 

        /// update registers 
        if (id==219) // <------- MODEL_CODE
        {
            char model_code[MAX_LCD_WIDTH];
            char buf[MAX_LCD_WIDTH+1];
            int* model_code_int;
            sprintf(buf,splitValue[6]); //default model code
			memcpy(model_code,buf,MAX_LCD_WIDTH);
            model_code_int = (int*)model_code;
            for (i=0;i<4;i++) REG_MODEL_CODE[i] = model_code_int[i];
			TimerWatchdogReactivate(CSL_TMR_1_REGS);
        }
		else if ((id>0) && (id<1000)) updateVars(id,value[6]);
		else if (id==60001) REG_TEMP_OIL_NUM_CURVES = value[6]; 
		else if (id==60003) for (i=0;i<10;i++) REG_TEMPS_OIL[i] = value[i+6];
        else if ((id>60022) && (id<60096)) for (i=0;i<4;i++) REG_COEFFS_TEMP_OIL[(id-60023)/8][i] = value[i+6];
        else if ((id>63646) && (id<63766)) STREAM_OIL_ADJUST[(id-63647)/2] = value[6];

        /// reset line[]
		line[0] = '\0';
		for (i=0;i<1000;i++);
	    TimerWatchdogReactivate(CSL_TMR_1_REGS);
	}	
	
	/// close file
	f_close(&fil);
	for (i=0;i<1000;i++);
    TimerWatchdogReactivate(CSL_TMR_1_REGS);
	Swi_enable();

	/// update FACTORY DEFAULT
   	storeUserDataToFactoryDefault();
	for (i=0;i<1000;i++);
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

	/// print status -- we use print as an intended "delay"
	displayLcd("   RESTARTING   ",LCD1);

	/// force to expire watchdog timer
    while(1); 
}


void usbhMscDriveOpen(void)
{
	int i;
    usb_host_params.usbMode      = USB_HOST_MSC_MODE;
    usb_host_params.instanceNo   = USB_INSTANCE;
    usb_handle = USB_open(usb_host_params.instanceNo, &usb_host_params);

    // failed to open
    if (usb_handle == 0) return;

    // Setup the INT Controller
	usbHostIntrConfig (&usb_host_params);

    // Initialize the file system.
    FATFS_init();

    // Open an instance of the mass storage class driver.
	Swi_disable();

	g_ulMSCInstance = USBHMSCDriveOpen(usb_host_params.instanceNo, 0, MSCCallback);
	
	for (i=0;i<5;i++)
	{
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
		usb_osalDelayMs(1000);
	}

	Swi_enable();
}


void enumerateUsb(void)
{
	int i = 0;
	char dummy[] = "100";
	isUsbMounted = FALSE;
	
    stopClocks();

	while(i<10) 
	{
		snprintf(dummy,2,"%d",USB_RTC_SEC);
        if (USBHCDMain(USB_INSTANCE, g_ulMSCInstance) == 0)
		{
			snprintf(dummy,2,"%d",USB_RTC_SEC);
        	if(g_eState == STATE_DEVICE_ENUM)
        	{
				snprintf(dummy,2,"%d",USB_RTC_SEC);
            	if (USBHMSCDriveReady(g_ulMSCInstance) != 0) usb_osalDelayMs(500);
            	if (!g_fsHasOpened && (FATFS_open(0U, NULL, &fatfsHandle) == FR_OK)) g_fsHasOpened = 1;
				isUsbMounted = TRUE;
				break;
        	}	
		}

		i++;
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
		snprintf(dummy,2,"%d",USB_RTC_SEC);
    }

    startClocks();
}
