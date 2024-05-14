FAUST_PREFIX?=/usr/local

suffix=$(shell $(CC) -dumpmachine | awk -F -linux '{print $$1}')

LLVM_VERSION?=$(shell $(FAUST_PREFIX)/bin/faust --version | sed -En 's/^Build with LLVM version ([0-9]*).*/\1/p')

ifeq ($(suffix),aarch64-jelos)
	FAUST_PREFIX=
	INC=pfx.aarch64-jelos/include
	LIB=pfx.aarch64-jelos/lib
	LLVM_LIBS=-L. -l:./libLLVM-17.so # -Wl,--rpath=. # $(LIB)/pfx.aarch64-jelos/lib/
	CXXFLAGS= -march=armv8-a 
else
	INC=$(shell $(FAUST_PREFIX)/bin/faust -includedir)
	LIB=$(shell $(FAUST_PREFIX)/bin/faust -libdir)
	LLVM_LIBS=$(shell llvm-config-$(LLVM_VERSION) --libs)
	# LLVM_LIBS=-lLLVM
	CXXFLAGS= -march=native $(shell llvm-config-$(LLVM_VERSION) --cppflags)
endif

CXXFLAGS+=-I$(INC) -DMIDICTRL -DOSCCTRL -DSOUNDFILE -DDYNAMIC_DSP -std=c++11 -O3 -fpermissive -fPIC 
FAUST_LIBS=$(LIB)/libfaust.a -L$(LIB) -lOSCFaust  #$(LIB)/libOSCFaust.a
LDFLAGS='-Wl,-rpath,$$ORIGIN' -lz -lncurses $(FAUST_LIBS) $(LLVM_LIBS) $(shell pkg-config --cflags --libs jack sndfile alsa) -shared

# $(CXX) $(CXXFLAGS) MfxDspFaust.cpp $(LDFLAGS) -o MfxDspFaust
build:
	$(CXX) $(CXXFLAGS) MfxDspFaust.cpp $(LDFLAGS) -o libMfxFaust.$(suffix).so
