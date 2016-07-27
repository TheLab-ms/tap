/*
 * tap.c
 *
 * Core firmware for the TAP (Technology Access Platform).
 *
 */

#include <msp430.h>
#include "tap.h"

#include <string.h>

//***********  Standard firmware  ********************************************
//
// The standard firmware lights all of the red then green then blue and
// finally all LEDs upon power up, then scrolls our name.
//

const unsigned char our_name[] = "TheLab.ms";

#define	BOOT_FRAMES	60		/* Number of frames to hold each boot color */
#define	TEXT_FRAMES	4		/* Number of frames to hold text scroll step */

//***********  Globals  ******************************************************
extern const unsigned char	font_ascii[][8];

burgermaster	Marinara;
burgermaster	Burgermaster_shadow;

unsigned char	display_buffer[8 * 8 * 3];
unsigned char	*display_addr = display_buffer;

unsigned int	loops;
unsigned int	next_color;

volatile unsigned int	LRN_mode_triggered;

//***********  Communication channels  ***************************************

#define	BUFFER_SIZE	256		/* power of 2 for best performance */

// Channels to assemble packets from the input sources
typedef struct channel
{
	unsigned int	next_in;
	unsigned int	next_out;
	unsigned char	buffer[BUFFER_SIZE];

} channel;

channel	serial_x;
channel	serial_y;

#ifdef PHOTO_SUPPORT
channel	photo;
#endif

#ifdef USB_SUPPORT
channel	usb;
#endif	// USB_SUPPORT

channel	x_out;
channel	y_out;

//***********  Text scrolling  ***********************************************

// This buffer is used to create a temporary text fragment for the standard
// firmware or for a special text demo
unsigned char	temp_text[BUFFER_SIZE];

unsigned char	bg_red;
unsigned char	bg_green;
unsigned char	bg_blue;
unsigned char	txt_red;
unsigned char	txt_green;
unsigned char	txt_blue;

text_fragment	*first_fragment;
text_fragment	*current_fragment;

unsigned int	offset;
unsigned int	text_hold;

unsigned char	I_am_not_text_master = 0;
unsigned int	text_sync_origin;
unsigned char	first_char;
const unsigned char	*current_char;
const unsigned char	*last_char;

//***********  Function prototypes  ******************************************

void create_text_fragment(
		unsigned char		*buffer,
		unsigned char		flags,
		unsigned char		red,
		unsigned char		green,
		unsigned char		blue,
		unsigned int		length,
		const unsigned char *s);
void create_split_text_fragment(
		unsigned char		*buffer,
		unsigned char		flags,
		unsigned char		red,
		unsigned char		green,
		unsigned char		blue,
		unsigned int		length1,
		const unsigned char *s1,
		unsigned int		length2,
		const unsigned char *s2);
void prepare_to_scroll_text(text_fragment *fragment);
void config_init(void);
void init_ioports(void);
void scan_LEDs(void);
void initU5(void);


/*
 ************ flush_channel **************************************************
 *
 * Empty a buffer
 *
 */
void flush_channel(channel *c)
{
	c->next_in = 0;
	c->next_out = 0;
}

/*
 ************ put_byte *******************************************************
 *
 * Insert a byte into the buffer
 *
 */
int put_byte(channel *c, unsigned char b)
{
	if ((c->next_in + 1) % BUFFER_SIZE == c->next_out)
		// Buffer is full
		return 0;

	c->buffer[c->next_in] = b;
	c->next_in = (c->next_in + 1) % BUFFER_SIZE;
	return 1;
}

/*
 ************ bytes_in *******************************************************
 *
 * Return the number of bytes in the buffer
 *
 */
unsigned int bytes_in(channel *c)
{
	if (c->next_in >= c->next_out)
		// Number is difference between pointers
		return c->next_in - c->next_out;
	else
		// Account for wrap around
		return c->next_in + BUFFER_SIZE - c->next_out;
}

/*
 ************ peek_byte ******************************************************
 *
 * Nondestructively retrieve a byte from the buffer
 *
 */
unsigned char peek_byte(channel *c, unsigned int index)
{
	return c->buffer[(c->next_out + index) % BUFFER_SIZE];
}

/*
 ************ remove_bytes ***************************************************
 *
 * Remove bytes from the buffer
 *
 */
void remove_bytes(channel *c, unsigned int count)
{
	if (bytes_in(c) >= count)
		c->next_out = (c->next_out + count) % BUFFER_SIZE;
	else
		// Cannot remove more than there is, just empty it
		flush_channel(c);
}

/*
 ************ contiguous_block_size ******************************************
 *
 * Return the size of the largest block at the head of the buffer
 *
 */
unsigned int contiguous_block_size(channel *c)
{
	if (c->next_in >= c->next_out)
		// Size is difference between pointers
		return c->next_in - c->next_out;
	else
		// Size is rest of the buffer
		return BUFFER_SIZE - c->next_out;
}

/*
 ************ packet_address *************************************************
 *
 * Return the address of the start of the first packet in the buffer
 *
 */
unsigned char * packet_address(channel *c)
{
	return &c->buffer[c->next_out];
}

/*
 ************ buffer_address *************************************************
 *
 * Return the address of the start of the buffer
 *
 */
unsigned char * buffer_address(channel *c)
{
	return c->buffer;
}

/*
 ************ packet_complete ************************************************
 *
 * Return whether an entire packet is in a channel
 *
 */
int packet_complete(channel *c)
{
	unsigned int	avail;
	unsigned int	size;

	avail = bytes_in(c);
	if (avail < 3)
		// Packet cannot be complete
		return 0;

	size = (peek_byte(c, 1) << 8) + peek_byte(c, 2);
	return avail >= size;
}

/*
 ************ factory_defaults ***********************************************
 *
 * Set state to factory defaults
 *
 */
void factory_defaults(burgermaster *state)
{
	state->tag[0] = 'T';
	state->tag[1] = 'A';
	state->tag[2] = 'P';
	state->flags = 0;
	state->x_address = 0;
	state->y_address = 0;
	state->data_address = (char *)0xFFFF;
	state->font_address = (char *)0xFFFF;
}

/*
 ************ save_state *****************************************************
 *
 * Save state to flash memory
 *
 */
void save_state(void)
{
	char	*flash_ptr;

	flash_ptr = (char *)FLASH_INFO_D;
	FCTL3 = FWKEY;			// Clear Lock bit
	FCTL1 = FWKEY+ERASE;	// Set Erase bit
	*flash_ptr = 0;			// Dummy write to erase Flash seg
	FCTL1 = FWKEY+WRT;		// Set WRT bit for write operation

	memcpy(flash_ptr, &Burgermaster_shadow, sizeof(burgermaster));

	FCTL1 = FWKEY;			// Clear WRT bit
	FCTL3 = FWKEY+LOCK;		// Set LOCK bit
}

