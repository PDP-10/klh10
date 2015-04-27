/*  Note: the source for DUMPER.MAC changed considerably between v.419, which
    implemented formats 3,4,5, and v.563 which implemented 4,5,6.
    What follows is a synthesis of info gleaned from both.
*/

/* COMMENT

	F O R M A T   O F   D U M P E R   T A P E S
	===========================================


EACH PHYSICAL RECORD WRITTEN BY DUMPER CONTAINS ONE OR MORE
LOGICAL RECORDS, EACH OF WHICH IS 518 (1006 OCTAL) WORDS LONG.

EACH LOGICAL RECORD HAS THE FOLLOWING FORMAT:

	!=======================================================!
CHKSUM  !          CHECKSUM OF ENTIRE 518-WORD RECORD           !  +0
	!-------------------------------------------------------!
ACCESS  !         PAGE ACCESS BITS (CURRENTLY NOT USED)         !  +1
	!-------------------------------------------------------!
TAPNO   !SCD!    SAVESET NUMBER     !        TAPE NUMBER        !  +2
	!-------------------------------------------------------!
PAGNO   !F1!F2!    FILE # IN SET    !      PAGE # IN FILE       !  +3
	!-------------------------------------------------------!
TYP     !              RECORD TYPE CODE (NEGATED)               !  +4
	!-------------------------------------------------------!
SEQ     !        RECORD SEQUENCE NUMBER (INCREASES BY 1)        !  +5
	!=======================================================!
	!                                                       !
	!         CONTENTS OF FILE PAGE IF DATA RECORD          !
	!        OTHER TYPES HAVE OTHER INFORMATION HERE        !
	!                                                       !
	!=======================================================!


TYPE	VALUE	MEANING
----	-----	-------
DATA	  0	CONTENTS OF FILE PAGE
TPHD	  1	NON-CONTINUED SAVESET HEADER
FLHD	  2	FILE HEADER (CONTAINS FILESPEC, FDB)
FLTR	  3	FILE TRAILER
TPTR	  4	TAPE TRAILER (OCCURS ONLY AFTER LAST SAVESET)
USR	  5	USER DIRECTORY INFORMATION
CTPH	  6	CONTINUED SAVESET HEADER
FILL	  7	NO MEANING, USED FOR PADDING


SCD (3 BITS) - 0=NORMAL SAVE, 1=COLLECTION, 2=ARCHIVE, 3=MIGRATION

F1 F2	MEANING
-- --	-------
 0  0	OLD-FORMAT TAPE (NO FILE # IN PAGNO BITS 2-17)
 1  1	OLD-FORMAT TAPE, CONTINUED FILE
 0  1	NEW-FORMAT TAPE (FILE # IN PAGNO BITS 2-17)
 1  0	NEW-FORMAT TAPE, CONTINUED FILE

A DUMPER TAPE IS A COLLECTION OF RECORDS ORGANIZED IN THE
FOLLOWING FASHION:


!=======================================================!
!            HEADER FOR FIRST SAVESET (TPHD)            !
!-------------------------------------------------------!
!          USER INFO (USR) OR FILE (SEE BELOW)          !
!-------------------------------------------------------!
!                   USER INFO OR FILE                   !
!-------------------------------------------------------!
!                           .                           !
!                           .                           !
!                           .                           !
!=======================================================!
!            HEADER FOR SECOND SAVESET (TPHD)           !
!-------------------------------------------------------!
!          USER INFO (USR) OR FILE (SEE BELOW)          !
!-------------------------------------------------------!
!                   USER INFO OR FILE                   !
!-------------------------------------------------------!
!                           .                           !
!                           .                           !
!                           .                           !
!=======================================================!
!                                                       !
!                  SUBSEQUENT SAVESETS                  !
!                                                       !
!=======================================================!
!                                                       !
!                     LAST SAVESET                      !
!                                                       !
!=======================================================!
!                  TAPE TRAILER (TPTR)                  !
!=======================================================!


NOTES:

1.  ON LABELED TAPES, THE TPTR RECORD APPEARS ONLY IF
    THE SAVESET IS CONTINUED ON ANOTHER TAPE.

2.  SOLITARY TAPE MARKS (EOF'S) ARE IGNORED ON INPUT.
    TWO CONSECUTIVE TAPE MARKS ARE INTERPRETED AS TPTR.

3.  ON LABELED TAPES, EACH SAVESET OCCUPIES EXACTLY ONE FILE.

4.  THE FIRST RECORD OF A CONTINUED SAVESET IS CTPH
    INSTEAD OF TPHD.

A DISK FILE SAVED ON A DUMPER TAPE ALWAYS HAS THIS
SEQUENCE OF RECORDS:

!=======================================================!
!                  FILE HEADER (FLHD)                   !
!-------------------------------------------------------!
!          DATA RECORD: 1 PAGE OF FILE (DATA)           !
!-------------------------------------------------------!
!          DATA RECORD: 1 PAGE OF FILE (DATA)           !
!-------------------------------------------------------!
!                           .                           !
!                           .                           !
!                           .                           !
!-------------------------------------------------------!
!                  FILE TRAILER (FLTR)                  !
!=======================================================!

END COMMENT
*/

