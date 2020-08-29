/* DVLITES.C - Driver for Spare Time Gizmos Panda Display lights
*/

/*  Copyright 2005 Mark Crispin
**  All Rights Reserved
**
**  This file is part of the KLH10 Distribution.  Use, modification, and
**  re-distribution is permitted subject to the terms in the file
**  named "LICENSE", which contains the full text of the legal notices
**  and should always accompany this Distribution.
**
**  This program is provide "AS IS" with NO WARRANTY OF ANY KIND.
**
**  This notice (including the copyright and warranty disclaimer)
**  must be included in all copies or derivations of this software.
**
**
**  This file is derived from PandaDisplayTest, which is
**  Copyright 2004 by Spare Time Gizmos
**  and is used by permission.  
*/

#include <stdio.h>
#include <string.h>
#include "klh10.h"

#if !KLH10_DEV_LITES && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_LITES		/* Moby conditional for entire file */

#if HAVE_LIBUSB_1_0
#include <libusb.h>
#endif
/*
 * For now, assume that having <sys/io.h> is equivalent to having
 * outb(). That may not be exactly true but it's the code we have.
 */
#ifdef HAVE_SYS_IO_H
#include <sys/io.h>
#else
static inline int ioperm(int a, int b, int c) { return -1; }
static inline void outb(int a, int b) {}
#endif
#include "dvlites.h"


/* Internal function prototypes */

static void lites_wreg (unsigned char reg,unsigned char data);


#define DISABLE 0		/* convenient names */
#define ENABLE 1

/*
 * DESCRIPTION:
 *
 *   To a programmer, the Panda Display appears as a file of eight byte wide
 * registers.  The PC can write a register by sending the desired data to the
 * parallel port data bit, selecting the desired register address with the
 * INIT, AUTO LF, and SELECT IN printer status signals, and the clocking the
 * printer STROBE signal. The Panda Display registers are write only - there's
 * no way for the PC to ever read back from the display.  In this file, the
 * WriteRegister() method takes care of all the mechanics of writing a byte
 * to an individual register.
 *
 *   The first five registers, numbers 0..4, correspond to the forty data LEDs.
 * Register 0 is the left most block of eight LEDs, and register 5 the right
 * most.  The MSB of each byte is the left most LED of the block and the LSB
 * the right - pretty much what you'd expect.  This isn't rocket science!
 *
 *   Register 5 controls the four status LEDs.  Bit 7 (the MSB) controls LED1,
 * bit 6 LED2, bit 5 LED3 and finally, bit 4 controls LED4.  The lower four
 * bits of this register are currently unused.
 *
 *   Register 6 is the display enable register - writing a 1 to the LSB of this
 * register turns on the main forty data LEDs, and writing a 0 turns them off.
 * The powerup state of this register (nor any of the others, for that matter)
 * is not guaranteed, so the software should always initialize this register.
 * Currently only the LSB of register 6 is used - the remaining 7 bits are
 * ignored and should be zero.  The display enable flag affects ONLY the main
 * data LEDs - it has no effect on the four status LEDs.  Disabling the display
 * has no effect on the data currently stored in registers 0..4 nor does it
 * prevent the software from loading new data in these registers; it simply
 * turns off the LED drivers.  Finally, note that this register is NOT affected
 * by the unit selection (see register 7) - any writes to register 6 will
 * change _all_ displays in the system at the same time.
 *
 *   It's possible (if you're a really rabid blinking lights kinda person) to
 * have up to four Panda Displays daisy chained on the same parallel port.
 * Each display has a unique two bit address assigned to it by jumpers JP1/2,
 * and register 7 is used to select the active display unit.  The software
 * should write a two bit unit select code to bits 6 and 7 (the LSBs) of this
 * register and the display with the matching jumper settings will be selected.
 *
 *	JP2		JP1		Unit
 *	---------	---------	----
 *	removed		removed		0
 *	removed		installed	1
 *	installed	removed		2
 *	installed	installed	3
 *
 *   Only the selected Panda Display will respond to writing registers 0..5.
 * All unselected displays will ignore any writes to these registers. Registers
 * 6 and 7 are always enabled regardless of the unit selected and any writes to
 * these registers will affect all displays simultaneously. Finally, remember
 * that this register doesn't have any guaranteed state after power up, so the
 * software should always select a display as the first step.
 */