/*
 ************ save_text ******************************************************
 *
 * Save text to flash memory
 *
 */
text_fragment * save_text(channel *c)
{
	char			*flash_ptr;
	text_fragment	*text_ptr;
	unsigned int	text_length;
	unsigned int	num_chars;

	text_length = (peek_byte(c, 1) << 8) + peek_byte(c, 2) - 9;
	if (text_length > 246)
		{
		// Cap the text length
		text_length = 246;
		if (peek_byte(c, 8) & TAP_TEXT_FONT_MASK)
			// Two-byte chars
			num_chars = 246 / 2;
		else
			num_chars = 246;
		}
	else
		if (peek_byte(c, 8) & TAP_TEXT_FONT_MASK)
			// Two-byte chars
			num_chars = text_length / 2;
		else
			num_chars = text_length;

	text_ptr = (text_fragment *)FLASH_INFO_C;

	flash_ptr = (char *)FLASH_INFO_C;
	FCTL3 = FWKEY;			// Clear Lock bit
	FCTL1 = FWKEY+ERASE;	// Set Erase bit
	*flash_ptr = 0;			// Dummy write to erase Flash seg

	if (num_chars > 128 - 9)
		{
		// Too big to fit in only info segment C
		flash_ptr = (char *)FLASH_INFO_B;
		FCTL3 = FWKEY;			// Clear Lock bit
		FCTL1 = FWKEY+ERASE;	// Set Erase bit
		*flash_ptr = 0;			// Dummy write to erase Flash seg
		}

	FCTL1 = FWKEY+WRT;		// Set WRT bit for write operation

	text_ptr->font_and_flags = peek_byte(c, 8) | NOT_DELETED;
	text_ptr->red_intensity = peek_byte(c, 5);
	text_ptr->green_intensity = peek_byte(c, 6);
	text_ptr->blue_intensity = peek_byte(c, 7);
	text_ptr->num_chars = num_chars;

	if (contiguous_block_size(c) >= text_length + 9)
		// The image is in one piece
		memcpy(text_ptr->text, packet_address(c) + 9,
				text_length);
	else if (contiguous_block_size(c) <= 9)
		// The packet is split but the text is not
		memcpy(text_ptr->text, buffer_address(c) + 9 -
				contiguous_block_size(c), text_length);
	else
		{
		// The image is split by buffer wrap around
		memcpy(text_ptr->text, packet_address(c) + 9,
				contiguous_block_size(c) - 9);
		memcpy(text_ptr->text + contiguous_block_size(c) - 9,
				buffer_address(c),
				text_length - (contiguous_block_size(c) - 9));
		}

	FCTL1 = FWKEY;			// Clear WRT bit
	FCTL3 = FWKEY+LOCK;		// Set LOCK bit

	return text_ptr;
}

/*
 ************ save_image *****************************************************
 *
 * Save image to flash memory
 *
 */
binary_image * save_image(channel *c)
{
	char			*flash_ptr;
	binary_image	*image_ptr;
	unsigned int	image_length;

	image_length = (peek_byte(c, 1) << 8) + peek_byte(c, 2) - 5;
	if (image_length > 192)
		// Cap the image length
		image_length = 192;

	image_ptr = (binary_image *)FLASH_INFO_C;

	flash_ptr = (char *)FLASH_INFO_C;
	FCTL3 = FWKEY;			// Clear Lock bit
	FCTL1 = FWKEY+ERASE;	// Set Erase bit
	*flash_ptr = 0;			// Dummy write to erase Flash seg

	flash_ptr = (char *)FLASH_INFO_B;
	FCTL3 = FWKEY;			// Clear Lock bit
	FCTL1 = FWKEY+ERASE;	// Set Erase bit
	*flash_ptr = 0;			// Dummy write to erase Flash seg

	FCTL1 = FWKEY+WRT;		// Set WRT bit for write operation

	image_ptr->flags = NOT_DELETED | IS_IMAGE;
	image_ptr->num = image_length;

	if (contiguous_block_size(c) >= image_length + 5)
		// The image is in one piece
		memcpy(image_ptr->intensities, packet_address(c) + 5,
				image_length);
	else if (contiguous_block_size(c) <= 5)
		// The packet is split but the image is not
		memcpy(image_ptr->intensities, buffer_address(c) + 5 -
				contiguous_block_size(c), image_length);
	else
		{
		// The image is split by buffer wrap around
		memcpy(image_ptr->intensities, packet_address(c) + 5,
				contiguous_block_size(c) - 5);
		memcpy(image_ptr->intensities + contiguous_block_size(c) - 5,
				buffer_address(c),
				image_length - (contiguous_block_size(c) - 5));
		}

	FCTL1 = FWKEY;			// Clear WRT bit
	FCTL3 = FWKEY+LOCK;		// Set LOCK bit

	return image_ptr;
}

/*
 ************ load_image *****************************************************
 *
 * Load a saved image
 *
 */
void load_image(unsigned char *display_buffer, binary_image	*image_ptr)
{
	memcpy(display_buffer, image_ptr->intensities, image_ptr->num);
}

/*
 ************ uart_putcX *****************************************************
 *
 * Send a byte to the next TAP along X axis using the serial port
 *
 * ch = the byte
 *
 */
void uart_putcX(const unsigned char ch)
{
	// Wait for the transmit buffer to be ready
	while (!(UCA1IFG & UCTXIFG));

	// Transmit data
	UCA1TXBUF = ch;
}

//*********** uart_putsX *****************************************************
void uart_putsX(const unsigned char *str)
{
	while (*str != '\0')
		uart_putcX(*str++);
}

/*
 ************ uart_putcY *****************************************************
 *
 * Send a byte to the next TAP along Y axis using the serial port
 *
 * ch = the byte
 *
 */
void uart_putcY(const unsigned char ch)
{
	/* Wait for the transmit buffer to be ready */
	while (!(UCA0IFG & UCTXIFG));

	/* Transmit data */
	UCA0TXBUF = ch;
}

//*********** uart_putsY *****************************************************
void uart_putsY(const unsigned char *str)
{
	while (*str != '\0')
		uart_putcY(*str++);
}

/*
 ************ set_id *********************************************************
 *
 * Set my ID
 *
 * x = X address
 * y = Y address
 * flags = flags to propagate, like temporary
 *
 */
