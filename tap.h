/*
 * tap.h
 *
 * Definitions for the TAP (Technology Access Platform).
 *
 */

#ifndef TAP_H_
#define TAP_H_


#include <tappacket.h>
#include <tapif.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define	FRAME_RATE_TEST		/* Define to toggle pin 6.2 for each frame */

#define	PHOTOCELL_SUPPORT	/* Define to include photocell input support */

//********  Downloaded Data Definitions  *************************************
typedef struct burgermaster
{
	char			tag[3];
	unsigned char	flags;
	unsigned char	x_address;
	unsigned char	y_address;
	char			*data_address;
	char			*font_address;

} burgermaster;

#define	BRIGHT_MODE			0x80
#define	ID_ASSIGNED			0x40

#define	DOWNLOADED_MASK		0x03	/* Mask for flags field */
#define	DOWNLOADED_NONE		0x00
#define	DOWNLOADED_TEXT		0x01
#define	DOWNLOADED_IMAGE	0x02

#define	FLASH_INFO_B		0x1900	/* Information memory segment B */
#define	FLASH_INFO_C		0x1880	/* Information memory segment C */
#define	FLASH_INFO_D		0x1800	/* Information memory segment D */

typedef struct text_fragment
{
	unsigned char			font_and_flags;
	unsigned char			red_intensity;
	unsigned char			green_intensity;
	unsigned char			blue_intensity;
	struct text_fragment	*next_address;
	unsigned char			num_chars;
	unsigned char			text[0];

} text_fragment;

#define	SUPPRESS_TEXT_SYNC		0x80
#define	DISPLAY_WHOLE_CHARS		0x40

#define	FONT_ID					0x01	/* Mask for flags field */

#define	NOT_DELETED				0x2
#define	IS_IMAGE				0x4

typedef struct binary_image
{
	unsigned char	flags;
	unsigned char	num;
	unsigned char	intensities[0];

} binary_image;

//*********** Set maximum LED ON time ****************************************
#define MAX_DELAY   255		/* Set too large - get flicker */
#define NUM_LEVELS  MAX_DELAY+1
							// Set too low - get more overhead and lose dynamic
                            //  range of intensity
                            // Powers of 2 are more efficient scaling from 0..FF
#define	LOW_POWER_DELAY	3	/* Cycles for individual LED ON delay */
#define	BRIGHT_DELAY	15	/* Adjust when MAX_DELAY is changed */

#define	FULL_INTENSITY	MAX_DELAY

//*********** For direct access to the display buffer ************************
#define	RED			0
#define	GREEN		1
#define	BLUE		2

#define	COL_SIZE	3
#define	COL0		0
#define	COL1		COL_SIZE
#define	COL2		(2*COL_SIZE)
#define	COL3		(3*COL_SIZE)
#define	COL4		(4*COL_SIZE)
#define	COL5		(5*COL_SIZE)
#define	COL6		(6*COL_SIZE)
#define	COL7		(7*COL_SIZE)

#define	ROW_SIZE	(8*COL_SIZE)
#define	ROW0		0
#define	ROW1		ROW_SIZE
#define	ROW2		(2*ROW_SIZE)
#define	ROW3		(3*ROW_SIZE)
#define	ROW4		(4*ROW_SIZE)
#define	ROW5		(5*ROW_SIZE)
#define	ROW6		(6*ROW_SIZE)
#define	ROW7		(7*ROW_SIZE)

//*********** Map columns to hardware ****************************************
#define	COL0R_ON	P3OUT |= BIT1
#define	COL0R_OFF	P3OUT &= ~BIT1

#define	COL0G_ON	P4OUT |= BIT1
#define	COL0G_OFF	P4OUT &= ~BIT1

#define	COL0B_ON	P1OUT |= BIT6
#define	COL0B_OFF	P1OUT &= ~BIT6

#define	COL1R_ON	P4OUT |= BIT0
#define	COL1R_OFF	P4OUT &= ~BIT0

#define	COL1G_ON	P4OUT |= BIT2
#define	COL1G_OFF	P4OUT &= ~BIT2

#define	COL1B_ON	P1OUT |= BIT7
#define	COL1B_OFF	P1OUT &= ~BIT7

#define	COL2R_ON	P4OUT |= BIT3
#define	COL2R_OFF	P4OUT &= ~BIT3

#define	COL2G_ON	P4OUT |= BIT7
#define	COL2G_OFF	P4OUT &= ~BIT7

#define	COL2B_ON	P4OUT |= BIT6
#define	COL2B_OFF	P4OUT &= ~BIT6

#define	COL3R_ON	P3OUT |= BIT0
#define	COL3R_OFF	P3OUT &= ~BIT0

#define	COL3G_ON	P3OUT |= BIT2
#define	COL3G_OFF	P3OUT &= ~BIT2

#define	COL3B_ON	P1OUT |= BIT5
#define	COL3B_OFF	P1OUT &= ~BIT5

#define	COL4R_ON	P1OUT |= BIT4
#define	COL4R_OFF	P1OUT &= ~BIT4

#define	COL4G_ON	P1OUT |= BIT2
#define	COL4G_OFF	P1OUT &= ~BIT2

#define	COL4B_ON	P1OUT |= BIT3
#define	COL4B_OFF	P1OUT &= ~BIT3

#define	COL5R_ON	P5OUT |= BIT0
#define	COL5R_OFF	P5OUT &= ~BIT0

#define	COL5G_ON	P6OUT |= BIT7
#define	COL5G_OFF	P6OUT &= ~BIT7

#define	COL5B_ON	P6OUT |= BIT6
#define	COL5B_OFF	P6OUT &= ~BIT6

#define	COL6R_ON	P6OUT |= BIT0
#define	COL6R_OFF	P6OUT &= ~BIT0

#define	COL6G_ON	P6OUT |= BIT5
#define	COL6G_OFF	P6OUT &= ~BIT5

#define	COL6B_ON	P6OUT |= BIT4
#define	COL6B_OFF	P6OUT &= ~BIT4

#define	COL7R_ON	P6OUT |= BIT1
#define	COL7R_OFF	P6OUT &= ~BIT1

#define	COL7G_ON	P5OUT |= BIT1
#define	COL7G_OFF	P5OUT &= ~BIT1

#define	COL7B_ON	P6OUT |= BIT3
#define	COL7B_OFF	P6OUT &= ~BIT3


#ifdef __cplusplus
}
#endif
#endif /* TAP_H_ */
