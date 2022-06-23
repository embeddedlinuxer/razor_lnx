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
* nandwriter.c
*-------------------------------------------------------------------------
* This code is essentially TI's NANDWriter project integrated into our 
* firmware and adapted to run inside the TI-RTOS kernel. The code looks a bit 
* arcane because it was originally written (by TI) to work with a slew of 
* different processors and NAND chips. I've noticed some strange behavior in 
* the write-verification routine, NAND_verifyPage(), that will sometimes 
* return in a false positive result for data corruption. I mean REALLY weird 
* behavior... like the boolean expression (0xFF == 0xFF) returning FALSE. 
* Currently the fail-status of data verification is ignored until someone can 
* poke around in the disassembly (joy!) to figure out what is going on there.
*-------------------------------------------------------------------------
* HISTORY:
*       ?-?-?       : David Skew : Created
*       Jul-18-2018 : Daniel Koh : Migraged to linux platform
*------------------------------------------------------------------------*/
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "device.h"
#include "debug.h"
#include "nandwriter.h"
#include "nand.h"
#include "device_nand.h"
#include "Globals.h"
#include "util.h"
#include <ti/fs/fatfs/ff.h>

/************************************************************
* FIRMWARE AND VARIABLE START BLOCK ADDRESS
************************************************************/
#define PDI_RAZOR_FIRMWARE 		"0:pdi_razor_firmware.ais"
#define ACCESS_DELAY			1000
#define APP_START_BLK 			1
#define VAR_START_BLK 			50

/************************************************************
* Local Macro Declarations                                  *
************************************************************/

#define NANDWIDTH_16
#define MAX_BLK_NUM     		220
#define SIZE_CFG        		52244
#define FBASE           		0x62000000
#define NANDStart       		0x62000000

/************************************************************
* Global Variable Definitions for page buffers              *
************************************************************/

extern VUint32 __FAR__ DDRStart;
static Uint8* gNandTx;
static Uint8* gNandRx;

/************************************************************
* Function Declarations                                     *
************************************************************/

static Uint32 LOCAL_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt);
static Uint32 USB_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt);
extern void UTIL_setCurrMemPtr(void *value);

/************************************************************
* Function Definitions                                		*
************************************************************/

static Uint8 DEVICE_ASYNC_MEM_IsNandReadyPin(ASYNC_MEM_InfoHandle hAsyncMemInfo)
{
    return ((Uint8) ((AEMIF->NANDFSR & DEVICE_EMIF_NANDFSR_READY_MASK)>>DEVICE_EMIF_NANDFSR_READY_SHIFT));
}


void writeNand(void)
{
	Swi_disable();
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
	Store_Vars_in_NAND();
	Swi_enable();
}

/****************************************************************************************
 * Store_Vars_in_NAND() writes all variables in the "CFG" data section into NAND flash	*
 ****************************************************************************************/
void Store_Vars_in_NAND(void)
{
    Uint32 num_pages;
    NAND_InfoHandle  hNandInfo;
    Uint8 *heapPtr, *cfgPtr;
    Int32 data_size = 0, alloc_size = 0, i = 0;

    UTIL_setCurrMemPtr(0);

    // Initialize NAND Flash
    hNandInfo = NAND_open((Uint32)NANDStart, BUS_16BIT );
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

    if (hNandInfo == NULL) return;
    data_size = SIZE_CFG;
    num_pages = 0;
    while ((num_pages * hNandInfo->dataBytesPerPage) < data_size) 
	{
		num_pages++;
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
	}

    // we want to allocate an even number of pages.
    alloc_size = num_pages * hNandInfo->dataBytesPerPage;

    // setup pointer in RAM
    hNandInfo = NAND_open((Uint32)NANDStart, BUS_16BIT );
    heapPtr = (Uint8 *) UTIL_allocMem(alloc_size);
    cfgPtr = (Uint8*) ADDR_DDR_CFG;

    // copy data to heap
    for (i=0; i<alloc_size; i++) 
	{
		heapPtr[i]=cfgPtr[i];
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
	}

    // Write the file data to the NAND flash
    if (LOCAL_writeData(hNandInfo, heapPtr, num_pages) != E_PASS) return;
}


/****************************************************************************************
 * Restore_Vars_From_NAND() reads all data values stored in NAND memory and				*
 * writes them to the "CFG" data section in RAM											*
 ****************************************************************************************/
