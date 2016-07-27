/*
 * tappacket.h
 *
 * Definitions for the TAP (Technology Access Platform) packets.
 *
 */

#ifndef TAPPACKET_H_
#define TAPPACKET_H_

#ifdef __cplusplus
extern "C"
{
#endif

//*********** Packet types ***************************************************
#define	TAP_PACKET_TYPE_MASK			0x1F

#define	TAP_PACKET_FACTORY_RESET		0x10
#define	TAP_PACKET_SET_BRIGHTNESS_MODE	0x11
#define	TAP_PACKET_USER_DEFINED			0x12
#define	TAP_PACKET_SET_ID				0x13
#define	TAP_PACKET_TEXT_STRING			0x14
#define	TAP_PACKET_TEXT_SYNC_ESTABLISH	0x15
#define	TAP_PACKET_TEXT_SYNC_ORIGIN		0x16
#define	TAP_PACKET_TEXT_SYNC_MARK		0x17
#define	TAP_PACKET_BINARY_IMAGE			0x18
#define	TAP_PACKET_CLEAR_DOWNLOAD		0x19

#define	TAP_PACKET_FLAG_IGNOREADDRESS	0x80
#define	TAP_PACKET_FLAG_NOFORWARD		0x40
#define	TAP_PACKET_FLAG_TEMPORARY		0x20

#define	TAP_TEXT_FLAG_NOSYNC			0x80
#define	TAP_TEXT_FLAG_NOSCROLL			0x40
#define	TAP_TEXT_FONT_MASK				0x01

#define	LONGEST_TEXT					246
#define	BIGGEST_PACKET					255


#ifdef __cplusplus
}
#endif
#endif /* TAPPACKET_H_ */
