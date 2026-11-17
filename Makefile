# Durak DS - minimal Makefile (devkitARM + libnds)

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set")
endif

include $(DEVKITARM)/base_tools

LIBNDS ?= $(DEVKITPRO)/libnds

TARGET := durak
SRC    := main.c

ARCH    := -march=armv5te -mtune=arm946e-s
CFLAGS  := -Wall -O2 $(ARCH) -mthumb -mthumb-interwork -ffunction-sections -fdata-sections -DARM9 -I$(LIBNDS)/include
LDFLAGS := -specs=ds_arm9.specs -g $(ARCH) -mthumb -mthumb-interwork -L$(LIBNDS)/lib
LIBS    := -lnds9

# use the stock libnds icon if it exists
BANNER := $(if $(wildcard $(LIBNDS)/icon.bmp),-b $(LIBNDS)/icon.bmp "Durak DS;Swiss minimalism card game;",)

.PHONY: all clean

all: $(TARGET).nds

$(TARGET).elf: $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) $(LIBS) -o $@

$(TARGET).nds: $(TARGET).elf
	ndstool -c $@ -9 $< $(BANNER)

clean:
	rm -f $(TARGET).elf $(TARGET).nds
