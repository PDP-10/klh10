# Makefile fragment for building kl

####################################################################
##	Specific KLH10 configuration
##

# Standard setup for KL (TOPS-10 and TOPS-20)
#
CONFFLAGS = -DKLH10_CPU_KLX=1	\
	    -DKLH10_SYS_T20=1	\
	    -DKLH10_EVHS_INT=1	\
		    -DKLH10_DEV_DPNI20=1 \
		    -DKLH10_DEV_DPTM03=1 \
		    -DKLH10_DEV_DPRPXX=1 \
	    -DKLH10_MEM_SHARED=1	\
	    -DKLH10_RTIME_OSGET=1	\
	    -DKLH10_ITIME_INTRP=1	\
	    -DKLH10_CTYIO_INT=1	\
	    -DKLH10_APRID_SERIALNO=3600 \
	    -DKLH10_CLIENT=\"MyKL\"

all:	kn10-kl tapedd vdkfmt wxtest enaddr

#---
