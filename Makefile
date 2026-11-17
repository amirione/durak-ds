# Durak DS - Makefile on official devkitPro rules (ds_rules)
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set")
endif

# ds_rules reads LIBPATHS while it is included, so define it first
LIBPATHS := -L$(DEVKITPRO)/libnds/lib

include $(DEVKITARM)/ds_rules

# ds_rules uses tab recipes, so switch the prefix only after the include
.RECIPEPREFIX := >

TARGET         := durak
GAME_TITLE     := Durak DS
GAME_SUBTITLE1 := Swiss minimalism card game

ARCH     := -march=armv5te -mtune=arm946e-s
INCLUDE  := -I$(LIBNDS)/include
CFLAGS   := -g -Wall -O2 -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE) -DARM9
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lnds9

export LD := $(CC)
OFILES   := main.o
OUTPUT   := $(CURDIR)/$(TARGET)
export DEPSDIR := $(CURDIR)

.PHONY: all clean
all: $(OUTPUT).nds

$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

clean:
> rm -f $(OFILES) $(OUTPUT).elf $(OUTPUT).nds $(TARGET).map $(TARGET).lst main.d
