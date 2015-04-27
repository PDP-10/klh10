/* DPIMP.H - Definitions for IMP emulator
*/
/* $Id: dpimp.h,v 2.4 2001/11/19 10:45:49 klh Exp $
*/
/*  Copyright © 1992, 1998, 2001 Kenneth L. Harrenstien
**  All Rights Reserved
**
**  This file is part of the KLH10 Distribution.  Use, modification, and
**  re-distribution is permitted subject to the terms in the file
**  named "LICENSE", which contains the full text of the legal notices
**  and should always accompany this Distribution.
**
**  This software is provided "AS IS" with NO WARRANTY OF ANY KIND.
**
**  This notice (including the copyright and warranty disclaimer)
**  must be included in all copies or derivations of this software.
*/
/*
 * $Log: dpimp.h,v $
 * Revision 2.4  2001/11/19 10:45:49  klh
 * Add blob into dpimp_s for ARP cache.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef DPIMP_INCLUDED
#define DPIMP_INCLUDED 1

#ifdef RCSID
 RCSID(dpimp_h,"$Id: dpimp.h,v 2.4 2001/11/19 10:45:49 klh Exp $")
#endif


#ifndef SICONF_SIMP
# define SICONF_SIMP 1	/* TRUE if compiling Simple-IMP only, not true IMP */
#endif

/* IMP leader definitions */

/* Byte offsets in IMP leader */
#define SIL_FMT   0	/* Always 0000 + 1111 (new 96-bit format type) */
			/*	Else low 4 bits are old msg type (4 = nop) */
#define SIL_NET   1	/* Net number (always 0) */
#define SIL_FLG   2	/* Flags (0363 unused, 010 trace (ignored), */
			/*		    and 04 shd be ignored) */
#define SIL_TYP   3	/* Message type */
#define SIL_HTY   4	/* Handling type */
			/*	(7=big buffs, 4=small buffs, 0=ctl link) */
#define SIL_HST   5	/* Host number on IMP */
#define SIL_IMP1  6	/* IMP number (high 8 bits) */
#define SIL_IMP0  7	/*  "     "   ( low 8 bits) */
#define SIL_LNK   8	/* Link number (High 8 bits of Message ID) */
#define SIL_SUB   9	/* Low 4 bits of MsgID (0), then 4 bits of Sub-type */
#define SIL_LEN1 10	/* Message length (high 8 bits) */
#define SIL_LEN0 11	/*    "       "   ( low 8 bits) */

#define SI_LDRSIZ 12	/* # bytes in IMP leader */
	/* Remaining bytes are data, specifically IP datagram */

#define SILNK_IP 0233	/* Link # to use for IP datagram messages */

/* IMP message types (IMP -> Host) */

#define SIMT_HH   0	/* Regular Host-Host Message */
#define SIMT_LERR 1	/* Error in Leader (no msg-id) */
#define SIMT_GDWN 2	/* IMP Going Down */
#define SIMT_UN3  3	/* - */
#define SIMT_NOP  4	/* NOP */
#define SIMT_RFNM 5	/* RFNM - Ready For Next Message (xmit succeeded) */
#define SIMT_HDS  6	/* Host Dead Status (general info) */
#define SIMT_DHD  7	/* Destination Host Dead (xmit failed) */
#define SIMT_DERR 8	/* Error in Data (has msg-id) */
#define SIMT_INC  9	/* Incomplete Transmission (xmit failed temporarily) */
#define SIMT_IRS 10	/* Interface Reset - IMP dropped its ready line */

/*	In message types 2 and 6, the going-down status 16-bit word is
	stored in (SIL_LNK<<8 + SIL_SUB).

	In message type 4 (NOP) the padding count is the low 4 bits of SIL_SUB.
	Padding is only put on type-0 messages to separate the leader from
	the data.
*/

#define SI_MAXMSG 1007	/* Max # of data bytes in an IMP regular message */

#if KLH10_DEV_DPIMP	/* Standard DP version */

#ifndef DPSUP_INCLUDED
# include "dpsup.h"
#endif

/* Version of DPIMP-specific shared memory structure */

#define DPIMP_VERSION ((1<<10) | (1<<5) | (2))	/* 1.1.2 */