void set_id(int x, int y, int flags)
{
	Marinara.flags |= ID_ASSIGNED;
	Marinara.x_address = x;
	Marinara.y_address = y;

	if ((flags & TAP_PACKET_FLAG_TEMPORARY) == 0)
		{
		// Save to flash
		Burgermaster_shadow.flags |= ID_ASSIGNED;
		Burgermaster_shadow.x_address = x;
		Burgermaster_shadow.y_address = y;
		save_state();
		}

	// Propagate downstream
	uart_putcX(TAP_PACKET_SET_ID |
			TAP_PACKET_FLAG_IGNOREADDRESS |
			TAP_PACKET_FLAG_NOFORWARD |
			flags & TAP_PACKET_FLAG_TEMPORARY);
	uart_putcX(0);
	uart_putcX(5);
	uart_putcX(x + 1);
	uart_putcX(y);

	uart_putcY(TAP_PACKET_SET_ID |
			TAP_PACKET_FLAG_IGNOREADDRESS |
			TAP_PACKET_FLAG_NOFORWARD |
			flags & TAP_PACKET_FLAG_TEMPORARY);
	uart_putcY(0);
	uart_putcY(5);
	uart_putcY(x);
	uart_putcY(y + 1);
}

/*
 ************ process_packet *************************************************
 *
 * Process a data packet
 *
 * c = the channel containing the packet
 *
 */
void process_packet(channel *c)
{
	unsigned int	image_length;
	unsigned int	packet_length;
	text_fragment	*text;

	packet_length = (peek_byte(c, 1) << 8) + peek_byte(c, 2);
	if (packet_length > BIGGEST_PACKET)
		{
		// No way for this to work out well, so just punt
		flush_channel(c);
		return;
		}

	if ((peek_byte(c, 0) & TAP_PACKET_FLAG_IGNOREADDRESS) ||
			(Marinara.flags & ID_ASSIGNED) == 0 ||
			(peek_byte(c, 3) == Marinara.x_address) &&
			(peek_byte(c, 4) == Marinara.y_address))
		// Broadcast mode or ID not assigned or ID matches
		switch (peek_byte(c, 0) & TAP_PACKET_TYPE_MASK)
			{
			case TAP_PACKET_FACTORY_RESET:

				factory_defaults(&Marinara);

				if ((peek_byte(c, 0) & TAP_PACKET_FLAG_TEMPORARY) == 0)
					{
					// Save to flash
					Burgermaster_shadow = Marinara;
					save_state();
					}

				// Initialize standard firmware versus demo
				config_init();

				break;

			case TAP_PACKET_SET_BRIGHT_MODE:

				Marinara.flags |= BRIGHT_MODE;

				if ((peek_byte(c, 0) & TAP_PACKET_FLAG_TEMPORARY) == 0)
					{
					// Save to flash
					Burgermaster_shadow.flags |= BRIGHT_MODE;
					save_state();
					}

				break;

			case TAP_PACKET_SET_LOW_POWER_MODE:

				Marinara.flags &= ~BRIGHT_MODE;

				if ((peek_byte(c, 0) & TAP_PACKET_FLAG_TEMPORARY) == 0)
					{
					// Save to flash
					Burgermaster_shadow.flags &= ~BRIGHT_MODE;
					save_state();
					}

				break;

			case TAP_PACKET_SET_ID:

				set_id(peek_byte(c, 3), peek_byte(c, 4), peek_byte(c, 0));

				break;

			case TAP_PACKET_TEXT_STRING:
				// Text string packet

				// Indicate downloaded text
				Marinara.flags = (Marinara.flags & ~DOWNLOADED_MASK) |
						DOWNLOADED_TEXT;

				if ((peek_byte(c, 0) & TAP_PACKET_FLAG_TEMPORARY) == 0)
					{
					// Save to flash
					Burgermaster_shadow.flags =
							(Burgermaster_shadow.flags & ~DOWNLOADED_MASK) |
								DOWNLOADED_TEXT;
					text = save_text(c);
					Burgermaster_shadow.data_address = (char *)text;
					save_state();
					}
				else
					{
					// Create a temporary text fragment in RAM
					if (contiguous_block_size(c) >= packet_length)
						// The packet is in one piece
						create_text_fragment(temp_text, peek_byte(c, 8),
								peek_byte(c, 5), peek_byte(c, 6),
								peek_byte(c, 7),
								packet_length - 9, packet_address(c) + 9);
					else if (contiguous_block_size(c) <= 9)
						// The packet is split but the string is not
						create_text_fragment(temp_text, peek_byte(c, 8),
								peek_byte(c, 5), peek_byte(c, 6),
								peek_byte(c, 7),
								packet_length - 9, buffer_address(c) + 9 -
									contiguous_block_size(c));
					else
						// The string is split by buffer wrap around
						create_split_text_fragment(temp_text, peek_byte(c, 8),
								peek_byte(c, 5), peek_byte(c, 6),
								peek_byte(c, 7),
								contiguous_block_size(c) - 9,
								packet_address(c) + 9,
								packet_length - contiguous_block_size(c),
								buffer_address(c));
					text = (text_fragment *)temp_text;
					}

				// Display starting with the first character
				prepare_to_scroll_text((text_fragment *)temp_text);

				break;

			case TAP_PACKET_TEXT_SYNC_ESTABLISH:

				I_am_not_text_master = 1;

				break;

			case TAP_PACKET_TEXT_SYNC_ORIGIN:

				text_sync_origin = peek_byte(c, 5);	//!!! % (txt_length + 1);

				// Tell the next guy his origin
				uart_putcX(TAP_PACKET_TEXT_SYNC_ORIGIN | TAP_PACKET_FLAG_NOFORWARD);
				uart_putcX(0);
				uart_putcX(6);
				uart_putcX(0);
				uart_putcX(0);
				uart_putcX(text_sync_origin + 1);

				break;

			case TAP_PACKET_TEXT_SYNC_MARK:

				// Tell the next guy to follow suit
				uart_putcX(TAP_PACKET_TEXT_SYNC_MARK | TAP_PACKET_FLAG_NOFORWARD);
				uart_putcX(0);
				uart_putcX(5);
				uart_putcX(0);
				uart_putcX(0);

				// Resume scrolling with the character at the origin
				offset = 0;

				// Need to do modulus here!!!
//				if (text_sync_origin == txt_length)
//					{
//					current_char = first_fragment->text;
//					first_char = 1;
//					}
//				else
					{
					current_char = &first_fragment->text[text_sync_origin];
					first_char = 0;
					}

				break;

			case TAP_PACKET_BINARY_IMAGE:
				// Copy bitmap
				image_length = packet_length - 5;
				if (image_length > 192)
					// Cap the image length
					image_length = 192;

				if (contiguous_block_size(c) >= image_length + 5)
					// The image is in one piece
					memcpy(display_buffer, packet_address(c) + 5,
							image_length);
				else if (contiguous_block_size(c) <= 5)
					// The packet is split but the image is not
					memcpy(display_buffer, buffer_address(c) + 5 -
							contiguous_block_size(c), image_length);
				else
					{
					// The image is split by buffer wrap around
					memcpy(display_buffer, packet_address(c) + 5,
							contiguous_block_size(c) - 5);
					memcpy(display_buffer + contiguous_block_size(c) - 5,
							buffer_address(c),
							image_length - (contiguous_block_size(c) - 5));
					}

				// Indicate downloaded image
				Marinara.flags = (Marinara.flags & ~DOWNLOADED_MASK) |
						DOWNLOADED_IMAGE;

				if ((peek_byte(c, 0) & TAP_PACKET_FLAG_TEMPORARY) == 0)
					{
					// Save to flash
					Burgermaster_shadow.flags =
							(Burgermaster_shadow.flags & ~DOWNLOADED_MASK) |
								DOWNLOADED_IMAGE;
					Burgermaster_shadow.data_address = (char *)save_image(c);
					save_state();
					}

				break;
			}

	// Reset for the next packet
	remove_bytes(c, packet_length);
}

