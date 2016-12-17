## GNU Makefile 

## edit config.mk as-needed for your environment
include config.mk

CC          = $(CROSS)gcc
LD          = $(CROSS)ld
AR          = $(CROSS)ar
PKG_CONFIG  = $(CROSS)pkg-config

SRCDIR      = src
OBJDIR      = obj
BINDIR      = bin

SOURCES     = $(wildcard $(SRCDIR)/*.c)
OBJECTS     = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%$(xtn).o,$(SOURCES))
TARGETSBASE = timcat timver
TARGETSTHIS = $(addsuffix $(xtn),$(TARGETSBASE))
TARGETS     = $(addprefix $(BINDIR)/,$(TARGETSTHIS))
TARGETS    += config.mk
CLEAN_LIST  = $(foreach f,$(TARGETSBASE),$(wildcard $(OBJDIR)/$(f)*.o))

##########################################################################

.PHONY: all debug all-foreach-arch debug-foreach-arch test clean clean-all showconf

all: $(TARGETS)

all-foreach-arch: all
	@echo
	@echo "Building for Linux.x86_64 ..."
	@LINUX64=1 LINUX32=0 USE_MXE=0 $(MAKE) --no-print-directory all
	@echo
	@echo "Building for Linux.i686 ..."
	@LINUX64=0 LINUX32=1 USE_MXE=0 $(MAKE) --no-print-directory all
	@echo
	@echo "Building for Win32.i686 ..."
	@LINUX64=0 LINUX32=0 USE_MXE=1 $(MAKE) --no-print-directory all

debug:
	FOR_GDB=1 $(MAKE) --no-print-directory all

debug-foreach-arch:
	FOR_GDB=1 $(MAKE) --no-print-directory all-foreach-arch

test: clean showconf $(BINDIR)/hw$(xtn) $(BINDIR)/test_getaddrinfo$(xtn)

Makefile: 
	$(error "Why doesn't Makefile exist in $(shell pwd)")

$(OBJECTS): | $(OBJDIR)
$(OBJDIR):
	@echo "[DIR]  $@"
	@mkdir -p $(OBJDIR)

$(TARGETS): | $(BINDIR)
$(BINDIR):
	@echo "[DIR]  $@"
	@mkdir -p $(BINDIR)

$(SRCDIR)/%.bin2c: $(SRCDIR)/%.c
	@echo "[XXD]  $@"
	$(shell xxd -i <"$<" >"$@")

$(OBJDIR)/%$(xtn).o: $(SRCDIR)/%.c $(SRCDIR)/%.bin2c
	@echo "[OBJ]  $@"
	@$(CC) -o $@ -c $(CFLAGS) $<

$(BINDIR)/timcat$(xtn): $(OBJDIR)/timcat$(xtn).o
	@echo "[BIN]  $@"
	@$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

$(BINDIR)/timver$(xtn): $(OBJDIR)/timver$(xtn).o
	@echo "[BIN]  $@"
	@$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

clean:
	rm -f $(OBJECTS)

clean-all:
	rm -f $(CLEAN_LIST)

showconf:
	@echo " "
	@echo "            Makefile Configuration             "
	@echo "==============================================="
	@echo "\$$(CROSS)   = $(CROSS)"
	@echo "\$$(xtn)     = $(xtn)"
	@echo "\$$(CFLAGS)  = $(CFLAGS)"
	@echo "\$$(LDFLAGS) = $(LDFLAGS)"
	@echo "\$$(LIBS)    = $(LIBS)"
	@echo " "
	@echo "\$$(PKG_CONFIG) = $(PKG_CONFIG)"
	@echo "\$$(CC)         = $(CC)"
	@echo "\$$(LD)         = $(LD)"
	@echo "\$$(AR)         = $(AR)"
	@echo "\$$(PATH)       = $(PATH)"
	@echo " "
	@echo "\$$(SOURCES)    = $(SOURCES)"
	@echo "\$$(OBJECTS)    = $(OBJECTS)"
	@echo "\$$(TARGETS)    = $(TARGETS)"
	@echo " "

config.mk:
	@cp -i -n config.def.mk config.mk
	@echo " "
	@echo "[CFG]  using config.def.mk "