/* Additional notes from DUMPER version 563:

	CURFMT==6	;CURRENT FORMAT NUMBER, DO NOT CHANGE
			;6 GAINED "TONEXT" RECORD TYPE
			;5 GAINED PASSWORD ENCRYPTION AND OTHER CRDIR%oids
			;4 IS THE LOWEST LEGAL TAPE TYPE

     Old DUMPERs used record offset 1 (now .FLAG) for a "page access"
word.  In all cases it was set to a canned value on write and ignored
on read.  This not being very useful, the word has been usurped for a
flag word in tape version 6.  However, the bit values of H.HIST must
never be used as flags, since old DUMPERs always set them.

     Record type 7 WHEN WRITTEN ON TAPE is always a Filler record and
implies that the rest of the physical record can be discarded.  GETREC
does not pass these records back. If GETREC does return record type 7,
it is the SAVEEN (end of saveset) record.  Be careful of the
difference.  SAVEEN records are generated by reading into an EOF.

	Current record header format:
.CHKSM	checksum of entire record. Ignore if FL.NCK is set in .FLAG
.FLAG	flags (FL.???).  FL.HIS is always set for historical reasons.
.TAPNO	<STYP>B2 + <SavesetNumber>B17+<TapeNumber>
.PAGNO	<OLDFLG>B1 + <FileNumber>B17 + <PageNumberInFile>
.TYP	negated record type
.SEQ	sequence number (usually increases by one)

STYP = 0 Normal Save, 1 Collection, 2 Archival, 3 Migration
OLDFLG = 1B0 on an old style tape in a TAPEEN (4) record if it isn't *really*
	 the end of the file, but in fact means to go to the next tape.

     The Saveset number is only filled in in
Archival/Collection/Migration savesets.

     If, on reading a tape, a sequence number does not increase, but
stays the same or goes down (on tapes with more than one logical
record per physical record), an error was encountered while writing
the tape that didn't show up while reading it. The second physical
record is ignored.


			Tape format

     Tapes are a group of Savesets, ended by a end-of-tape record
(either TONEXT, indicating the data continued on another tape, or
TAPEEN, meaning end of all data).

	They are written as
	saveset sequence
	EOF (on some types of tapes)
	saveset sequence
	EOF (on some types of tapes)
	...
	TAPEEN or TONEXT
	EOF
	EOF (logical EOT)

	Where a saveset sequence consists of
	Saveset header (SAVEST)
	File header (FILEST)				| for each
	File data (DATA)	|for each page of data	| file in the
	File trailer (FILEEN)				| saveset.

     A TONEXT record can occur at ANY point, indicating the next tape
is needed to read the next record.  The next tape will start with a
CONTST record (continued saveset).

     And also: old tapes will have a FILEST record after a CONTST
record if mid-file, which should be ignored; and FILEEN tapes with
PG.CON set in .PAGNO are treated as TONEXT records (and are handled
that way by GETREC).

     Any physical record on tape is made up of 1-15. logical records
(always the same number of records per physical record for any given
tape).  SAVEST, CONTST and TAPEEN records are always the first in
their physical records (previous physical records being padded with
FILLER records if needed to accomplish this).


*/

/* DUMPER formats */
#define DFMTV0	0	/* BBN TENEX DUMPER format */
	/*	1 */
	/*	2 */
#define DFMTV3	3	/* T20 V2 - FDB changes, structures, etc. */
#define DFMTV4	4	/* T20 V3 - new GTDIR blocks */
#define DFMTV5	5	/* T20 V6 - bigger GTDIR blocks, pwd encryption */
#define DFMTV6	6	/* T20 V7 - record type 8, randomness */


/*
EACH PHYSICAL RECORD WRITTEN BY DUMPER CONTAINS ONE OR MORE
LOGICAL RECORDS, EACH OF WHICH IS 518 (1006 OCTAL) WORDS LONG.

(Note: max blocking factor is 15., i.e. no more than 15 logical records
per physical record.)
*/

/* Header of every DUMPER logical record */
#define RECHDR_CKSUM	0	/* Checksum */
#define RECHDR_FLAG	1	/* DV6: flags, previously ACCESS */
#define RECHDR_TAPNO	2	/* Tape number */
#define RECHDR_PAGNO	3	/* Page number */
#define RECHDR_TYP	4	/* Record type (RECTYP_xxx), negated */
#define RECHDR_SEQ	5	/* Record sequence # */
#define RECHDR_LEN	6	/* Header size (# words) */