/*
 ************ process_packets ************************************************
 *
 * Process any packets which may be ready
 *
 */
void process_packets(void)
{
	unsigned int	Done;

	if (LRN_mode_triggered)
		{
		LRN_mode_triggered = 0;

		// Set my ID to (0, 0)
		set_id(0, 0, 0);
		}

	Done = 0;

	while (!Done)
		{
		Done = 1;

#ifdef USB_SUPPORT
		if (packet_complete(&usb))
			{
			process_packet(&usb);
			Done = 0;
			}
#endif	// USB_SUPPORT

		if (packet_complete(&serial_x))
			{
			process_packet(&serial_x);
			Done = 0;
			}

		if (packet_complete(&serial_y))
			{
			process_packet(&serial_y);
			Done = 0;
			}

#ifdef PHOTO_SUPPORT
		if (packet_complete(&photo))
			{
			process_packet(&photo);
			Done = 0;
			}
#endif	// PHOTO_SUPPORT
		}
}

/*
 ************ process_packets_for_awhile *************************************
 *
 * Process any packets which may arrive for awhile
 *
 */
void process_packets_for_awhile(unsigned int awhile)
{
	while (awhile--)
		process_packets();
}

/*
 ************ fillRect *******************************************************
 *
 * Fill a rectangle in the display buffer with the specified color
 *
 * buffer = the address of the display buffer
 * x = the left edge of the rectangle
 * y = the top edge of the rectangle
 * width = the width of the rectangle
 * height = the height of the rectangle
 * red = the intensity of the red part of the color
 * green = the intensity of the green part of the color
 * blue = the intensity of the blue part of the color
 *
 */
void fillRect(unsigned char *buffer,
			  int x,
			  int y,
			  int width,
			  int height,
			  unsigned char red,
			  unsigned char green,
			  unsigned char blue)
{
	int	i;
	int	j;

	// Enforce sanity
	if (x < 0 || x > 7 || y < 0 || y > 7 || width < 0 || height < 0)
		return;

	// Constrain size
	if (x + width - 1 > 7)
		width = 7 - x + 1;
	if (y + height - 1 > 7)
		height = 7 - y + 1;

	// Go to the top edge of the rectangle
	buffer = buffer + y * 8 * 3;
	for (i = 0; i < height; i++)
		{
		// Go to the left edge of the rectangle
		buffer = buffer + x * 3;
		for (j = 0; j < width; j++)
			{
			*buffer++ = red;
			*buffer++ = green;
			*buffer++ = blue;
			}

		// Go to the next row
		buffer = buffer + (8 - width) * 3;
		}
}

/*
 ************ bltChar *******************************************************
 *
 * Block transfer a character glyph to the display buffer
 *
 * buffer = the address of the display buffer
 * glyph = the bits for the character
 * origin = origin column of the character
 * clear_background = 0 if background color is rendered
 *
 * Globals
 * txt_red = foreground red intensity
 * txt_green = foreground green intensity
 * txt_blue = foreground blue intensity
 * bg_red = background red intensity
 * bg_green = background green intensity
 * bg_blue = background blue intensity
 *
 */
void bltChar(unsigned char *buffer, const unsigned char *glyph, int origin,
		int clear_background)
{
	unsigned int	row;
	unsigned int	col;
	unsigned int	columns;
	unsigned char	glyph_row;

	if (origin < -7 || origin > 7)
		return;

	columns = 8;

	for (row = 0; row < 8; row++)
		{
		glyph_row = *glyph++;

		if (origin < 0)
			{
			// Glyph is offset to the left, lose that many columns
			glyph_row = glyph_row >> abs(origin);

			columns = 8 + origin;
			}
		if (origin > 0)
			{
			// Glyph is offset to the right, skip that many columns
			buffer = buffer + 3 * origin;
			columns = 8 - origin;
			}

		for (col = 0; col < columns; col++)
			{
			if (glyph_row & 1)
				{
				// Pixel is foreground color
				*buffer++ = txt_red;
				*buffer++ = txt_green;
				*buffer++ = txt_blue;
				}
			else
				if (clear_background == 0)
					{
					// Pixel is background color
					*buffer++ = bg_red;
					*buffer++ = bg_green;
					*buffer++ = bg_blue;
					}
				else
					// Advance past clear pixels
					buffer = buffer + 3;

			glyph_row = glyph_row >> 1;
			}

		if (origin < 0)
			// Glyph is offset to the left, leave that many columns on the right
			buffer = buffer + 3 * abs(origin);
		}
}

/*
 ************ create_text_fragment *******************************************
 *
 * Create a text fragment
 *
 * buffer = the buffer to contain the fragment
 * flags = flags and font for the fragment
 * red = red intensity
 * green = green intensity
 * blue = blue intensity
 * length = the length of the text string
 * s = the text string to put into the fragment
 *
 */
void create_text_fragment(
		unsigned char		*buffer,
		unsigned char		flags,
		unsigned char		red,
		unsigned char		green,
		unsigned char		blue,
		unsigned int		length,
		const unsigned char *s)
{
	text_fragment	*txt;

	txt = (text_fragment *)buffer;

	// Copy text string into the fragment
	memcpy(txt->text, s, length);
	txt->num_chars = length;

	txt->font_and_flags = flags;
	txt->red_intensity = red;
	txt->green_intensity = green;
	txt->blue_intensity = blue;
	txt->next_address = (text_fragment *)0xFFFF;
}

/*
 ************ create_split_text_fragment *************************************
 *
 * Create a text fragment from two strings
 *
 * buffer = the buffer to contain the fragment
 * flags = flags and font for the fragment
 * red = red intensity
 * green = green intensity
 * blue = blue intensity
 * length1 = the length of the first text string
 * s1 = the first text string to put into the fragment
 * length2 = the length of the second text string
 * s2 = the second text string to put into the fragment
 *
 */