Uint32 Restore_Vars_From_NAND(void)
{
    Uint8 *dataPtr;
    Int32 data_size = 0;
    NAND_InfoHandle hNandInfo;
    Uint32 numBlks, num_pages, i, blockNum, pageNum, pageCnt;

    hNandInfo = NAND_open((Uint32)NANDStart, BUS_16BIT );

    data_size = SIZE_CFG;
    num_pages = 0;

    while ((num_pages * hNandInfo->dataBytesPerPage)  < data_size)  num_pages++;

    gNandTx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);
    gNandRx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);

    for (i=0; i<NAND_MAX_PAGE_SIZE; i++)
    {
        gNandTx[i]=0xff;
        gNandRx[i]=0xff;
    }

    // Get total number of blocks needed
    numBlks = 0;
    while ((numBlks*hNandInfo->pagesPerBlock) < num_pages) numBlks++;

    // Start in block 50
    // Leave blocks 0-49 alone, reserved for the boot image + bad blocks
    blockNum = VAR_START_BLK;

    while (blockNum < hNandInfo->numBlocks)
    {
        // Find first good block
        if (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS) return E_FAIL;

        // Start writing in page 0 of current block
        pageNum = 0;
        pageCnt = 0;

        // Setup data pointer
        dataPtr = (Uint8*)ADDR_DDR_CFG;

        // Start page read loop
        do
        {
            UTIL_waitLoop(200);

            if (NAND_readPage(hNandInfo, blockNum, pageNum, gNandRx) != E_PASS) return E_FAIL;

            for (i=0;i<hNandInfo->dataBytesPerPage;i++)
                dataPtr[i+pageNum*hNandInfo->dataBytesPerPage] = gNandRx[i];

            pageNum++;
            pageCnt++;

            if (pageNum == hNandInfo->pagesPerBlock)
            {
                // A block transition needs to take place; go to next good block
                do
                {
                    blockNum++;
                    if (blockNum > MAX_BLK_NUM) return E_FAIL;  //exceeded the "max" addressable block
                }
                while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS);

                pageNum = 0;
            }
        } while (pageCnt < num_pages);

        break;
    }
    return E_PASS;
}


// Generic function to write a UBL or Application header and the associated data
static Uint32 LOCAL_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt)
{
	Uint32 blockNum,pageNum,pageCnt;
  	Uint32 numBlks;
  	Uint32 i;
  	Uint8  *dataPtr;

  	gNandTx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);
  	gNandRx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);

  	for (i=0; i<NAND_MAX_PAGE_SIZE; i++)
  	{
    	gNandTx[i]=0xff;
    	gNandRx[i]=0xff;
  	}

  	// Get total number of blocks needed
  	numBlks = 0;
  	while ((numBlks*hNandInfo->pagesPerBlock) < totalPageCnt) 
	{
		numBlks++;
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
	}

	// Start in block 50 : Leave blocks 0-49 alone -- reserved for the boot image + potential bad blocks
	blockNum = 50;

  	// Unprotect all blocks of the device
  	if (NAND_unProtectBlocks(hNandInfo, blockNum, (hNandInfo->numBlocks-1)) != E_PASS)
  	{
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
    	blockNum++;
    	return E_FAIL;
  	}

  	while (blockNum < hNandInfo->numBlocks)
  	{
		TimerWatchdogReactivate(CSL_TMR_1_REGS);

    	// Find first good block
    	while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS) 
		{
			blockNum++;
			TimerWatchdogReactivate(CSL_TMR_1_REGS);
		}

    	// Erase the current block
    	NAND_eraseBlocks(hNandInfo,blockNum,1);

    	// Start writing in page 0 of current block
    	pageNum = 0;
    	pageCnt = 0;

   		// Setup data pointer
    	dataPtr = srcBuf;

    	// Start page writing loop
    	do
    	{
			TimerWatchdogReactivate(CSL_TMR_1_REGS);

      		// Write the AIS image data to the NAND device
      		if (NAND_writePage(hNandInfo, blockNum,  pageNum, dataPtr) != E_PASS)
      		{
        		NAND_reset(hNandInfo);
        		NAND_badBlockMark(hNandInfo,blockNum);
        		dataPtr -= pageNum * hNandInfo->dataBytesPerPage;
        		blockNum++;
        		continue;
      		}

      		UTIL_waitLoop(400);
			TimerWatchdogReactivate(CSL_TMR_1_REGS);

      		// Verify the page just written
      		//if (NAND_verifyPage(hNandInfo, blockNum, pageNum, dataPtr, gNandRx) != E_PASS) {}

      		pageNum++;
      		pageCnt++;
      		dataPtr +=  hNandInfo->dataBytesPerPage;

      		if (pageNum == hNandInfo->pagesPerBlock)
      		{
        		// A block transition needs to take place; go to next good block
        		do
        		{
					TimerWatchdogReactivate(CSL_TMR_1_REGS);
          			blockNum++;
        		}
        		while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS);

        		// Erase the current block
        		NAND_eraseBlocks(hNandInfo,blockNum,1);

        		pageNum = 0;
      		}
    	} while (pageCnt < totalPageCnt);

		TimerWatchdogReactivate(CSL_TMR_1_REGS);
    	NAND_protectBlocks(hNandInfo);
    	break;
  	}
  	return E_PASS;
}


