## !!!! DO NOT EDIT config.def.mk, run 'make config.mk', then edit config.mk !!!!
## 
##  GNU Makefile
LIBS        := -lssh2

ifeq ($(FOR_GDB),1)
  ## Debug
  xtn       := _dbg
  OFAST     := 
  OGOLD     := 
  CFLAGS    := -O0 -g2 -Wall -std=c99 -pedantic -D_DBGLOG
  LDFLAGS   := -O0 -g2 -std=c99
else
  ## Release
  xtn       :=
  OFAST     := -O2 -fwhole-program -flto
  OFAST     += -funroll-loops -fweb -ffast-math -fuse-linker-plugin 
  OGOLD     := -fuse-ld=gold
  CFLAGS    += -Wall -std=c99 $(OFAST) 
  #CFLAGS    += -D_INCLUDE_TIMCAT_SOURCE
  LDFLAGS   += -s -std=c99 $(OFAST)
endif


## LINES BELOW HERE ARE ONLY REQUIRED FOR CROSS-COMPILING
##   (see "all-foreach-arch:" target in Makefile)

CROSS :=
ifeq ($(USE_MXE),1)
  ## GCC WIN32 (MXE build system)
  xtn      := $(xtn).exe
  PATH     := /opt/mxe/usr/bin:$(PATH)
  CROSS    := i686-pc-mingw32-
  LIBS     += -lws2_32 -lgcrypt -lgpg-error -lz
  CFLAGS   += -mwin32 -mconsole -DWINVER=0x0501
  LDFLAGS  += -static -s
else 
 ifeq ($(LINUX32),1)
   ## GCC multilib (linux i686 -w32)
   xtn      := $(xtn)-i686
   CARCH    := i686
   CHOST    := i686-pc-linux-gnu
   CFLAGS   += -m32 -march=i686 -mtune=generic
   LDFLAGS  += -m32
 else ifeq ($(LINUX64),1)
    ## GCC multilib (linux x86_64 -w64)
    xtn      := $(xtn)-x86_64
    CFLAGS   += -m64 -march=nocona -mtune=generic $(OGOLD)
    LDFLAGS  += -m64 -march=nocona -mtune=generic $(OGOLD)
 else
    ## native compilation
    xtn      := $(xtn)-native
    CFLAGS   += -march=native -mtune=native $(OGOLD)
    LDFLAGS  += -march=native -mtune=native $(OGOLD)
 endif
endif