/* Fields in FLAG:
	FL.HIS==(170000);Always set in .FLAG (historical, old page access bits)
	FL.NCK==1B0	; ([563],V6) No real checksum in .CHKSM
*/
/* Fields in TAPNO:
	700000,,0	; high 3 bits are saveset type:
			; 0 normal, 1 Collection, 2 Archival, 3 Migration
	 77777,,0	; Saveset number
	     0,,777777	; Tape number
*/
/* Fields in PAGNO:
	400000,,0	; PGNCFL - continued tape file
			;	(set in 3(FLTR), 4(TPTR), 2(FLHD))
			; [563] PG.CON means TONEXT
	200000,,0	; PGNNFL - File # is valid, if complement of PGNCFL
			; [563] PG.NFN always set, even if PG.CON set too.
	177777,,0	; File number (in saveset)
	     0,,777777	; Page number in file

F1 F2	MEANING
-- --	-------
 0  0	OLD-FORMAT TAPE (NO FILE # IN PAGNO BITS 2-17)
 1  1	OLD-FORMAT TAPE, CONTINUED FILE
 0  1	NEW-FORMAT TAPE (FILE # IN PAGNO BITS 2-17)
 1  0	NEW-FORMAT TAPE, CONTINUED FILE
*/

/* Record types (values negated in header) */
				/* [419] [563] */
#define RECTYP_DATA	0	/* DATA  DATA	data record, file contents */
#define RECTYP_TPHD	1	/* TPHD  SAVEST	Tape/saveset header	*/
#define RECTYP_FLHD	2	/* FLHD  FILEST	File header		*/
#define RECTYP_FLTR	3	/* FLTR  FILEEN	File trailer/end	*/
#define RECTYP_TPTR	4	/* TPTR  TAPEEN	Tape trailer, saveset end */
#define RECTYP_USR	5	/* USR   DIRECT	User directory info */
#define RECTYP_CTPH	6	/* CTPH  CONTST	Continued saveset header */
#define RECTYP_FILL	7	/* FILL  FILL,SAVEEN	Filler record */
#define RECTYP_FLCT	8	/* 	 TONEXT	To next tape rec	*/
				/*		(continued file)	*/

/* Record data formats (512 words following header) */

/* Type 0 (DATA) - File data page, all 512 words */

/* Types 1 and 6 (TPHD, CTPH) - Tape/saveset headers */
/*
	0: <tape format>		; FMT - A DFMTVn value
	1: <ptr to saveset name>	; PNT - either 3 or 20
	2: <TAD of dump>		; TAD

	In V4,V5 the saveset name starts at 3.
	In V6 there is more data:
	3: <sixbit VolID of tape>	; VOL (not used on read)
	4: <edit # of DUMPER>		; EDT
	20: start of saveset name	; MSG
*/

/* Type 2 (FLHD) - File header */
/*
	0: <start of ASCIZ filename spec>
	200: <start of FDB block>
*/

/* Type 3 (FLTR) - File trailer */
/*
	0: <start of FDB block> - modified to reflect file dumped

;[554] For ARCHIVed files, a tape written by 4.1 DUMPER will have
;[554] 30 FDB words, 10 words for author name, 10 words for last
;[554] writer, then 7 words of archive information. A tape written
;[554] by 6.0 DUMPER has 37 words of FDB, then the author, last writer,
;[554] and archive information. So, check the tape format so we account
;[554] for the correct number of words from FDB-start when looking for
;[554] the archive information.

	Note: the 37-word FDB appears to have its own length (in wds) as
	the first word.
*/

/* Type 4 (TPTR) - Tape trailer */
/*
	Data portion unused
*/

/* Type 5 (USR) - User directory info */
/*
	May be only a user name, or full directory info if dumped under
	right conditions (wheel, etc).

	0: <GTDIR info block>	; V6 fixes all ptrs to offsets before writing;
				; V4/V5 only fix for PSW and ACT.
	40: <username string>	; UHNAM
	60: <password string>	; UHPSW
	100: <acct string>	; UHACT
	200: <user groups array>	; CDUG (0200 words)
	400: <dir groups array>		; CDDG (0200 words)
	600: <subdir groups array>	; CDSG (0200 words)

*/

/* Type 7 (FILL) - Filler record (to fill out physical record) */
/*
	Data portion unused
*/

/* Type 8 (FLCT) - TONEXT, file continuation (new in V6)
	Note: this value was used internally in DUMPER 419 as a made-up
	type called SSND (saveset end), returned when physical EOFs
	were encountered.  DUMPER 563 re-used 7 (FILL) internally for that
	purpose.
*/
/*
	Data portion apparently unused
*/

				/* 5 bytes per 36-bit word */
				/* 518 word logical blocks */
#define TAPEBLK  518*5		/* Size of one logical block */

				/* Checksum is first word */
