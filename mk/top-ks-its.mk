# Makefile fragment for building ks-its

####################################################################
##	Specific KLH10 configuration
##

# Standard setup for KS ITS
#
CONFFLAGS = -DKLH10_CPU_KS=1	\
	    -DKLH10_SYS_ITS=1	\
	    -DKLH10_EVHS_INT=1	\
		    -DKLH10_DEV_DPTM03=1 \
		    -DKLH10_DEV_DPRPXX=1 \
		    -DKLH10_DEV_DPIMP=1 \
	    -DKLH10_SIMP=0 \
	    -DKLH10_MEM_SHARED=1 \
	    $(TINTFLAGS) \
	    $(DINTFLAGS) \
	    -DKLH10_APRID_SERIALNO=4097 -DKLH10_DEVMAX=12 \
	    -DKLH10_CLIENT=\"MyITS\" \
	    $(CONFFLAGS_AUX) \
	    -DVMTAPE_ITSDUMP=1

all:	kn10-ks-its tapedd vdkfmt wxtest enaddr

#---
