/* RCSID.H - Standardize handling of RCS ident strings
*/
/* $Id: rcsid.h,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 2001 Kenneth L. Harrenstien
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
 * $Log: rcsid.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
    The main reason for defining the RCSID macro is to allow better
control of whether, and how, source control ID strings are included in
the binary output.  The use of a macro also allows identifier
concatenation which in turn allows this mechanism to work for include
files as well, as long as they follow these conventions:

	- RCSID should be invoked only once (ie within _INCLUDED code).
	- The module name for "foo.h" should be "foo_h".
	- To be safe, use of RCSID should be within an #ifdef RCSID
		and only put where it is safe to define data.

This works because the _INCLUDED ensures there are no duplicate defs
within the current .C module, and the defs that are made will not
conflict with inclusions in other modules because of the "static"
storage class.

Another mechanism needs to be used for special cases such as include
files that are meant to be included multiple times, eg for generating
tables.

*/

#ifndef RCSID_INCLUDED
#define RCSID_INCLUDED 1

#ifndef RCSID
# ifndef lint
#  define RCSID(mod,idstr) static const char rcsid_ ## mod [] = idstr;
# else
#  define RCSID(mod,idstr)
# endif
#endif

#ifdef RCSID
 RCSID(rcsid_h,"$Id: rcsid.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#endif /* ifndef RCSID_INCLUDED */
