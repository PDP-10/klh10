/* VDISK.H - Virtual Disk support definitions
*/
/* $Id: vdisk.h,v 2.5 2002/05/21 09:47:40 klh Exp $
*/
/*  Copyright © 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: vdisk.h,v $
 * Revision 2.5  2002/05/21 09:47:40  klh
 * Change protos for vdk_read, vdk_write
 *
 * Revision 2.4  2002/03/28 16:53:20  klh
 * Added "SIMH" as synonym for "DLW8" format
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef VDISK_INCLUDED		/* Only include once */
#define VDISK_INCLUDED 1

#ifdef RCSID
 RCSID(vdisk_h,"$Id: vdisk.h,v 2.5 2002/05/21 09:47:40 klh Exp $")
#endif

/* Canonical C true/false values */
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#ifndef VDK_DISKMAP		/* Set TRUE to include disk mapping code */
# ifdef KLH10_DISKMAP
#  define VDK_DISKMAP KLH10_DISKMAP
# else
#  define VDK_DISKMAP 0
# endif
#endif

#ifndef VDK_SECTOR_SIZE		/* Allow specifying sector size in wds */
# define VDK_SECTOR_SIZE 128		/* Default for all known DEC disks */
# define VDK_NWDS(d) VDK_SECTOR_SIZE	/* Size as function of disk unit */
#endif

/* Available virtual disk formats (how to represent PDP-10 words on disk). 
 * Note that the "SIMH" format was added as a convenient synonym for DLW8,
 * which is the format used by Supnik's SIMH emulator.
 */
#define VDK_FORMATS \
 vdk_fmt(VDK_FMT_RAW, "RAW", "Raw - no conversion",			\
				2*sizeof(w10_t), cvtfr_raw, cvtto_raw),	\
 vdk_fmt(VDK_FMT_RARE, "RARE", "Rare - input clean, output raw",	\
				2*sizeof(w10_t), cvtfr_rare, cvtto_raw),\
 vdk_fmt(VDK_FMT_DBD9, "DBD9", "Disk_BigEnd_Double (9/2) (H36)",	\
					9, cvtfr_dbd9, cvtto_dbd9),	\
 vdk_fmt(VDK_FMT_DLD9, "DLD9", "Disk_LittleEnd_Double (9/2)",		\
					9, cvtfr_dld9, cvtto_dld9),	\
 vdk_fmt(VDK_FMT_DBW8, "DBW8", "Disk_BigEnd_Word (8)",			\
					2*8, cvtfr_dbw8, cvtto_dbw8),	\
 vdk_fmt(VDK_FMT_DLW8, "DLW8", "Disk_LittleEnd_Word (8)",		\
					2*8, cvtfr_dlw8, cvtto_dlw8),	\
 vdk_fmt(VDK_FMT_SIMH, "SIMH", "Disk_LittleEnd_Word (8)",		\
					2*8, cvtfr_dlw8, cvtto_dlw8),	\
 vdk_fmt(VDK_FMT_DBH4, "DBH4", "Disk_BigEnd_Halfword (8)",		\
					2*2*4, cvtfr_dbh4, cvtto_dbh4),	\
 vdk_fmt(VDK_FMT_DLH4, "DLH4", "Disk_LittleEnd_Halfword (8)",		\
					2*2*4, cvtfr_dlh4, cvtto_dlh4)


enum {
# define vdk_fmt(i,n,c,s,f,t) i
	VDK_FORMATS
	, VDK_FMT_N
# undef vdk_fmt
};


#if VDK_DISKMAP
struct vdk_header {
#  if 0
	char dh_id[4];
	int dh_ver;

	/* Other config params, etc etc */
#  endif
	osdaddr_t dh_freep;
};
#endif /* VDK_DISKMAP */

struct vdk_unit {
	char dk_devname[16];	/* Device name, eg "RP06" */
	int dk_format;		/* Data format */
	char *dk_filename;	/* Pathname of diskfile */
	osfd_t dk_fd;		/* Diskfile I/O handle */
	int dk_iswrite;		/* TRUE if R/W, else RO */

	/* Config info */
	int dk_dtype;		/* Emulated disk type */
	unsigned dk_ncyls,	/* Emulated # cylinders/unit */
		dk_ntrks,	/* Emulated # tracks/cylinder */
		dk_nsecs;	/* Emulated # sectors/track */
	unsigned dk_nwds;	/* Emulated # words/sector */
	unsigned dk_bytesec;	/* # real bytes per emulated sector */
				/*  (depends on dk_format) */

	unsigned char *dk_buf;	/* M Pointer to conversion buffer */
	size_t dk_bufsiz;	/* Size of conversion buffer */
	unsigned dk_bufsecs;	/* # sectors that fit in buffer */
	void (*dk_fmt2wds)(	/* Conversion routine, disk->mem */
		w10_t *, int, unsigned char *);
	void (*dk_wds2fmt)(	/* Conversion routine, mem->disk */
		unsigned char *, w10_t *, int);
	char *dk_blkbuf;	/* M Pointer to scratch block-size buffer */

	void (*dk_errhan)(	/* Error handler */
		struct vdk_unit *, char *);
	char *dk_errarg;	/* Arg to handler */
	int dk_err;		/* # of last I/O error (0 if none) */

#if VDK_DISKMAP
	int dk_ismap;		/* TRUE if disk being mapped */
	struct vdk_header dk_dfh;	/* Copy of diskfile header */
	int dk_blkalign;	/*  Block alignment at end of cylinder: */
				/*	0 - none, wrap uses next cyl */
				/*	+ - round up, wrap uses bogus sects */
				/*	- - round down, wrap ignores sects */
	unsigned dk_secblk;	/*   Blocking factor: # sectors per block */
	unsigned dk_wdsblk;	/*   # words per block */
	uint32 dk_byteblk;	/*   # bytes per block */
	unsigned dk_blkcyl;	/*   # blocks per cylinder */
	uint32 dk_nblks;	/*   # blocks (# entries in map) */
	osdaddr_t *dk_map;	/* M Pointer to map */
	osdaddr_t dk_seekp;	/*   Last requested seek location */
	osdaddr_t dk_freep;	/*   First known free location */
#endif /* VDK_DISKMAP */

};


/* Facility declarations */

extern int vdk_init(struct vdk_unit *,
		    void (*)(struct vdk_unit *, char *), char *);
extern int vdk_mount(struct vdk_unit *, char *, int);
extern int vdk_unmount(struct vdk_unit *);
extern int vdk_read(struct vdk_unit *, w10_t *, uint32, int);
extern int vdk_write(struct vdk_unit *, w10_t *, uint32, int);

/* Compute block number given disk, cylinder, track, sector? */
#define vdk_blknum(d,c,t,s)	/* unfinished */


#define vdk_ismounted(d) ((d)->dk_filename != NULL)
#define vdk_iswritable(d) ((d)->dk_iswrite)

/* TRUE if track & sector is valid start of a block */
#define vdk_secisblk(d,t,s) \
	(((((uint32)(t)*(d)->dk_nsecs) + (s)) % (d)->dk_secblk) ? FALSE : TRUE)

#endif /* ifndef VDISK_INCLUDED */
