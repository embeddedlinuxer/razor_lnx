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
* PDI_I2C.h
*-------------------------------------------------------------------------
* These two files contain all the source code for generic functions of I2C 
* communication, as well as many specialized functions for reading/writing 
* to particular I2C peripherals. ADCs are read and controlled via I2C, 
* and are used to acquire temperature and reflected power readings from the 
* oscillator board. I2C is also used to control the Analog Output value as 
* well as the pin expanders that operate the LCD and the infrared LEDs. 
* Note: very rarely an I2C peripheral will hang, i.e. not release the data 
* line. (That infernal ribbon cable and various connectors are likely to 
* blame.) I2C_Recover() fixes this by sending pulses on SCL until the 
* peripheral lets go of SDA.
* The I2C uses hardware interrupt 6 (I2C_Hwi) to quickly post a SWI and then exit.
*------------------------------------------------------------------------*/
#ifndef PDI_I2C_H_
#define PDI_I2C_H_

/*============================================================================*/
/*                             MACRO DEFINITIONS                              */
/*============================================================================*/

/// This wasn't included in cslr_gpio.h for some reason
#define CSL_GPIO_BINTEN_EN8_MASK         (0x00000100u)
#define CSL_GPIO_BINTEN_EN8_SHIFT        (0x00000008u)
#define CSL_GPIO_BINTEN_EN8_DISABLE      (0x00000000u)
#define CSL_GPIO_BINTEN_EN8_ENABLE       (0x00000001u)

#define CSL_GPIO_BINTEN_EN3_MASK         (0x00000008u)
#define CSL_GPIO_BINTEN_EN3_SHIFT        (0x00000003u)
#define CSL_GPIO_BINTEN_EN3_RESETVAL     (0x00000000u)
#define CSL_GPIO_BINTEN_EN3_DISABLE      (0x00000000u)
#define CSL_GPIO_BINTEN_EN3_ENABLE       (0x00000001u)

#define CSL_GPIO_BINTEN_EN6_MASK         (0x00000040u)
#define CSL_GPIO_BINTEN_EN6_SHIFT        (0x00000006u)
#define CSL_GPIO_BINTEN_EN6_RESETVAL     (0x00000000u)
#define CSL_GPIO_BINTEN_EN6_DISABLE      (0x00000000u)
#define CSL_GPIO_BINTEN_EN6_ENABLE       (0x00000001u)

#define CSL_I2C_ICSTR_BB_MASK (0x00001000u)
#define CSL_I2C_ICSTR_BB_SHIFT (0x0000000Cu)
#define CSL_I2C_ICSTR_BB_RESETVAL (0x00000000u)
#define CSL_I2C_ICSTR_BB_FREE (0x00000000u)
#define CSL_I2C_ICSTR_BB_BUSY (0x00000001u)
#define CSL_I2C_ICSTR_BB_CLEAR (0x00000001u)

#define CSL_I2C_ICMDR_IRS_MASK (0x00000020u)
#define CSL_I2C_ICMDR_IRS_SHIFT (0x00000005u)
#define CSL_I2C_ICMDR_IRS_RESETVAL (0x00000000u)
#define CSL_I2C_ICMDR_IRS_DISABLE (0x00000000u)
#define CSL_I2C_ICMDR_IRS_ENABLE (0x00000001u)

#define CSL_I2C_ICMDR_STT_MASK (0x00002000u)
#define CSL_I2C_ICMDR_STT_SHIFT (0x0000000Du)
#define CSL_I2C_ICMDR_STT_RESETVAL (0x00000000u)
#define CSL_I2C_ICMDR_STT_CLEAR (0x00000000u)
#define CSL_I2C_ICMDR_STT_SET (0x00000001u)

#define CSL_I2C_ICMDR_STP_MASK (0x00000800u)
#define CSL_I2C_ICMDR_STP_SHIFT (0x0000000Bu)
#define CSL_I2C_ICMDR_STP_RESETVAL (0x00000000u)
#define CSL_I2C_ICMDR_STP_CLEAR (0x00000000u)
#define CSL_I2C_ICMDR_STP_SET (0x00000001u)

#define CSL_I2C_ICMDR_MST_MASK (0x00000400u)
#define CSL_I2C_ICMDR_MST_SHIFT (0x0000000Au)
#define CSL_I2C_ICMDR_MST_RESETVAL (0x00000000u)
#define CSL_I2C_ICMDR_MST_SLAVE_MODE     (0x00000000u)
#define CSL_I2C_ICMDR_MST_MASTER_MODE    (0x00000001u)

#define CSL_I2C_ICMDR_TRX_MASK (0x00000200u)
#define CSL_I2C_ICMDR_TRX_SHIFT (0x00000009u)
#define CSL_I2C_ICMDR_TRX_RESETVAL (0x00000000u)
#define CSL_I2C_ICMDR_TRX_RX_MODE (0x00000000u)
#define CSL_I2C_ICMDR_TRX_TX_MODE (0x00000001u)

#define CSL_I2C_ICMDR_RM_MASK (0x00000080u)
#define CSL_I2C_ICMDR_RM_SHIFT (0x00000007u)
#define CSL_I2C_ICMDR_RM_RESETVAL (0x00000000u)
#define CSL_I2C_ICMDR_RM_DISABLE (0x00000000u)
#define CSL_I2C_ICMDR_RM_ENABLE (0x00000001u)

