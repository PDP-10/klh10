/* Note that all definitions in this file are in OCTAL! */
  
/* Record types: */

#define T_LABEL   1		/* Label. */
#define T_BEGIN   2		/* Start of SaveSet. */
#define T_END     3		/* End of SaveSet. */
#define T_FILE    4		/* File data. */
#define T_UFD     5		/* UFD data. */
#define T_EOV     6		/* End of volume. */
#define T_COMM    7		/* Comment. */
#define T_CONT    010		/* Continuation. */

/* Offsets into header block: */

#define G_TYPE    0		/* Record type. */
#define G_SEQ     1		/* Sequence #. */
#define G_RTNM    2		/* Relative tape #. */
#define G_FLAGS   3		/* Flags: */
#define   GF_EOF  0400000	/*   End of file. */
#define   GF_RPT  0200000	/*   Repeat of last record. */
#define   GF_NCH  0100000	/*   Ignore checksum. */
#define   GF_SOF  0040000	/*   Start of file. */
#define G_CHECK   4		/* Checksum. */
#define G_SIZE    5		/* Size of data in this block. */
#define G_LND     6		/* Length of non-data block. */

/* header fields in T_FILE blocks: */

#define F_PCHK    014		/* Checksum of O_NAME block for this file. */
#define F_RDW     015		/* Relative data offset in file. */
#define F_PTH     016		/* Path/name components of this file. */

/* Non-data block types: */

#define O_NAME    1		/* File name. */
#define O_FILE    2		/* File attributes. */
#define O_DIRECT  3		/* Directory attributes. */
#define O_SYSNAME 4		/* System name block. */
#define O_SAVESET 5		/* SaveSet name block. */

/* Offsets in attribute block: */

#define A_FHLN	  0		/* header length word */
#define A_FLGS	  1		/* flags */
#define A_WRIT	  2		/* creation date/time */
#define A_ALLS	  3		/* allocated size */
#define A_MODE	  4		/* mode */
#define A_LENG	  5		/* length */
#define A_BSIZ	  6		/* byte size */
#define A_VERS	  7		/* version */
#define A_PROT	  010		/* protection */
#define A_ACCT	  011		/* byte pointer account string */
#define A_NOTE	  012		/* byte pointer to anonotation string */
#define A_CRET	  013		/* creation date/time of this generation */
#define A_REDT	  014		/* last read date/time of this generation */
#define A_MODT	  015		/* monitor set last write date/time */
#define A_ESTS	  016		/* estimated size in words */
#define A_RADR	  017		/* requested disk address */
#define A_FSIZ	  020		/* maximum file size in words */
#define A_MUSR	  021		/* byte ptr to id of last modifier */
#define A_CUSR	  022		/* byte ptr to id of creator */
#define A_BKID	  023		/* byte ptr to save set of previous backup */
#define A_BKDT	  024		/* date/time of last backup */
#define A_NGRT	  025		/* number of generations to retain */
#define A_NRDS	  026		/* nbr opens for read this generation */
#define A_NWRT	  027		/* nbr opens for write this generation */
#define A_USRW	  030		/* user word */
#define A_PCAW	  031		/* privileged customer word */
#define A_FTYP	  032		/* file type and flags */
#define A_FBSZ	  033		/* byte sizes */
#define A_FRSZ	  034		/* record and block sizes */
#define A_FFFB	  035		/* application/customer word */

/* T_BEGIN, T_END & T_CONT header offsets: */

#define S_DATE    014
#define S_FORMAT  015
#define S_BVER    016
#define S_MONTYP  017
#define S_SVER    020
#define S_APR     021
#define S_DEVICE  022
#define S_MTCHAR  023
#define S_REELID  024
#define S_LTYPE   025