#define UNIT_PGM 0		/* unit 0: program data lights */
#define UNIX_xxx 1		/* unit 1: reserved for future assignment */
#define UNIX_yyy 2		/* unit 2: reserved for future assignment */
#define UNIX_zzz 3		/* unit 3: reserved for future assignment */
#define UNIT_MAX 3		/* maximum unit number */


/* Port address offsets */

#define PORT_DATA 0		/* offset of the data port from the base adr */
#define PORT_STATUS 1		/*   "     "  "  status  "   "   "    "   "  */
#define PORT_CONTROL 2		/*   "     "  "  control "   "   "    "   "  */
#define PORT_MAX 3		/* number of ports used by the lights */

/* Control port definitions */

/*
 *   To send data to the display, the PC should first send the data byte to
 * the DATA port and then generate pulse on the STROBE bit in the CONTROL
 * port.  The display latches all data from the PC on the _rising_ edge of
 * the STROBE L signal, however remember that the parallel port hardware
 * inverts this signal so that a rising edge is actually generated by the
 * 1 -> 0 transition of the STROBE bit in the CONTROL register.
 */

#define STROBE 0x01


/*
 *   The Panda display has three "address" inputs that are used to select
 * an internal register and/or a special function code.  These three address
 * inputs are driven by the parallel port's AUTO LF, INIT and SELECT outputs,
 * which are all controlled by three bits in the CONTROL register.  The AUTO
 * LF and SELECT bits are actually inverted by the PC parallel port hardware
 * (but INIT is not!), however the Panda board takes care of this for us and
 * and they all look "normal" to us...
 */

#define RS0 0x02		/* AUTO LF */
#define RS1 0x04		/* INPUT */
#define RS2 0x08		/* SELECT (IN) */


/*
 *   This table defines the CONTROL port bits corresponding to each of the
 * eight register addresses.  It's done in this rather round-about way so that
 * we don't have to depend on the register select bits being consecutive or
 * even in order!
 */

static const unsigned char control[8] = {
            0,          RS0,      RS1    ,      RS1|RS0,
  RS2        ,  RS2    |RS0,  RS2|RS1    ,  RS2|RS1|RS0
};


/* Mnemonics for the Panda Display register addresses... */

#define REG_DATA_MSB 0		/* left most 8 data LEDs */
#define REG_DATA_1 1		/* second from the left */
#define REG_DATA_2 2		/* center */
#define REG_DATA_3 3		/* second from the right */
#define REG_DATA_LSB 4		/* right most 8 data LEDs */
#define REG_STATUS 5		/* status LEDs */
#define REG_ENABLE 6		/* display enable register */
#define REG_SELECT 7		/* unit select register */
#define REG_MAX 7		/* maximum register */

/* Static data */

static int port = 0;		/* parallel port, normally LPT1 (0x378)  */
static int unit = -1;		/* currently selected unit */
static libusb_device_handle *lights_handle = NULL;
static unsigned int lights_left = 0;
static unsigned int lights_right = 0;
static int lights_aux = 0;


#define USB_CFG_VENDOR_ID       0xc0, 0x16
#define USB_CFG_DEVICE_ID       0xdf, 0x05
#define USB_CFG_DEVICE_NAME     'P','a','n','d','a',' ','D','i','s','p','l','a','y',
#define USB_CFG_DEVICE_NAME_LEN 13

