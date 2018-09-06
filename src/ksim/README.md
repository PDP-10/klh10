ksim
====

ksim is an 8080 simulator that attempts to be very accurate.  While
ksim may be minimally useful in its current form, it is primarily
intended as a reference implementation. There is currently no
documentation, and the code is not well-commented. Maximum performance
was not a goal, so very little optimization has been done. Interrupts
are not implemented, though they would be easy to add. There is crude
console I/O, and extremely crude disk I/O. It works just barely well
enough that I've successfully run CP/M.

As part of a Retrochallenge 2012 Winter Warmup project, I ran an 8080
exerciser and other instruction test programs on a Sol 20 to better
understand the behavior of the 8080's flags (which are NOT exactly the
same as those of the Z-80), and updated ksim to accurately simulate
the flags. There are a lot of Z-80 simulators out there, but fewer
8080 simulators, and fewer yet that get the flags right.

ksim is released under the FSF GPLv3 license.