void create_split_text_fragment(
		unsigned char		*buffer,
		unsigned char		flags,
		unsigned char		red,
		unsigned char		green,
		unsigned char		blue,
		unsigned int		length1,
		const unsigned char *s1,
		unsigned int		length2,
		const unsigned char *s2)
{
	text_fragment	*txt;

	txt = (text_fragment *)buffer;

	// Copy text strings into the fragment
	memcpy(txt->text, s1, length1);
	memcpy(txt->text + length1, s2, length2);
	txt->num_chars = length1 + length2;

	txt->font_and_flags = flags;
	txt->red_intensity = red;
	txt->green_intensity = green;
	txt->blue_intensity = blue;
	txt->next_address = (text_fragment *)0xFFFF;
}

/*
 ************ find_glyph *****************************************************
 *
 * Find the glyph representation for a character
 *
 * ch = the character
 *
 */
const unsigned char * find_glyph(unsigned int ch)
{
	return &font_ascii[ch][0];
}

/*
 ************ find_next_text_fragment ****************************************
 *
 * Find next non-empty text fragment
 *
 * fragment = the first text fragment of a list to try
 *
 * returns the address of the non-empty fragment or 0xFFFF if none
 *
 */
text_fragment * find_next_text_fragment(text_fragment *fragment)
{
	while (fragment != (text_fragment *)0xFFFF && fragment->num_chars == 0)
		fragment = fragment->next_address;

	return fragment;
}

/*
 ************ prepare_to_scroll_text *****************************************
 *
 * Set up for scrolling text
 *
 * fragment = the first text fragment to scroll
 *
 */
void prepare_to_scroll_text(text_fragment *fragment)
{
	fragment = find_next_text_fragment(fragment);
	if (fragment == (text_fragment *)0xFFFF)
		{
		// No text, display a single blinking red '?'
		create_text_fragment(temp_text, TAP_TEXT_FLAG_NOSCROLL,
				FULL_INTENSITY, 0, 0,
				1, "?");
		fragment = (text_fragment *)temp_text;
		}

	first_fragment = fragment;
	current_fragment = fragment;

	current_char = fragment->text;
	last_char = current_char + fragment->num_chars - 1;

	txt_red = fragment->red_intensity;
	txt_green = fragment->green_intensity;
	txt_blue = fragment->blue_intensity;

	if (first_fragment->font_and_flags & DISPLAY_WHOLE_CHARS)
		// Display the message whole characters at a time
		text_hold = TEXT_FRAMES * 8;
	else
		// Scroll the text
		text_hold = TEXT_FRAMES;

	// Fake a leading space to scroll the first character in
	first_char = 1;

	offset = 0;
	loops = 0;
}

/*
 ************ start_scrolling_text *******************************************
 *
 * Start scrolling text
 *
 * fragment = the first text fragment to scroll
 *
 */
void start_scrolling_text(text_fragment *fragment)
{
	prepare_to_scroll_text(fragment);

	if (!I_am_not_text_master)
		{
		// Hut 1...hut 2...
		uart_putcX(TAP_PACKET_TEXT_SYNC_MARK | TAP_PACKET_FLAG_NOFORWARD);
		uart_putcX(0);
		uart_putcX(5);
		uart_putcX(0);
		uart_putcX(0);

		// Nobody is there to ask me to do this
		first_char = 0;
		}

	// Wait a little for everybody to get situated
	process_packets_for_awhile(1000);
}

/*
 ************ scroll_text ****************************************************
 *
 * Perform one step of scrolling text
 *
 */
void scroll_text(void)
{
	unsigned char	ch;
	int				color_changed;
	text_fragment	*next;

	// Clear video buffer
	fillRect(display_addr, 0, 0, 8, 8, 0, 0, 0);
	color_changed = 0;

	if (first_char)
		// Fake a leading space to scroll first char onto
		ch = ' ';
	else
		ch = *current_char;

	bltChar(display_addr, find_glyph(ch), -offset, 0);
	if (offset > 0)
		{
		// Do part of the next character, if there is one
		if (first_char)
			ch = *current_char;
		else
			if (current_char < last_char)
				// Use next character in the same fragment
				ch = *(current_char + 1);
			else
				{
				next = find_next_text_fragment(current_fragment->next_address);
				if (next != (text_fragment *)0xFFFF)
					{
					// Use first character of the next fragment
					ch = next->text[0];

					txt_red = next->red_intensity;
					txt_green = next->green_intensity;
					txt_blue = next->blue_intensity;
					color_changed = 1;
					}
				else
					// Scroll in the dummy space for a repeat
					ch = ' ';
				}
		bltChar(display_addr, find_glyph(ch), 8 - offset, 0);
		if (color_changed)
			{
			txt_red = current_fragment->red_intensity;
			txt_green = current_fragment->green_intensity;
			txt_blue = current_fragment->blue_intensity;
			color_changed = 0;
			}
		}

	if (offset < 7 &&
			!(first_fragment->font_and_flags & DISPLAY_WHOLE_CHARS))
		offset++;
	else
		{
		// Go to next character
		offset = 0;
		if (first_char)
			{
			first_char = 0;

			if (!I_am_not_text_master)
				{
				// Periodically resync everybody
				uart_putcX(TAP_PACKET_TEXT_SYNC_MARK | TAP_PACKET_FLAG_NOFORWARD);
				uart_putcX(0);
				uart_putcX(5);
				uart_putcX(0);
				uart_putcX(0);
				}
			}
		else
			if (current_char < last_char)
				// Go to the next character in this fragment
				current_char++;
			else
				{
				next = find_next_text_fragment(current_fragment->next_address);
				if (next != (text_fragment *)0xFFFF)
					// Go to the first character of the next fragment
					current_fragment = next;
				else
					{
					// No more fragments, start over with the first fragment
					first_char = 1;
					current_fragment = first_fragment;
					}

				txt_red = current_fragment->red_intensity;
				txt_green = current_fragment->green_intensity;
				txt_blue = current_fragment->blue_intensity;
				current_char = current_fragment->text;
				last_char = current_char + current_fragment->num_chars - 1;
				}
		}
}

//*********** For demo *******************************************************
void setup(void);
void loop(void);

#include "demo.h"

/*
 ************ receive_data_from_usb ******************************************
 *
 * Receive a byte from the USB port
 *
 */
void receive_data_from_usb(unsigned char b)
{
	put_byte(&usb, b);
}

/*
 ************ Port_1 *********************************************************
 *
 * Port 1 interrupt service routine
 *
 */
#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void)
{
	if (P1IFG & BIT1)
		{
		// Clear interrupt
		P1IFG &= ~BIT1;

		// Trigger LRN mode
		LRN_mode_triggered = 1;
		}
}

/*
 ************ USCI_A0_ISR ****************************************************
 *
 * Y UART interrupt service routine
 *
 */
