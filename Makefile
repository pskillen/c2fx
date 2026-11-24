# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# Host archive:         make
# Host tests:           make test
# ASan and UBSan:       make sanitize
# Cortex-M4 soft-float: make cross-arm
# CK803S:               make cross-csky
# CK803S with DSP asm:  make cross-csky CSKY_DSP=1
#
# ARM_PREFIX and CSKY_PREFIX are the cross tool prefixes, including the
# trailing hyphen.

ARM_PREFIX ?= arm-none-eabi-
CSKY_PREFIX ?= csky-elf-

CC ?= gcc
AR ?= ar
BUILD ?= build/host

CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Werror -Wdouble-promotion -Wfloat-conversion
EXTRA_CFLAGS ?=
CPPFLAGS := -Iinclude -Isrc/tables

SRCS := src/c2fx_version.c $(wildcard src/tables/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

ARM_CFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections
CSKY_CFLAGS := -mcpu=ck803s -Os -ffunction-sections -fdata-sections
ifeq ($(CSKY_DSP),1)
CSKY_CFLAGS += -Wa,-mdsp
endif

.PHONY: all archive test sanitize cross-arm cross-csky clean force

all: archive

archive: $(BUILD)/libc2fx.a

$(BUILD)/libc2fx.a: $(OBJS)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJS)

# The stamp changes only when the compiler or flags change, so a later
# `make cross-csky CSKY_DSP=1` rebuilds objects left by a plain cross-csky.
force: ;

$(BUILD)/cflags.stamp: force
	mkdir -p $(dir $@)
	printf '%s\n' '$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS)' > $@.tmp
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(BUILD)/%.o: src/%.c include/c2fx.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -c $< -o $@

test: $(BUILD)/test_version $(BUILD)/test_tables
	$(BUILD)/test_version
	$(BUILD)/test_tables

$(BUILD)/test_version: tests/test_version.c $(BUILD)/libc2fx.a include/c2fx.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/libc2fx.a -o $@

$(BUILD)/test_tables: tests/test_tables.c $(BUILD)/libc2fx.a src/tables/c2_3200_tab.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/libc2fx.a -o $@

sanitize:
	$(MAKE) test BUILD=build/sanitize EXTRA_CFLAGS='-fsanitize=address,undefined'

cross-arm:
	$(MAKE) archive BUILD=build/arm CC='$(ARM_PREFIX)gcc' AR='$(ARM_PREFIX)ar' EXTRA_CFLAGS='$(ARM_CFLAGS)'

ifeq ($(CSKY_DSP),1)
CSKY_BUILD := build/csky-dsp
else
CSKY_BUILD := build/csky
endif

cross-csky:
	$(MAKE) archive BUILD=$(CSKY_BUILD) CC='$(CSKY_PREFIX)gcc' AR='$(CSKY_PREFIX)ar' EXTRA_CFLAGS='$(CSKY_CFLAGS)'

clean:
	rm -rf build