#define WdoffChecksum      0
#define BtoffChecksum      0
#define BtlenChecksum     36
				/* Page access bits is second word */
#define WdoffAccess        1
#define BtoffAccess        0
#define BtlenAccess       36
				/* SCD, first 3 bits in next word */
#define WdoffSCD           2
#define BtoffSCD           0
#define BtlenSCD           3
				/* Number of saveset on tape */
#define WdoffSaveSetNum    2
#define BtoffSaveSetNum    3
#define BtlenSaveSetNum   15
				/* Tape number of dump */
#define WdoffTapeNum       2
#define BtoffTapeNum      18
#define BtlenTapeNum      18
				/* F1, F2 Flag bits */
#define WdoffF1F2          3
#define BtoffF1F2          0
#define BtlenF1F2          2
				/* File Number in Set (new format only) */
#define WdoffFileNum       3
#define BtoffFileNum       2
#define BtlenFileNum      16
				/* Page Number in file */
#define WdoffPageNum       3
#define BtoffPageNum      18
#define BtlenPageNum      18
				/* Record type (2's complement) */
#define WdoffRectype       4
#define BtoffRectype       0
#define BtlenRectype      36
				/* Record sequence number */
#define WdoffRecseq        5
#define BtoffRecseq        0
#define BtlenRecseq       36

#define RecHdrlen	6	/* # words in logical record header */

				/* SCD Values */
#define SCDNormal       0
#define SCDCollection   1
#define SCDArchive      2
#define SCDMigration    3

				/* F1, F2 Values */
#define F1F2Old            0
#define F1F2OldContinue    3
#define F1F2New            1
#define F1F2NewContinue    2

				/* Record type values */
#define RectypeData     0
#define RectypeTphd     1
#define RectypeFlhd     2
#define RectypeFltr     3
#define RectypeTptr     4
#define RectypeUsr      5
#define RectypeCtph     6
#define RectypeFill     7
#define RectypeTonext   8	/* V6: "to next tape" */

char *rectypes[] = {
    "DATA",
    "ISSH",
    "FLHD",
    "FLTR",
    "TPTR",
    "UDIR",
    "CSSH",
    "FILL",
    "NEXT"
};

#define BtoffWord       0
#define BtlenWord       36

/* Word offsets for saveset header data (Record types 1, 6) */
#define WdoffSSFormat	RecHdrlen+0	/* Saveset format (DUMPER fmt) */
#define WdoffSSNamoff	RecHdrlen+1	/* Saveset name offset */
#define WdoffSSDate	RecHdrlen+2	/* Saveset date (TAD) */

#define WdoffFLName        6            /* Filename offset (type 2) */
#define WdoffFDB         134            /* FDB offset (type 2) */

#define WdoffFDB_CTL	01+WdoffFDB	/* Control word .FBCTL */

#define BtoffFDB_Arc	11		/* archived */
#define BtlenFDB_Arc	1

#define BtoffFDB_Inv	12		/* invisible */
#define BtlenFDB_Inv	1

#define BtoffFDB_Off	13		/* offline */
#define BtlenFDB_Off	1

#define WdoffFDB_PRT     04+WdoffFDB	/* protection */
#define BtoffFDB_PRT       18
#define BtlenFDB_PRT       18

#define WdoffFDB_BSZ     011+WdoffFDB	/* Number of bits per byte */
#define BtoffFDB_BSZ       6
#define BtlenFDB_BSZ       6

#define WdoffFDB_PGC     011+WdoffFDB	/* Number of pages in the file */
#define BtoffFDB_PGC      18
#define BtlenFDB_PGC      18

#define WdoffFDB_Size    012+WdoffFDB	/* Number of bytes in the file */

#define BtoffFDB_Size      0
#define BtlenFDB_Size     36

#define WdoffFDB_Wrt     014+WdoffFDB	/* Date of last write to file */

#define WdoffFDB_Ref     015+WdoffFDB	/* read time */

#define WdoffFDB_PGC_A	022+WdoffFDB	/* Pagecount before archive */

#define WdoffFDB_TP1	033+WdoffFDB	/* Tape ID for archive run 1 */

#define WdoffFDB_SS1	034+WdoffFDB	/* Saveset # for archive run 1 */
#define BtoffFDB_SS	0
#define BtlenFDB_SS	18
#define WdoffFDB_TF1	034+WdoffFDB	/* Tape file # for archive run 1 */
#define BtoffFDB_TF	18
#define BtlenFDB_TF	18

#define WdoffFDB_TP2	035+WdoffFDB	/* Tape ID for archive run 2 */
#define WdoffFDB_SS2	036+WdoffFDB	/* Saveset # for archive run 2 */
#define WdoffFDB_TF2	036+WdoffFDB	/* Tape file # for archive run 2 */