#pragma vector=USCI_A0_VECTOR
__interrupt void USCI_A0_ISR(void)
{
	switch(__even_in_range(UCA0IV,4))
		{
		case 0:							// Vector 0 - no interrupt
			break;
		case 2:							// Vector 2 - RXIFG
			put_byte(&serial_y, UCA0RXBUF);
			break;
		case 4:							// Vector 4 - TXIFG
			break;
		default:
			break;
		}
}

/*
 ************ USCI_A1_ISR ****************************************************
 *
 * X UART interrupt service routine
 *
 */
#pragma vector=USCI_A1_VECTOR
__interrupt void USCI_A1_ISR(void)
{
	switch(__even_in_range(UCA1IV,4))
		{
		case 0:							// Vector 0 - no interrupt
			break;
		case 2:							// Vector 2 - RXIFG
			put_byte(&serial_x, UCA1RXBUF);
			break;
		case 4:							// Vector 4 - TXIFG
			break;
		default:
			break;
		}
}

/*
 ************ config_init ****************************************************
 *
 * Initialize standard firmware versus demo
 *
 */
void config_init(void)
{
	if ((Marinara.flags & DOWNLOADED_MASK) == DOWNLOADED_IMAGE)
		{
		// Clear the display
		fillRect(display_buffer, 0, 0, 8, 8, 0, 0, 0);

		// Load saved image
		load_image(display_buffer, (binary_image *)Marinara.data_address);

		// Skip boot up color cycling display test
		next_color = 5;
		}
	else if ((Marinara.flags & DOWNLOADED_MASK) == DOWNLOADED_TEXT)
		{
		// Set up to scroll  the text
		start_scrolling_text((text_fragment *)Marinara.data_address);

		// Skip boot up color cycling display test
		next_color = 5;
		}
	else
		{
#ifdef DEMO

		// Clear the display
		fillRect(display_buffer, 0, 0, 8, 8, 0, 0, 0);

		// Initialize the demo
		setup();

		// Skip boot up color cycling display test
		next_color = 5;

#else

		if ((Marinara.flags & DOWNLOADED_MASK) == DOWNLOADED_NONE)
			{
			// Nothing downloaded, scroll our name after booting
			create_text_fragment(temp_text, 0, 0, FULL_INTENSITY, 0,
					strlen((char *)our_name), our_name);
			}

		next_color = 0;

#endif
		}
}

//*********** init_tap *******************************************************
void init_tap(void)
{
	burgermaster	*flashSave;
	text_fragment	*txt;

	init_ioports();
	initU5();						// All 8 outputs high

	P3SEL = BIT3+BIT4;				// P3.3,4 = USCI_A0 TXD/RXD
	UCA0CTL1 |= UCSWRST;			// **Put state machine in reset**
	UCA0CTL1 |= UCSSEL_2;			// SMCLK
	UCA0BR0 = 220;					// 12MHz 115200 (see User's Guide)
	UCA0BR1 = 0;					// 12MHz 115200
	UCA0MCTL = UCBRS_0;				// Modln UCBRSx=0, UCBRFx=0,
									// over sampling
	UCA0CTL1 &= ~UCSWRST;			// **Initialize USCI state machine**
	UCA0IE |= UCRXIE;				// Enable USCI_A0 RX interrupt

	P4SEL = BIT4+BIT5;				// P4.4,5 = USCI_A1 TXD/RXD
	UCA1CTL1 |= UCSWRST;			// Reset USCI 1
	UCA1CTL1 |= UCSSEL_2;			// SMCLK
	UCA1BR0 = 220;					// 115,200 baud rate
	UCA1BR1 = 0;
	UCA1MCTL = UCBRS0;
	UCA1CTL1 &= ~UCSWRST;			// Enable USCI 1
	UCA1IE |= UCRXIE;				// Enable USCI_A0 RX interrupt

	flashSave = (burgermaster *)FLASH_INFO_D;
	if (flashSave->tag[0] == 'T' && flashSave->tag[1] == 'A' &&
			flashSave->tag[2] == 'P')
		{
		// Retrieve state saved to flash memory
		Marinara = *flashSave;
		}
	else
		// Default to low-power mode, ID not assigned, nothing downloaded
		factory_defaults(&Marinara);
	Burgermaster_shadow = Marinara;

	// Mark temporary text fragment as invalid
	txt = (text_fragment *)temp_text;
	txt->next_address = 0;

	// Initialize input and output channels
	flush_channel(&serial_x);
	flush_channel(&serial_y);

	#ifdef PHOTO_SUPPORT
	flush_channel(&photo);
	#endif

	#ifdef USB_SUPPORT
	flush_channel(&usb);
	#endif	// USB_SUPPORT

	flush_channel(&x_out);
	flush_channel(&y_out);

	// Initialize standard firmware versus demo
	config_init();

	// Enable interrupt on port 1 pin 1
	P1IE |= BIT1;					// P1.0 interrupt enabled
	P1IES |= BIT1;					// P1.0 Hi/lo edge
	P1IFG &= ~BIT1;					// P1.0 IFG cleared
	LRN_mode_triggered = 0;

	__bis_SR_register(GIE);			// Enable global interrupts

	if ((Marinara.flags & DOWNLOADED_MASK) == DOWNLOADED_TEXT ||
			(Marinara.flags & DOWNLOADED_MASK) == 0 &&
			  txt->next_address != 0)
		{
		// Prepare for synchronized text scrolling

		// Wait a little to ensure init_demo has finished for everybody
		__delay_cycles(12000);

		// Own the TAP to the right
		uart_putcX(TAP_PACKET_TEXT_SYNC_ESTABLISH | TAP_PACKET_FLAG_NOFORWARD);
		uart_putcX(0);
		uart_putcX(5);
		uart_putcX(0);
		uart_putcX(0);

		// Wait a little for the one to the left to do the same
		process_packets_for_awhile(1000);

		if (!I_am_not_text_master)
			{
			// I am the leftmost TAP and am in control, tell #2 his origin
			uart_putcX(TAP_PACKET_TEXT_SYNC_ORIGIN | TAP_PACKET_FLAG_NOFORWARD);
			uart_putcX(0);
			uart_putcX(6);
			uart_putcX(0);
			uart_putcX(0);
			uart_putcX(1);
			}
		}

	// Wait a little longer
	process_packets_for_awhile(1000);

	loops=0;
}