static Uint32 USB_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt)
{
	Uint32    blockNum,pageNum,pageCnt;
  	Uint32    numBlks;
  	Uint32    i;
  	Uint8     *dataPtr;

  	gNandTx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);
  	gNandRx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);

  	for (i=0; i<NAND_MAX_PAGE_SIZE; i++)
  	{

    	gNandTx[i]=0xff;
    	gNandRx[i]=0xff;
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
  	}

  	// Get total number of blocks needed
  	numBlks = 0;
  	while ( (numBlks*hNandInfo->pagesPerBlock) < totalPageCnt ) numBlks++;
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

  	// Start in block 1 (leave block 0 alone)
  	blockNum = 1;

  	// Unprotect all blocks of the device
  	while (NAND_unProtectBlocks(hNandInfo, blockNum, (hNandInfo->numBlocks-1)) != E_PASS) blockNum++;
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

   	// Find first good block
   	while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS) blockNum++;
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

   	// Erase the current block
   	NAND_eraseBlocks(hNandInfo,blockNum,1);

   	// Start writing in page 0 of current block
   	pageNum = 0;
   	pageCnt = 0;

   	// Setup data pointer
   	dataPtr = srcBuf;

   	// Start page writing loop
   	do
   	{
		TimerWatchdogReactivate(CSL_TMR_1_REGS);

   		/// write AIS image data to the NAND device
   		if (NAND_writePage(hNandInfo, blockNum,  pageNum, dataPtr) != E_PASS)
   		{
			TimerWatchdogReactivate(CSL_TMR_1_REGS);
       		NAND_reset(hNandInfo);
       		NAND_badBlockMark(hNandInfo,blockNum);
       		dataPtr -=  pageNum * hNandInfo->dataBytesPerPage;
       		blockNum++;
       		continue;
   		}

   		for(i=0;i<ACCESS_DELAY;i++);
		TimerWatchdogReactivate(CSL_TMR_1_REGS);

   		pageNum++;
   		pageCnt++;
   		dataPtr +=  hNandInfo->dataBytesPerPage;

		if (pageNum == hNandInfo->pagesPerBlock)
   		{
			TimerWatchdogReactivate(CSL_TMR_1_REGS);

       		/// A block transition needs to take place; go to next good block
       		blockNum++;
       		while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS) blockNum++;
			TimerWatchdogReactivate(CSL_TMR_1_REGS);
        		
       		/// Erase the current block
       		NAND_eraseBlocks(hNandInfo,blockNum,1);
       		pageNum = 0;

   			for(i=0;i<ACCESS_DELAY;i++);
   		}

		/// watchdog timer reactive
		TimerWatchdogReactivate(CSL_TMR_1_REGS);

   	} while (pageCnt < totalPageCnt);

	TimerWatchdogReactivate(CSL_TMR_1_REGS);
   	NAND_protectBlocks(hNandInfo);
  	return E_PASS;
}