static libusb_device_handle *get_panda_handle(libusb_device **devs)
{
    libusb_device *dev;
    libusb_device_handle *handle = NULL;
    int i = 0;
    int r;

    int found = 0;
    int openable = 0;

    unsigned char prod[256];
    char devname[USB_CFG_DEVICE_NAME_LEN] = {USB_CFG_DEVICE_NAME};

    unsigned char   rawVid[2] = {USB_CFG_VENDOR_ID};
    unsigned char    rawPid[2] = {USB_CFG_DEVICE_ID};

    int vid = rawVid[0] + 256 * rawVid[1];
    int pid = rawPid[0] + 256 * rawPid[1];


    while ((dev = devs[i++]) != NULL) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(dev, &desc); /* this always succeeds */
        // Do the VID and PID match?
        if (desc.idVendor == vid && desc.idProduct == pid) {
            found = 1;
            r = libusb_open(dev, &handle);
            // If we can't open it, keep trying.
            // There may be a device with the same pid and vid but not a Panda Display
            if (r < 0) {
                continue;
            }
            openable = 1;
            r = libusb_get_string_descriptor_ascii(handle, desc.iProduct, prod, sizeof prod);
            if (r < 0) {
                libusb_close(handle);
                return NULL;
            }
            // Here we have something that matches the free
            // VID and PID offered by Objective Development.
            // Now we need to Check device name to see if it
            // really is a Panda Display.
            if ((0 == strncmp((char *)prod, devname, USB_CFG_DEVICE_NAME_LEN)) &&
                (desc.idVendor == vid) &&
                (desc.idProduct == pid)) {
                return handle;
            }
            libusb_close(handle);
        }
    }

    if (found) {
        if (openable)
                  fprintf (stderr, "Found USB device matching 16c0:05df, but it isn't a Panda Display\n");
        else
                  fprintf (stderr, "Found something that might be a Panda Display, but couldn't open it.\n");
    }

    return NULL;
}

static int lites_init_usb (void)
{
    libusb_device **devs;
    libusb_context *ctx = NULL;
    ssize_t cnt;
    int r, i, pos;

    if (lights_handle != NULL)
        return 1;

    r = libusb_init(&ctx);
    if (r < 0)
        return 0;

    cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0)
        return 0;

    lights_handle = get_panda_handle(devs);
    if (lights_handle == NULL)
        return 0;

    if (libusb_kernel_driver_active(lights_handle, 0) == 1)
        libusb_detach_kernel_driver(lights_handle, 0);

    r = libusb_claim_interface(lights_handle, 0);
    if(r < 0)
        return 0;

    return 1;
}

/* One-time initialization
 * Accepts: port base register
 * Returns: T if success, NIL if failure
 */

int lites_init (unsigned int prt)
{
  int ret;
  unsigned int i;

  if (prt == 0)
    return lites_init_usb ();
				/* enable access to the port */
  if ((ret = !ioperm (prt,PORT_MAX,ENABLE))) {
    port = prt;			/* access granted, note port */
    outb (0,port+PORT_CONTROL);	/* initialize the displays */
    for (i = 0; i <= UNIT_MAX; --i) {
      lites_setdisplay (i);	/* select unit */
      lites_setenable (ENABLE);	/* enable the display */
    }
  }
  return ret;
}


/* Set a display unit
 * Accepts: unit number (0 - 3)
 *
 * Unfortunately, the Panda display is a write only device, so there's no
 * way to know how many units are attached (or even if there are any
 * attached at all, for that matter!)...
 */

void lites_setdisplay (unsigned int unt)
{
  if ((unt <= UNIT_MAX) && (unit != unt)) lites_wreg (REG_SELECT,unit = unt);
}


/* Set the enable for the main data LEDs on the current display
 * Accepts: enable or disable
 *
 * Note that this has no effect on the status LEDs, nor does it change the
 * data actually stored in registers 0..4 - it simply turns off the LED
 * drivers...
 */

void lites_setenable (short enable)
{
  lites_wreg (REG_ENABLE,enable ? ENABLE : DISABLE);
}

/* Display data lights
 * Accepts: 5 bytes of data
 *
 * Send to the main data LEDs.  Remember that register 0 is the left most
 * (the MSBs) and register 4 the right most.
 */

void lites_data (unsigned char data[5])
{
  lites_wreg (REG_DATA_MSB,data[0]);
  lites_wreg (REG_DATA_1,data[1]);
  lites_wreg (REG_DATA_2,data[2]);
  lites_wreg (REG_DATA_3,data[3]);
  lites_wreg (REG_DATA_LSB,data[4]);
}