//*********** scan_tap *******************************************************
void scan_tap(void)
{
	if (next_color < 5)
		{
		if (loops > 0)
			loops--;
		else
			{
			loops = BOOT_FRAMES;

			next_color++;
			if (next_color < 5)
				// Set next color
				switch(next_color)
					{
					case 1:
						// Red
						fillRect(display_buffer, 0, 0, 8, 8,
								FULL_INTENSITY, 0, 0);
						break;
					case 2:
						// Green
						fillRect(display_buffer, 0, 0, 8, 8,
								0, FULL_INTENSITY, 0);
						break;
					case 3:
						// Blue
						fillRect(display_buffer, 0, 0, 8, 8,
								0, 0, FULL_INTENSITY);
						break;
					case 4:
						// White
						fillRect(display_buffer, 0, 0, 8, 8,
								FULL_INTENSITY, FULL_INTENSITY,
									FULL_INTENSITY);
						break;
					}
			else
				// Transition to scrolling text
				start_scrolling_text((text_fragment *)temp_text);
			}
		}
	else
		{
#ifdef DEMO

		process_packets();

		// Step the demo
		loop();
#endif

		if ((Marinara.flags & DOWNLOADED_MASK) == DOWNLOADED_TEXT ||
				(Marinara.flags & DOWNLOADED_MASK) == 0 &&
				  ((text_fragment *)temp_text)->next_address != 0)
			{
			// Scroll text
			if (loops > 0)
				loops--;
			else
				{
				// Scroll text one step
				loops = text_hold;

				scroll_text();
				}
			}
		}

	process_packets();

	// Scan through all LEDs once
	scan_LEDs();

#ifdef	FRAME_RATE_TEST
	P6OUT ^= BIT2;				// Toggle P6.2 test point
#endif
}

//*********** low_power_delay ************************************************
void low_power_delay(unsigned char x)
{
	while(x--)
		__delay_cycles(LOW_POWER_DELAY);
}


//*********** bright_delay ***************************************************
void bright_delay(unsigned char x)
{
	while(x--)
		__delay_cycles(BRIGHT_DELAY);
}


//*********** scan_LEDs ******************************************************
#define SCAN_COLUMN(TURN_COLUMN_ON,TURN_COLUMN_OFF) \
		{ \
		if (delay != 0) \
			{ \
			TURN_COLUMN_ON; \
			low_power_delay(delay); \
			if (delay >= MAX_DELAY) \
				{ \
				delay = *display_ptr++; \
				TURN_COLUMN_OFF; \
				} \
			else \
				{ \
				TURN_COLUMN_OFF; \
				low_power_delay(MAX_DELAY - delay); \
				delay = *display_ptr++; \
				} \
			} \
		else \
			{ \
			low_power_delay(MAX_DELAY); \
			delay = *display_ptr++; \
			} \
		}
#define	SCAN_RGB_COLUMN_LOW_POWER(TURN_RED_ON,TURN_RED_OFF,TURN_GREEN_ON,TURN_GREEN_OFF,TURN_BLUE_ON,TURN_BLUE_OFF) \
		{ \
		SCAN_COLUMN(TURN_RED_ON, TURN_RED_OFF); \
		SCAN_COLUMN(TURN_GREEN_ON, TURN_GREEN_OFF); \
		SCAN_COLUMN(TURN_BLUE_ON, TURN_BLUE_OFF); \
		}
#define	SCAN_RGB_COLUMN_BRIGHT(TURN_RED_ON,TURN_RED_OFF,TURN_GREEN_ON,TURN_GREEN_OFF,TURN_BLUE_ON,TURN_BLUE_OFF) \
		{ \
		red_time = display_ptr[0]; \
		green_time = display_ptr[1]; \
		blue_time = display_ptr[2]; \
		if (red_time != 0) \
			TURN_RED_ON; \
		if (green_time != 0) \
			TURN_GREEN_ON; \
		if (blue_time != 0) \
			TURN_BLUE_ON; \
		display_ptr = display_ptr + 3; \
		dark_time = MAX_DELAY; \
		if (red_time <= green_time && red_time <= blue_time) \
			{ \
			/* Red is the shortest */ \
			if (red_time != 0) \
				{ \
				bright_delay(red_time); \
				dark_time = dark_time - red_time; \
				green_time = green_time - red_time; \
				blue_time = blue_time - red_time; \
				} \
			TURN_RED_OFF; \
			if (green_time <= blue_time) \
				{ \
				/* Green is the next shortest */ \
				if (green_time != 0) \
					{ \
					bright_delay(green_time); \
					dark_time = dark_time - green_time; \
					blue_time = blue_time - green_time; \
					} \
				TURN_GREEN_OFF; \
				if (blue_time != 0) \
					{ \
					/* And finally any blue remaining */ \
					bright_delay(blue_time); \
					dark_time = dark_time - blue_time; \
					} \
				TURN_BLUE_OFF; \
				} \
			else \
				{ \
				/* Blue is the next shortest */ \
				if (blue_time != 0) \
					{ \
					bright_delay(blue_time); \
					dark_time = dark_time - blue_time; \
					green_time = green_time - blue_time; \
					} \
				TURN_BLUE_OFF; \
				if (green_time != 0) \
					{ \
					/* And finally any green remaining */ \
					bright_delay(green_time); \
					dark_time = dark_time - green_time; \
					} \
				TURN_GREEN_OFF; \
				} \
			} \
		else \
			if (blue_time <= green_time) \
				{ \
				/* Blue is the shortest */ \
				if (blue_time != 0) \
					{ \
					bright_delay(blue_time); \
					dark_time = dark_time - blue_time; \
					red_time = red_time - blue_time; \
					green_time = green_time - blue_time; \
					} \
				TURN_BLUE_OFF; \
				if (red_time <= green_time) \
					{ \
					/* Red is the next shortest */ \
					if (red_time != 0) \
						{ \
						bright_delay(red_time); \
						dark_time = dark_time - red_time; \
						green_time = green_time - red_time; \
						} \
					TURN_RED_OFF; \
					if (green_time != 0) \
						{ \
						/* And finally any green remaining */ \
						bright_delay(green_time); \
						dark_time = dark_time - green_time; \
						} \
					TURN_GREEN_OFF; \
					} \
				else \
					{ \
					/* Green is the next shortest */ \
					if (green_time != 0) \
						{ \
						bright_delay(green_time); \
						dark_time = dark_time - green_time; \
						red_time = red_time - green_time; \
						} \
					TURN_GREEN_OFF; \
					if (red_time != 0) \
						{ \
						/* And finally any red remaining */ \
						bright_delay(red_time); \
						dark_time = dark_time - red_time; \
						} \
					TURN_RED_OFF; \
					} \
				} \
			else \
				{ \
				/* Green is the shortest */ \
				if (green_time != 0) \
					{ \
					bright_delay(green_time); \
					dark_time = dark_time - green_time; \
					red_time = red_time - green_time; \
					green_time = green_time - green_time; \
					} \
				TURN_GREEN_OFF; \
				if (red_time <= blue_time) \
					{ \
					/* Red is the next shortest */ \
					if (red_time != 0) \
						{ \
						bright_delay(red_time); \
						dark_time = dark_time - red_time; \
						blue_time = blue_time - red_time; \
						} \
					TURN_RED_OFF; \
					if (blue_time != 0) \
						{ \
						/* And finally any blue remaining */ \
						bright_delay(blue_time); \
						dark_time = dark_time - blue_time; \
						} \
					TURN_BLUE_OFF; \
					} \
				else \
					{ \
					/* Blue is the next shortest */ \
					if (blue_time != 0) \
						{ \
						bright_delay(blue_time); \
						dark_time = dark_time - blue_time; \
						red_time = red_time - blue_time; \
						} \
					TURN_BLUE_OFF; \
					if (red_time != 0) \
						{ \
						/* And finally any red remaining */ \
						bright_delay(red_time); \
						dark_time = dark_time - red_time; \
						} \
					TURN_RED_OFF; \
					} \
				} \
		/* Any time left over */ \
		if (dark_time != 0) \
			bright_delay(dark_time); \
		}

