.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error Set DEVKITARM to your devkitARM directory)
endif
include $(DEVKITARM)/ds_rules
# This project does not use LTO; use nm directly (gcc-nm is broken on some Windows SDKs).
export NM := $(PREFIX)nm

TARGET := DSiDrop
BUILD := build/obj
SOURCES := source
INCLUDES := include
ARCH := -march=armv5te -mtune=arm946e-s -mthumb
SOURCE_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
CFLAGS := -g -Wall -Wextra -Werror -Wstack-usage=4096 -fstack-usage -O2 -ffunction-sections -fdata-sections $(ARCH)
CFLAGS += -ffile-prefix-map=$(SOURCE_ROOT)=. -fdebug-prefix-map=$(SOURCE_ROOT)=.
CFLAGS += $(INCLUDE) -DARM9 $(EXTRA_CFLAGS)
LDFLAGS = -specs=ds_arm9.specs -g $(ARCH) -Wl,--gc-sections,-Map,$(notdir $*.map)
LIBS := -ldswifi9 -lfat -lnds9
LIBDIRS := $(LIBNDS)
export GAME_TITLE := DSiDrop

ifneq ($(notdir $(BUILD)),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export GAME_ICON := $(CURDIR)/assets/icon.bmp
export GAME_LICENSES := $(CURDIR)/licenses
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(CURDIR)/tests
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
ifneq ($(findstring -DROMSFINDER_CLIENT_TEST,$(EXTRA_CFLAGS)),)
CFILES += client_network_test.c
endif
ifneq ($(findstring -DROMSFINDER_COVER_TEST,$(EXTRA_CFLAGS)),)
CFILES += cover_prefetch_test.c
endif
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: all $(BUILD) clean
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	rm -rf -- $(BUILD) $(TARGET).elf $(TARGET).nds
else
$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).nds: $(GAME_ICON) $(wildcard $(GAME_LICENSES)/*.txt)
	@ndstool -c "$@" -9 "$(OUTPUT).elf" $(_ARM7_ELF) -d "$(GAME_LICENSES)" -b "$(GAME_ICON)" "$(GAME_TITLE)" -g "####" "00" "$(GAME_TITLE)" $(_ADDFILES)
$(OUTPUT).elf: $(OFILES)
-include $(DEPSDIR)/*.d
endif
