# Durak DS - Makefile (devkitARM + libnds + calico)
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
INCLUDE := -I$(LIBNDS)/include $(if $(wildcard $(CALICO)/include),-I$(CALICO)/include,)
CFLAGS  := -Wall -O2 $(ARCH) -mthumb -mthumb-interwork -ffunction-sections -fdata-sections -D__NDS__ -D__ARM9__ -DARM9 $(INCLUDE)
LDFLAGS := -specs=ds_arm9.specs -g $(ARCH) -mthumb -mthumb-interwork -L$(LIBNDS)/lib $(if $(wildcard $(CALICO)/lib),-L$(CALICO)/lib,)
LIBS    := -lnds9 $(if $(wildcard $(CALICO)/lib/libcalico_ds9.a),-lcalico_ds9,)

BANNER := $(if $(wildcard $(LIBNDS)/icon.bmp),-b $(LIBNDS)/icon.bmp "Durak DS;Swiss minimalism card game;",)

# diagnostics (shown at the top of the Build log)
$(info CALICO header: $(wildcard $(CALICO)/include/calico.h))
$(info CALICO libs: $(wildcard $(CALICO)/lib/*.a))
$(info LIBNDS libs: $(wildcard $(LIBNDS)/lib/*.a))

.PHONY: all clean

all: $(TARGET).nds

$(TARGET).elf: $(SRC)
> $(CC) $(CFLAGS) $(SRC) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).nds: $(TARGET).elf
> ndstool -c $@ -9 $< $(BANNER)

clean:
> rm -f $(TARGET).elf $(TARGET).nds
