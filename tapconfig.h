/*
 * tapconfig.h
 *
 * Definitions for the TAP (Technology Access Platform).
 *
 */

#ifndef TAPCONFIG_H_
#define TAPCONFIG_H_


#ifdef __cplusplus
extern "C"
{
#endif

//
// The latest hardware has a different pin assigned to one of the columns.
//
// The latest hardware can be distinguished by two push buttons, one for LRN mode and one for the BSL.
// Previous versions have only one button or none.
//

#define	LATEST_HW		/* Enable for the latest LED mapping */

#ifdef __cplusplus
}
#endif
#endif /* TAP_H_ */