void scan_LEDs()
{
	unsigned char	*display_ptr;
	unsigned char	dark_time;
	unsigned char	red_time;
	unsigned char	green_time;
	unsigned char	blue_time;
	unsigned char	delay;
	unsigned int	x;

	// Latch 0 into U5 B input - this will turn on power to first row
	PJOUT &= ~BIT3;						// Make B input low
	// Clock U5
	PJOUT &= ~BIT0;
	PJOUT |= BIT0;

	// First row of LEDs now has power...

	PJOUT |= BIT3;//make B input on U5 high so the shift register clocks a single zero across to power each row

	display_ptr = display_addr;

	if ((Marinara.flags & BRIGHT_MODE) == 0)
		// Preload the first delay
		delay = *display_ptr++;

	// Pulse all columns with required delay times and then power the next row
	for ( x = 0; x < 8; x++)
		{
		if ((Marinara.flags & BRIGHT_MODE) != 0)
			{
			SCAN_RGB_COLUMN_BRIGHT(COL0R_ON, COL0R_OFF, COL0G_ON, COL0G_OFF, COL0B_ON, COL0B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL1R_ON, COL1R_OFF, COL1G_ON, COL1G_OFF, COL1B_ON, COL1B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL2R_ON, COL2R_OFF, COL2G_ON, COL2G_OFF, COL2B_ON, COL2B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL3R_ON, COL3R_OFF, COL3G_ON, COL3G_OFF, COL3B_ON, COL3B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL4R_ON, COL4R_OFF, COL4G_ON, COL4G_OFF, COL4B_ON, COL4B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL5R_ON, COL5R_OFF, COL5G_ON, COL5G_OFF, COL5B_ON, COL5B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL6R_ON, COL6R_OFF, COL6G_ON, COL6G_OFF, COL6B_ON, COL6B_OFF);

			SCAN_RGB_COLUMN_BRIGHT(COL7R_ON, COL7R_OFF, COL7G_ON, COL7G_OFF, COL7B_ON, COL7B_OFF);
			}
		else
			{
			SCAN_RGB_COLUMN_LOW_POWER(COL0R_ON, COL0R_OFF, COL0G_ON, COL0G_OFF, COL0B_ON, COL0B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL1R_ON, COL1R_OFF, COL1G_ON, COL1G_OFF, COL1B_ON, COL1B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL2R_ON, COL2R_OFF, COL2G_ON, COL2G_OFF, COL2B_ON, COL2B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL3R_ON, COL3R_OFF, COL3G_ON, COL3G_OFF, COL3B_ON, COL3B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL4R_ON, COL4R_OFF, COL4G_ON, COL4G_OFF, COL4B_ON, COL4B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL5R_ON, COL5R_OFF, COL5G_ON, COL5G_OFF, COL5B_ON, COL5B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL6R_ON, COL6R_OFF, COL6G_ON, COL6G_OFF, COL6B_ON, COL6B_OFF);

			SCAN_RGB_COLUMN_LOW_POWER(COL7R_ON, COL7R_OFF, COL7G_ON, COL7G_OFF, COL7B_ON, COL7B_OFF);
			}

		// Enable power to next row of LEDs
		// Clock U5 moving the zero across the shift register
		PJOUT &= ~BIT0;
		PJOUT |= BIT0;
		}
}

//*********** initU5 *********************************************************
void initU5()
{
	unsigned int	x;

	// On reset U5 to all outputs low - bad! - all LED rows powered!
	//PJOUT &= ~BIT2;					// Reset low
	//PJOUT |= BIT2;					// Reset high

	PJOUT |= BIT3;						// Make B input of U5 high

	// Clock all 1s into U5 to turn all ROWS off
	for(x = 0; x < 8; x++)
		{
		PJOUT &= ~BIT0;
		PJOUT |= BIT0;
		}
}

//*********** init_ioports ***************************************************
void init_ioports()
{
	P1DIR &= ~BIT1;

	PJDIR |= BIT3;
	PJOUT |= BIT2;
	PJDIR |= BIT2;
	PJDIR |= BIT0;

	P3DIR |= BIT1;
	P3OUT &= ~BIT1;
	P4DIR |= BIT1;
	P4OUT &= ~BIT1;
	P1DIR |= BIT6;
	P1OUT &= ~BIT6;

	P4DIR |= BIT0;
	P4OUT &= ~BIT0;
	P4DIR |= BIT2;
	P4OUT &= ~BIT2;
	P1DIR |= BIT7;
	P1OUT &= ~BIT7;

	P4DIR |= BIT3;
	P4OUT &= ~BIT3;
	P4DIR |= BIT7;
	P4OUT &= ~BIT7;
	P4DIR |= BIT6;
	P4OUT &= ~BIT6;

	P3DIR |= BIT0;
	P3OUT &= ~BIT0;
	P3DIR |= BIT2;
	P3OUT &= ~BIT2;
	P1DIR |= BIT5;
	P1OUT &= ~BIT5;

	P1DIR |= BIT4;
	P1OUT &= ~BIT4;
	P1DIR |= BIT2;
	P1OUT &= ~BIT2;
	P1DIR |= BIT3;
	P1OUT &= ~BIT3;

	P5DIR |= BIT0;
	P5OUT &= ~BIT0;
	P6DIR |= BIT7;
	P6OUT &= ~BIT7;
	P6DIR |= BIT6;
	P6OUT &= ~BIT6;

	P6DIR |= BIT0;
	P6OUT &= ~BIT0;
	P6DIR |= BIT5;
	P6OUT &= ~BIT5;
	P6DIR |= BIT4;
	P6OUT &= ~BIT4;

	P6DIR |= BIT1;
	P6OUT &= ~BIT1;
	P5DIR |= BIT1;
	P5OUT &= ~BIT1;
	P6DIR |= BIT3;
	P6OUT &= ~BIT3;

#ifdef	FRAME_RATE_TEST
	// Test point
	P6DIR |= BIT2;
#endif
}