/* Display status lights
 * Accepts: LED status
 *
 * Since these all occupy the same register, there's no way to set them
 * individually...
 */

void lites_status (unsigned char data)
{
  lites_wreg (REG_STATUS,data & STATUS_LEDS);
}

/* Write byte to the specified register
 * Accepts: register
 *	    data byte
 *
 *  It's not safe to change the register select bits and the STROBE at
 * the same time.  That's because there's no guarantee that the bits will
 * all change simultaneously at the other end of the cable (because of
 * variations in propagation delay, slew rate, ringing, etc) and any skew
 * between the strobe and the address bits would cause glitches on the
 * register clocks.  The only safe thing is to set the register address
 * and then pulse the STROBE while holding the register constant.
 */

static void lites_wreg (unsigned char reg,unsigned char data)
{
				/* if port initted and register valid */
  if (port && (reg <= REG_MAX)) {
				/* send the data */
    outb (data,port + PORT_DATA);
				/* select the register */
    outb (control[reg],port + PORT_CONTROL);
				/* pulse strobe */
    outb (control[reg] | STROBE,port + PORT_CONTROL);
    outb (control[reg],port + PORT_CONTROL);
  }
}

static void lights_latch (void)
{
    unsigned char buffer[8];

    if (lights_handle == NULL)
        return;

    buffer[0] =  (lights_left  >> 14) & 0037;
    buffer[1] =  (lights_left  >>  6) & 0377;
    buffer[2] =  (lights_left  <<  2) & 0374;
    buffer[2] |= (lights_right >> 16) & 0003;
    buffer[3] =  (lights_right >>  8) & 0377;
    buffer[4] =   lights_right        & 0377;

    buffer[5] = (lights_aux << 4) & 0340;
    buffer[6] = 0;
    buffer[7] = 0;

    libusb_control_transfer(lights_handle,
                            LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
                            LIBUSB_REQUEST_SET_CONFIGURATION,
                            0x0000,
                            0,
                            buffer,
                            sizeof buffer,
                            5000);
}

/* Routines specific to the primary display lights */

static unsigned char byte0 = 0;	/* save of aux bits and high 4 pgm lites */


/* Send 36-bit program data to the program data lights
 * Accepts: lh
 *	    rh
 */

void lights_pgmlites (unsigned long lh,unsigned long rh)
{
  unsigned char data[5];

  if (lights_handle) {
    lights_left = lh;
    lights_right = rh;
    lights_latch ();
    return;
  }

  lites_setdisplay (UNIT_PGM);	/* select program display lights unit */
				/* calculate MSB with aux bits */
  byte0 = data[0] = ((lh >> 14) & 0xf) | (byte0 & 0xe0);
  data[1] = (lh >> 6) & 0xff;	/* calculate other bytes */
  data[2] = ((lh << 2) | (rh >> 16)) & 0xff;
  data[3] = (rh >> 8) & 0xff;
  data[4] = rh & 0xff;
  lites_data (data);		/* send data */
}


/* Send 3-bit auxillary data to the program data lights
 */

void lights_pgmaux (unsigned char aux)
{
  lites_setdisplay (UNIT_PGM);	/* select program display lights unit */
				/* rewrite just the MSB */
  lites_wreg (REG_DATA_MSB,byte0 = (byte0 & 0xf) | ((aux & 0x7) << 5));
}


/* System heartbeat
 */

void lights_status (char cpu,char disk,char tape,char net,char push)
{
  static unsigned char state = 0;
  state |= (cpu ? STATUS_LED1 : 0) | (disk ? STATUS_LED2 : 0) |
    (tape ? STATUS_LED3 : 0) | (net ? STATUS_LED4 : 0);
  if (push) {			/* push the state? */
    lites_status (state);	/* yes, do so */
    state = 0;			/* clear state for next time */
  }
}

#endif /* KLH10_DEV_LITES */
