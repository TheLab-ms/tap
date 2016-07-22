/*
 * tapif.h
 *
 * Interface to the TAP (Technology Access Platform) firmware.
 *
 */

#ifndef TAPIF_H_
#define TAPIF_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define	USB_SUPPORT

void init_tap(void);
void scan_tap(void);
void receive_data_from_usb(unsigned char b);


#ifdef __cplusplus
}
#endif
#endif /* TAPIF_H_ */
