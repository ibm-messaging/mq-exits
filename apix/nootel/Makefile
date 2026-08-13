# Output directory
B = bin

APIX=mqinootel

SRC = mqiotel.c

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

all: dirs  $(B)/$(APIX)_r.32 $(B)/$(APIX).32 $(B)/$(APIX)_r.64 $(B)/$(APIX).64

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
