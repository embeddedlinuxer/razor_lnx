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
* Errors.c 
*-------------------------------------------------------------------------*/

#define ERRORS_H
#include "Globals.h"

void Update_Relays(void)
{
#ifndef HAPTIC_RELAY 
	// CHECK RELAY CONDITION
	switch (REG_RELAY_MODE) 
	{
		case 0	: // Watercut
   			(REG_WATERCUT.calc_val > REG_RELAY_SETPOINT.calc_val) ? (COIL_RELAY[0].val = TRUE) : (COIL_RELAY[0].val = FALSE);
			break;
		case 1	: // Phase
   		 	if (COIL_ACT_RELAY_OIL.val)
       			(COIL_OIL_PHASE.val) ? (COIL_RELAY[0].val = TRUE) : (COIL_RELAY[0].val = FALSE);
         	else 
            	(COIL_OIL_PHASE.val) ? (COIL_RELAY[0].val = FALSE) : (COIL_RELAY[0].val = TRUE);
			break;
		case 2	:  // Error
			(DIAGNOSTICS > 0) ? (COIL_RELAY[0].val = TRUE) : (COIL_RELAY[0].val = FALSE);
			break;
		case 3	: // Manual
       		(COIL_RELAY_MANUAL.val) ? (COIL_RELAY[0].val = TRUE) : (COIL_RELAY[0].val = FALSE);
			break;
		default	:
       		COIL_RELAY[0].val = FALSE;
			break;
	}
    
	// UPDATE RELAY PIN 
    if (COIL_RELAY[0].val)
    {    
    	delayTimer++;
        if ((delayTimer > REG_RELAY_DELAY) || (COIL_RELAY_MANUAL.val))
        {    
            gpioRegs->BANK_REGISTERS[1].OUT_DATA |= 1 << 5; // Relay ON
            delayTimer = 0; 
        }    
   	}    
    else 
	{
        gpioRegs->BANK_REGISTERS[1].OUT_DATA &= ~(1 << 5); // Relay OFF
		delayTimer = 0;
	}	

#endif
}


void checkError(double val, double BOUND_LOW, double BOUND_HIGH, int ERR_LOW, int ERR_HIGH)
{
    if (val < BOUND_LOW) 
	{
		if ((DIAGNOSTICS & ERR_LOW) == 0) DIAGNOSTICS |= ERR_LOW;
		if (DIAGNOSTICS & ERR_HIGH) DIAGNOSTICS &= ~ERR_HIGH;
	}
    else if (val > BOUND_HIGH) 
	{
		if ((DIAGNOSTICS & ERR_HIGH) == 0) DIAGNOSTICS |= ERR_HIGH;
		if (DIAGNOSTICS & ERR_LOW) DIAGNOSTICS &= ~ERR_LOW;
	}
    else 
	{
		if (DIAGNOSTICS & ERR_HIGH) DIAGNOSTICS &= ~ERR_HIGH;
		if (DIAGNOSTICS & ERR_LOW) DIAGNOSTICS &= ~ERR_LOW;
	}

	return;
}
