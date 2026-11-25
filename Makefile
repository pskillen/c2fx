# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# Host archive:         make
# Host tests:           make test
# Decode vectors WAV:   make wav
# ASan and UBSan:       make sanitize
# Cortex-M4 soft-float: make cross-arm
# CK803S:               make cross-csky
# CK803S with DSP asm:  make cross-csky CSKY_DSP=1
#
# model/ is the host float oracle. `make test` and `make wav` build it.
# The archive and the cross builds do not contain it.
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

# Host float model. No -Wdouble-promotion or -Wfloat-conversion: the oracle uses double.
MODEL_CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Werror
MODEL_CPPFLAGS := -Imodel -Isrc/tables -Iinclude
MODEL_SRCS := model/c2m_frame.c model/c2m_quant.c model/c2m_decode.c model/c2m_lpc.c \
	model/c2m_phase.c model/c2m_synth.c
MODEL_OBJS := $(MODEL_SRCS:model/%.c=$(BUILD)/model/%.o)

SRCS := src/c2fx_version.c $(wildcard src/tables/*.c) $(wildcard src/dsp/*.c) \
	src/c2fx_frame.c src/c2fx_quant.c src/c2fx_lpc.c
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

ARM_CFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections
CSKY_CFLAGS := -mcpu=ck803s -Os -ffunction-sections -fdata-sections
ifeq ($(CSKY_DSP),1)
CSKY_CFLAGS += -Wa,-mdsp
endif

.PHONY: all archive test sanitize cross-arm cross-csky clean force wav

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
	printf '%s\n' '$(CC) $(CFLAGS) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $(MODEL_CPPFLAGS)' > $@.tmp
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(BUILD)/tables/%.o: src/tables/%.c src/tables/%.h include/c2fx.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/dsp/%.o: src/dsp/%.c src/dsp/c2fx_dsp.h src/dsp/c2fx_fxp.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -Isrc/dsp -c $< -o $@

$(BUILD)/%.o: src/%.c include/c2fx.h src/c2fx_internal.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -Isrc -Isrc/dsp -c $< -o $@

test: $(BUILD)/test_version $(BUILD)/test_tables $(BUILD)/test_model_frame $(BUILD)/test_model_vectors \
		$(BUILD)/test_model_envelope $(BUILD)/test_model_synth $(BUILD)/test_model_vs_c2dec
	$(BUILD)/test_version
	$(BUILD)/test_tables
	$(BUILD)/test_model_frame
	$(BUILD)/test_model_vectors
	$(BUILD)/test_model_envelope
	$(BUILD)/test_model_synth
	$(BUILD)/test_model_vs_c2dec

$(BUILD)/model/%.o: model/%.c $(wildcard model/*.h) src/tables/c2_3200_tab.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -c $< -o $@

$(BUILD)/test_model_frame: tests/test_model_frame.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_vectors: tests/test_model_vectors.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_envelope: tests/test_model_envelope.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_synth: tests/test_model_synth.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_vs_c2dec: tests/test_model_vs_c2dec.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

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

build/c2m_dec: model/c2m_dec.c $(MODEL_OBJS) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

wav: build/c2m_dec
	mkdir -p build/wav
	@set -eu; \
	for f in vectors/*.c2; do \
		base=$$(basename "$$f" .c2); \
		build/c2m_dec "$$f" "build/wav/$$base.pcm"; \
		python3 tools/pcm_to_wav.py "build/wav/$$base.pcm" "build/wav/$$base.wav"; \
		rm -f "build/wav/$$base.pcm"; \
	done

clean:
	rm -rf build
