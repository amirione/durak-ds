# Durak DS - Makefile (devkitARM + libnds 2.x + calico)
.RECIPEPREFIX := >

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set")
endif

include $(DEVKITARM)/base_tools

LIBNDS ?= $(DEVKITPRO)/libnds
CALICO ?= $(DEVKITPRO)/calico

TARGET := durak
SRC    := main.c

ARCH    := -march=armv5te -mtune=arm946e-s
INCLUDE := -I$(LIBNDS)/include -I$(CALICO)/include
CFLAGS  := -Wall -O2 -g $(ARCH) -ffunction-sections -fdata-sections -D__NDS__ -D__ARM9__ -DARM9 $(INCLUDE)
LDFLAGS := -specs=$(CALICO)/share/ds9.specs -g $(ARCH) -L$(CALICO)/lib -L$(CALICO)/share -L$(LIBNDS)/lib
LIBS    := -lnds9 -lcalico_ds9

ARM7   := $(CALICO)/bin/ds7_maine.elf
BANNER := $(if $(wildcard $(LIBNDS)/icon.bmp),-b $(LIBNDS)/icon.bmp "Durak DS;Swiss minimalism card game;",)

.PHONY: all clean

all: $(TARGET).nds

$(TARGET).elf: $(SRC)
> $(CC) $(CFLAGS) $(SRC) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).nds: $(TARGET).elf
> ndstool -c $@ -9 $< $(if $(wildcard $(ARM7)),-7 $(ARM7),) $(BANNER)

clean:
> rm -f $(TARGET).elf $(TARGET).nds