#define CSL_I2C_ICIVR_INTCODE_MASK (0x00000007u)
#define CSL_I2C_ICIVR_INTCODE_SHIFT (0x00000000u)
#define CSL_I2C_ICIVR_INTCODE_RESETVAL (0x00000000u)
#define CSL_I2C_ICIVR_INTCODE_NONE (0x00000000u)
#define CSL_I2C_ICIVR_INTCODE_AL (0x00000001u)
#define CSL_I2C_ICIVR_INTCODE_NACK (0x00000002u)
#define CSL_I2C_ICIVR_INTCODE_ARDY (0x00000003u)
#define CSL_I2C_ICIVR_INTCODE_ICRRDY (0x00000004u)
#define CSL_I2C_ICIVR_INTCODE_ICXRDY (0x00000005u)
#define CSL_I2C_ICIVR_INTCODE_SCD (0x00000006u)
#define CSL_I2C_ICIVR_INTCODE_AAS (0x00000007u)

#define I2C_START_SET		CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_STT,SET)
#define I2C_STOP_SET		CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_STP,SET)
#define I2C_START_CLR		CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_STT,CLEAR)
#define I2C_STOP_CLR		CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_STP,CLEAR)
#define I2C_RX_MODE			CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_TRX,RX_MODE)
#define I2C_TX_MODE			CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_TRX,TX_MODE)
#define I2C_RM_ON			CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_RM,ENABLE)
#define I2C_RM_OFF			CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_RM,DISABLE)
#define I2C_STBMODE_ON		CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_STB,ENABLE) 	//start byte mode enable
#define I2C_STBMODE_OFF		CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_STB,DISABLE)	//start byte mode disable
#define I2C_MASTER_MODE	    CSL_FINST(i2cRegs->ICMDR,I2C_ICMDR_MST,MASTER_MODE)	// put I2C module in Master mode
#define I2C_CNT_0BYTE		i2cRegs->ICCNT = CSL_FMK(I2C_ICCNT_ICDC,0x0) 	//Data count register = 0
#define I2C_CNT_1BYTE		i2cRegs->ICCNT = CSL_FMK(I2C_ICCNT_ICDC,0x1) 	//Data count register = 1
#define I2C_CNT_2BYTE		i2cRegs->ICCNT = CSL_FMK(I2C_ICCNT_ICDC,0x2) 	//Data count register = 2
#define I2C_CNT_3BYTE		i2cRegs->ICCNT = CSL_FMK(I2C_ICCNT_ICDC,0x3) 	//Data count register = 3
#define I2C_CNT_4BYTE		i2cRegs->ICCNT = CSL_FMK(I2C_ICCNT_ICDC,0x4) 	//Data count register = 4
#define I2C_CNT_6BYTE		i2cRegs->ICCNT = CSL_FMK(I2C_ICCNT_ICDC,0x6) 	// TESTING PURPOSES

#define LCD_FUNC_SET			(0x38)
#define LCD_DISP_ON				(0x0E)
#define LCD_DISP_CLR			(0x01)
#define LCD_ENTRY_MODE			(0x06)
#define LCD_SET_DDRAM_ADDR		(0x80)

#define I2C_INT_GENERATED_FALSE (0x00)
#define I2C_INT_GENERATED_TRUE  (0x01)
#define I2C_SLAVE_ADDR_WRITE	(0x40)
#define I2C_SLAVE_ADDR_READ		(0x41)
#define I2C_SLAVE_ADDR_XPANDR	(0x20)	// LCD expander
#define I2C_SLAVE_ADDR_MBVE		(0x21)	// MBVE expander
#define I2C_SLAVE_ADDR_DAC		(0x4C)	// DAC (AO) address
#define I2C_SLAVE_ADDR_ADC		(0x48)	// ADC address - Temperature & RP
#define I2C_SLAVE_ADDR_ADC2		(0x4A)	// ADC address - Density
#define I2C_SLAVE_ADDR_DS1340	(0x68)	// DS1340 address - RTC 
#define I2C_CTRL_BYTE_WL		(0x10)	//DAC control byte for a write+load operation
#define NO_DATA                 (0xFF)
#define I2C_BUTTON_S		    (0x1)
#define I2C_BUTTON_B		    (0x4)
#define I2C_BUTTON_V		    (0x8)
#define I2C_BUTTON_E		    (0x2)
#define I2C_BUTTON_NONE	        (0x0)
#define I2C_INIT_NUM_CHARS      (6)

inline void DisableButtonInts(void);
inline void EnableButtonInts(void);
int	LCD_setaddr(int column, int line);
int LCD_printch(char c, int column, int line);
void I2C_HWI_ISR(void);
void Init_I2C(void);
void Reset_I2C(Uint8 isKey, Uint32 I2C_KEY);
int  I2C_Recover(void);
void Init_LCD(void);
void Init_MBVE(void);
void I2C_SendByte(Uint8 out_byte);
void displayLcd(const char c[], int line);
void LCD_setcursor(int curs_on, int curs_blink);
void LCD_setBlinking(int column, int line);

static inline int I2C_Wait_To_Send(void);
static inline int I2C_Wait_To_Send_ARDY(void);
static inline int I2C_Wait_To_Receive(void);
static inline int I2C_Wait_For_Start(void);
static inline int I2C_Wait_For_Stop(void);
static inline int I2C_Wait_For_Ack(void);
static inline int errorCounter(Uint8 i2c_slave, Uint32 I2C_KEY);
#endif /* PDI_I2C_H_ */
