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
LIBS64=-L$(MQ)/lib64
LIBS32=-L$(MQ)/lib

CCOPTS= -g -I$(MQ)/inc
CC64OPTS = -m64 $(CCOPTS)
CC32OPTS = -m32 $(CCOPTS)
DEPS =
OTELLIBDIR=-L/usr/local/lib -L/usr/local/64
OTELLIBS = -lopentelemetry_trace
# OTELLIBS = otel2.a # Perhaps use something like this for archive library linking
OTELINCDIR=/usr/local/include/opentelemetry

all: dirs  $(B)/$(APIX)_r.32 $(B)/$(APIX).32 $(B)/$(APIX)_r.64 $(B)/$(APIX).64 $(B)/$(DLMOD)

$(B)/$(DLMOD):  $(DLSRC) mqiotel.hpp Makefile
	g++ -D_REENTRANT $(LDOPTS) $(CC64OPTS) -o $@ $(DLSRC) -L$(OTELLIBDIR) -I$(OTELINCDIR) $(OTELLIBS) -DOPENTELEMETRY_ABI_VERSION_NO=2

$(B)/$(APIX)_r.64 : $(SRC) $(DEPS) Makefile
	        gcc $(CC64OPTS) -D_REENTRANT  -o $@ $(SRC) -g \
	        $(LIBS64)  -lmqmzf_r -lmqm_r $(LDOPTS)

$(B)/$(APIX).64 : $(SRC) $(DEPS) Makefile
	        gcc $(CC64OPTS) -o $@ $(SRC) -g \
	        $(LIBS64)  -lmqmzf -lmqm $(LDOPTS)

$(B)/$(APIX)_r.32 : $(SRC) $(DEPS) Makefile
	        gcc $(CC32OPTS) -D_REENTRANT  -o $@ $(SRC) -g \
	        $(LIBS32)  -lmqmzf_r -lmqm_r $(LDOPTS)

$(B)/$(APIX).32 : $(SRC) $(DEPS) Makefile
	        gcc $(CC32OPTS) -o $@ $(SRC) -g \
	        $(LIBS32)  -lmqmzf -lmqm $(LDOPTS)

dirs:
	@mkdir -p $(B) 
dummy:
	/bin/true
