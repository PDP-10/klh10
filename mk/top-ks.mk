# Makefile fragment for building ks

####################################################################
##	Specific KLH10 configuration
##

# Standard setup for KS (TOPS-20, maybe TOPS-10)
#
CONFFLAGS = -DKLH10_CPU_KS=1	\
	    -DKLH10_SYS_T20=1	\
	    -DKLH10_EVHS_INT=1	\
		    -DKLH10_DEV_DPTM03=1 \
		    -DKLH10_DEV_DPRPXX=1 \
	    -DKLH10_MEM_SHARED=1 \
	    $(TINTFLAGS) \
	    $(DINTFLAGS) \
	    -DKLH10_APRID_SERIALNO=4097 -DKLH10_DEVMAX=12 \
	    -DKLH10_CLIENT=\"MyKS\" \
	    $(CONFFLAGS_AUX)

all:	kn10-ks tapedd vdkfmt wxtest enaddr

#---