/* DPIMP-specific stuff */
			/* C = controlling parent sets, D = Device proc sets */
			/*       If both, 1st letter indicates inital setter */
struct dpimp_s {
    struct dpc_s dpimp_dpc;	/* CD Standard DPC portion */
    int dpimp_ver;		/* C  Version of shared struct */
    int dpimp_attrs;		/* C  Attribute flags */
    char dpimp_ifnam[16];	/* CD Interface name if any */
    unsigned char dpimp_eth[6];	/* CD Ethernet address of interface */
    unsigned char dpimp_ip[4];	/* C 10's IP address to filter on, if shared */
    unsigned char dpimp_gw[4];	/* C Default GW address for IMP to use */
    int dpimp_inoff;		/* C Offset in buffer of input (I->H) data */
    int dpimp_outoff;		/* D Offset in buffer of output (H->I) data */
    int dpimp_backlog;		/* C Max sys backlog of rcvd packets */
    int dpimp_dedic;		/* C TRUE if dedicated ifc, else shared */
    int dpimp_doarp;		/* C TRUE to do ARP hackery, if shared */
    int dpimp_rdtmo;		/* C # secs to timeout on packetfilter read */

    /* The following is not used by the controlling parent at all; it
       is only used by DPIMP's two in/out procs.  It is here instead of in a
       separate shared segment for simplicity, and is an unstructured 
       blob to shield the controller from DPIMP implementation details.
    */
#ifndef DPIMP_BLOB_SIZE
# define DPIMP_BLOB_SIZE 2000
#endif
    size_t dpimp_blobsiz;
    unsigned char dpimp_blob[DPIMP_BLOB_SIZE];
};

/* Buffer offset:
	In order to eliminate the need to copy bytes from one buffer
into another simply to add or subtract an ethernet header or IMP
leader, I/O is actually performed at an OFFSET into the shared memory
buffers.  This offset value (dpimp_inoff or dpimp_outoff) is set by
the buffer writer and is made large enough to accomodate the largest
header/leader that other code will need to prefix onto the data.  The
data count includes this offset; to access the data, the buffer reader
must use the offset (provided by the writer) to both increment the
initial pointer and decrement the count.

	Currently IMP leaders are 12 bytes and ethernet headers are 14
bytes, so the initial offset is rounded up to 16 bytes (could be 14
but the alignment may make it slightly easier for O/S I/O).

*/
#define SIH_HSIZ 0		/* No bytes in SIMP header */
#define DPIMP_DATAOFFSET (16)	/* max(leader,etherhdr) rounded up */

/* Commands to and from DP and KLH10 LHDH driver */

	/* From 10 to DP */
#define DPIMP_RESET	0	/* Reset DP */
#define DPIMP_SPKT	1	/* Send data packet to ethernet */
#define DPIMP_SETETH	2	/* Set hardware ethernet address */

	/* From DP to 10 */
#define DPIMP_INIT	1	/* DP->10 Finished init */
#define DPIMP_RPKT	2	/* DP->10 Received data packet from net */

	/* Feature bits in dpimp_doarp */
/*			0x1 */	/* TRUE -- translate into 0xF */
#define DPIMP_ARPF_PUBL	0x2	/* Register self, publish */
#define DPIMP_ARPF_PERM	0x4	/* Register self, permanent */
#define DPIMP_ARPF_OCHK	0x8	/* Check outgoing ARPs for host platform */

#endif /* KLH10_DEV_DPIMP */	/* Standard DP version */

#if KLH10_DEV_SIMP	/* Pipe version */

/* All communication between the SIMP and its owner takes place over a
** bidirectional byte stream, normally a pipe.  Each direction is
** completely independent of the other.  All data is sent as encapsulated
** packets with the following 4-byte header:
** 		Header Check - always 0354	(MagiC #)
**		Packet Type - a SIH_Txxx value
**		High byte of packet length
**		Low byte of  "      "
*/

#define SIH_HDR 0354	/* Magic byte value introducing 4-byte header */
#define SIH_TDATA 'm'	/* Basic message to and from IMP */
#define SIH_TSUIC 'd'	/* To IMP: drop dead nicely */
#define SIH_HSIZ 4	/* # bytes in header */

#endif /* KLH10_DEV_SIMP */	/* Pipe version */

#endif	/* ifndef DPIMP_INCLUDED */
