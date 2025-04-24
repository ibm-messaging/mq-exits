# Output directory
B = bin

APIX=mqiotel
DLMOD=mqioteldl.so
SRC = mqiotel.c
DLSRC = mqiotel_main.cc  \
        mqiotel_put.cc  \
        mqiotel_get.cc  \
        mqiotel_open.cc  \
    	mqiotel_util.cc

MQ=/opt/mqm
HEADER = $(MQ)/inc/cmqc.h \
	 $(MQ)/inc/cmqcfc.h

LDOPTS = -shared -fPIC
LIBS64DIR=-L$(MQ)/lib64
LIBS32DOR=-L$(MQ)/lib

# We don't really need these libraries as the
# environment gives us direct access to any MQI functions
#LIBS=-lmqm -lmqmzf
#LIBS_R=-lmqm_r -lmqmzf_r

CCOPTS= -g -I$(MQ)/inc
CC64OPTS = -m64 $(CCOPTS)
CC32OPTS = -m32 $(CCOPTS)
DEPS =
# Where can we find the OTel CPP libraries. This is where their build process
# puts everything by default
OTELLIBDIR=-L/usr/local/lib -L/usr/local/lib64
OTELLIBS = -lopentelemetry_trace
# OTELLIBS = /usr/local/lib64/libopentelemetry_trace.a # Perhaps use something like this if we prefer archive library linking
OTELINCDIR=/usr/local/include/opentelemetry

all: dirs  $(B)/$(APIX)_r.32 $(B)/$(APIX).32 $(B)/$(APIX)_r.64 $(B)/$(APIX).64 $(B)/$(DLMOD)

# The real work is done in this module that is dlopened from the sub
$(B)/$(DLMOD):  $(DLSRC) mqiotel.hpp Makefile
	g++ -D_REENTRANT $(LDOPTS) $(CC64OPTS) -o $@ $(DLSRC) -L$(OTELLIBDIR) -I$(OTELINCDIR) $(OTELLIBS) -DOPENTELEMETRY_ABI_VERSION_NO=2

# The "stub" API exits that get loaded in different environments - the 32 and 64-bit versions
$(B)/$(APIX)_r.64 : $(SRC) $(DEPS) Makefile
	        gcc $(CC64OPTS) -D_REENTRANT  -o $@ $(SRC) -g \
	        $(LIBS64DIR)  $(LIBS_R) $(LDOPTS)

$(B)/$(APIX).64 : $(SRC) $(DEPS) Makefile
	        gcc $(CC64OPTS) -o $@ $(SRC) -g \
	        $(LIBS64DIR)  $(LIBS) $(LDOPTS)

$(B)/$(APIX)_r.32 : $(SRC) $(DEPS) Makefile
	        gcc $(CC32OPTS) -D_REENTRANT  -o $@ $(SRC) -g \
	        $(LIBS32DIR)  $(LIBS_R) $(LDOPTS)

$(B)/$(APIX).32 : $(SRC) $(DEPS) Makefile
	        gcc $(CC32OPTS) -o $@ $(SRC) -g \
	        $(LIBS32DIR)  $(LIBS) $(LDOPTS)

dirs:
	@mkdir -p $(B)
dummy:
	/bin/true