void upgradeFirmware(void)
{
	NAND_InfoHandle hNandInfo;
	Uint32 numPagesAIS;
    Uint8 *aisPtr;
    Int32 i, aisAllocSize, aisFileSize = 0;
    VUint16 *addr_flash;
    ASYNC_MEM_InfoHandle dummy;
	FIL fPtr;

	BYTE buffer[4096];
	int index = 0;
	int loop = 0;
	UINT br = 0;

	/// open fw file
    if (f_open(&fPtr, PDI_RAZOR_FIRMWARE, FA_READ) != FR_OK) return;
	
    UTIL_setCurrMemPtr(0);

    /// reset command
    addr_flash = (VUint16 *)(FBASE + DEVICE_NAND_CLE_OFFSET);

	TimerWatchdogReactivate(CSL_TMR_1_REGS);
    for (i=0;i<ACCESS_DELAY;i++);

    *addr_flash = (VUint16)0xFF;

	for (i=0;i<1000;i++)
	{
		if (DEVICE_ASYNC_MEM_IsNandReadyPin(dummy)) break;
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
	}	
    
    /// write getinfo command
    addr_flash = (VUint16 *)(FBASE + DEVICE_NAND_ALE_OFFSET);
    *addr_flash = (VUint16)NAND_ONFIRDIDADD;

	TimerWatchdogReactivate(CSL_TMR_1_REGS);
    for (i=0;i<ACCESS_DELAY;i++);
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
    for (i=0;i<4;i++) addr_flash = (VUint16 *)(FBASE);

    /// Initialize NAND Flash
    hNandInfo = NAND_open((Uint32)NANDStart, DEVICE_BUSWIDTH_16BIT);
    if (hNandInfo == NULL) return;

    /// Read file size
    aisFileSize = f_size(&fPtr);

    /// get page size
    numPagesAIS = 0;
    while ( (numPagesAIS * hNandInfo->dataBytesPerPage)  < aisFileSize) numPagesAIS++;

    /// We want to allocate an even number of pages.
    aisAllocSize = numPagesAIS * hNandInfo->dataBytesPerPage;

    /// Setup pointer in RAM
    aisPtr = (Uint8 *) UTIL_allocMem(aisAllocSize);

    /// Clear memory
    for (i=0; i<aisAllocSize; i++) aisPtr[i]=0xFF;
	TimerWatchdogReactivate(CSL_TMR_1_REGS);

    /// Go to start of file
    if (f_lseek(&fPtr,0) != FR_OK) return;

	/// display clear
	LCD_setcursor(0,0);
	displayLcd("FIRMWARE UPGRADE",0);	
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
	for (i=0;i<ACCESS_DELAY;i++);

	/// stop all clocks and timers excpet some required clocks
	disableAllClocksAndTimers();

	/// read file	
	for (;;) {
		for (i=0;i<5;i++) buffer[0] = '\0';
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
     	if (f_read(&fPtr, buffer, sizeof(buffer), &br) != FR_OK) return;
		if (br == 0) break; /* error or eof */
		for (loop=0;loop<sizeof(buffer);loop++) 
		{
      		for (i=0;i<10;i++) aisPtr[index] = buffer[loop];
			TimerWatchdogReactivate(CSL_TMR_1_REGS);
	    	for (i=0;i<10;i++) sprintf(lcdLine1,"      %3d%%    ",index*100/aisAllocSize);
			TimerWatchdogReactivate(CSL_TMR_1_REGS);
			index++;
   		}

		/// watchdog timer reactive
   		sprintf(lcdLine1,"      %3d%%    ",index*100/aisAllocSize);
		displayLcd(lcdLine1,1);	
		TimerWatchdogReactivate(CSL_TMR_1_REGS);
   		for(i=0;i<ACCESS_DELAY;i++);
    }

    f_close(&fPtr);
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
	for(i=0;i<ACCESS_DELAY;i++);

	/* download existing csv */
    downloadCsv();
    TimerWatchdogReactivate(CSL_TMR_1_REGS);
    for(i=0;i<ACCESS_DELAY*100;i++);

	/* disable all interrupts while accessing flash memory */
	Swi_disable();

	/* global erase nand memory */
	if (NAND_globalErase(hNandInfo) != E_PASS) return;
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
   	for(i=0;i<ACCESS_DELAY*100;i++);

    /* Write the file data to the NAND flash */
    if (USB_writeData(hNandInfo, aisPtr, numPagesAIS) != E_PASS) return;
	TimerWatchdogReactivate(CSL_TMR_1_REGS);
   	for(i=0;i<ACCESS_DELAY*100;i++);

	/* force watchdog timer to expire*/
    while(1); 
}
