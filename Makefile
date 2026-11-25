# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# Host archive:         make
# External DSP archive: make archive EXTERNAL_DSP=1
# Host tests:           make test
# Refresh bench CRCs:   make crc
# Decode vectors WAV:   make wav
# Fixed decoder WAV:    make wavfx
# Encode test PCM:      make enc
# ASan and UBSan:       make sanitize
# Cortex-M4 soft-float: make cross-arm
# CK803S:               make cross-csky
# CK803S with DSP asm:  make cross-csky CSKY_DSP=1
#
# model/ is the host float oracle. `make test` and `make wav` build it.
# The archive and the cross builds do not contain it.
#
# build/c2m_enc <in.pcm> <out.c2> encodes one file: int16 LE, 8 kHz, mono, a
# whole number of 160-sample frames. `make enc` runs it on every test voice and
# signal file and writes build/enc/<set>.c2. build/c2m_dec is the reverse.
# build/c2fx_dec is the fixed-point decoder; `make wavfx` writes build/wavfx/.
#
# ARM_PREFIX and CSKY_PREFIX are the cross tool prefixes, including the
# trailing hyphen.

ARM_PREFIX ?= arm-none-eabi-
CSKY_PREFIX ?= csky-elf-

CC ?= gcc
AR ?= ar
NM ?= nm
ifeq ($(EXTERNAL_DSP),1)
BUILD ?= build/host-external-dsp
else
BUILD ?= build/host
endif

CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Werror -Wdouble-promotion -Wfloat-conversion
EXTRA_CFLAGS ?=
CPPFLAGS := -Iinclude -Isrc/tables

ifeq ($(origin C2FX_VERSION),undefined)
C2FX_VERSION := $(shell git describe --tags --exact-match 2>/dev/null || \
	git rev-parse --short HEAD 2>/dev/null || echo local)
endif
export C2FX_VERSION
VERSION := $(patsubst v%,%,$(C2FX_VERSION))
VERSION_CASES := local 012abcd 1.2.3 1.2.3-rc.4

# Host float model. No -Wdouble-promotion or -Wfloat-conversion: the oracle uses double.
MODEL_CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Werror
MODEL_CPPFLAGS := -Imodel -Isrc/tables -Iinclude
MODEL_SRCS := model/c2m_frame.c model/c2m_quant.c model/c2m_decode.c model/c2m_lpc.c \
	model/c2m_phase.c model/c2m_synth.c model/c2m_fft.c model/c2m_analysis.c \
	model/c2m_pitch.c model/c2m_voicing.c model/c2m_encode.c
MODEL_OBJS := $(MODEL_SRCS:model/%.c=$(BUILD)/model/%.o)

BUILTIN_DSP_SRCS := src/dsp/c2fx_fft.c src/dsp/c2fx_math.c src/dsp/c2fx_tables.c
ifeq ($(EXTERNAL_DSP),1)
HOST_DSP_CPPFLAGS := $(CPPFLAGS)
CPPFLAGS += -DC2FX_EXTERNAL_DSP
HOST_DSP_OBJS := $(BUILTIN_DSP_SRCS:src/dsp/%.c=$(BUILD)/host-dsp/%.o)
endif

SRCS := src/c2fx_version.c $(wildcard src/tables/*.c) $(wildcard src/dsp/*.c) \
	src/c2fx_frame.c src/c2fx_quant.c src/c2fx_lpc.c src/c2fx_envelope.c \
	src/c2fx_phase.c src/c2fx_synth.c src/c2fx_lost.c src/c2fx_decode.c
ifeq ($(EXTERNAL_DSP),1)
SRCS := $(filter-out $(BUILTIN_DSP_SRCS),$(SRCS))
endif
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

ifeq ($(BUILD),build/host)
CRC_TOOL := build/c2fx_crc
else
CRC_TOOL := $(BUILD)/c2fx_crc
endif

ARM_CFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections
CSKY_CFLAGS := -mcpu=ck803s -Os -ffunction-sections -fdata-sections
ifeq ($(CSKY_DSP),1)
CSKY_CFLAGS += -Wa,-mdsp
endif

.PHONY: all archive test sanitize cross-arm cross-csky clean force wav wavfx enc \
	crc test-crc test-external-dsp check-external-dsp

all: archive

archive: $(BUILD)/libc2fx.a

$(BUILD)/libc2fx.a: $(OBJS)
	mkdir -p $(dir $@)
	rm -f $@.tmp
	$(AR) rcs $@.tmp $(OBJS)
	mv $@.tmp $@

# The stamp changes when the compiler, flags, or version change, so a later
# `make cross-csky CSKY_DSP=1` rebuilds objects left by a plain cross-csky.
force: ;

$(BUILD)/cflags.stamp: force
	mkdir -p $(dir $@)
	printf '%s\n' '$(CC) $(CFLAGS) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $(MODEL_CPPFLAGS) $(VERSION)' > $@.tmp
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(BUILD)/tables/%.o: src/tables/%.c src/tables/%.h include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/dsp/%.o: src/dsp/%.c src/dsp/c2fx_dsp.h src/dsp/c2fx_fxp.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -Isrc/dsp -c $< -o $@

$(BUILD)/host-dsp/%.o: src/dsp/%.c src/dsp/c2fx_dsp.h src/dsp/c2fx_fxp.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(HOST_DSP_CPPFLAGS) -Isrc/dsp -c $< -o $@

$(BUILD)/c2fx_version.o: src/c2fx_version.c include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) '-DC2FX_VERSION="$(VERSION)"' -c $< -o $@

$(BUILD)/%.o: src/%.c include/c2fx.h include/c2fx_types.h src/c2fx_internal.h src/dsp/c2fx_fxp.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) -Isrc -Isrc/dsp -c $< -o $@

test: $(BUILD)/test_version $(addprefix $(BUILD)/test-version/,$(VERSION_CASES)) \
		$(BUILD)/test_tables $(BUILD)/test_model_frame $(BUILD)/test_model_vectors \
		$(BUILD)/test_model_envelope $(BUILD)/test_model_synth $(BUILD)/test_model_vs_c2dec \
		$(BUILD)/test_dsp $(BUILD)/test_fx_frame $(BUILD)/test_fx_envelope \
		$(BUILD)/test_fx_decode $(BUILD)/test_fx_robust \
		$(BUILD)/test_model_quant_enc $(BUILD)/test_model_encoder $(BUILD)/test_model_rules \
		$(BUILD)/test_lsd_helper_self test-external-dsp test-crc
	$(BUILD)/test_version
	@set -eu; for version in $(VERSION_CASES); do \
		actual=$$($(BUILD)/test-version/$$version); \
		test "$$actual" = "c2fx version: $$version"; \
	done
	$(BUILD)/test_tables
	$(BUILD)/test_model_frame
	$(BUILD)/test_model_vectors
	$(BUILD)/test_model_envelope
	$(BUILD)/test_model_synth
	$(BUILD)/test_model_vs_c2dec
	$(BUILD)/test_dsp
	$(BUILD)/test_fx_frame
	$(BUILD)/test_fx_envelope
	$(BUILD)/test_fx_decode
	$(BUILD)/test_fx_robust
	$(BUILD)/test_model_quant_enc
	$(BUILD)/test_model_rules
	$(BUILD)/test_model_encoder
	$(BUILD)/test_lsd_helper_self

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

$(BUILD)/test_lsd_helper.o: tests/test_lsd_helper.c tests/test_lsd_helper.h $(wildcard model/*.h) $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Itests -c $< -o $@

$(BUILD)/test_model_vs_c2dec: tests/test_model_vs_c2dec.c tests/test_lsd_helper.h $(BUILD)/test_lsd_helper.o $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Itests $< $(BUILD)/test_lsd_helper.o $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_quant_enc: tests/test_model_quant_enc.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_rules: tests/test_model_rules.c $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_lsd_helper_self: tests/test_lsd_helper_self.c tests/test_lsd_helper.h $(BUILD)/test_lsd_helper.o $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Itests $< $(BUILD)/test_lsd_helper.o $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_model_encoder: tests/test_model_encoder.c tests/test_lsd_helper.h $(BUILD)/test_lsd_helper.o $(MODEL_OBJS) $(wildcard model/*.h) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Itests $< $(BUILD)/test_lsd_helper.o $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_version: tests/test_version.c $(BUILD)/libc2fx.a include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/libc2fx.a -o $@

$(BUILD)/test-version/%.o: src/c2fx_version.c include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) '-DC2FX_VERSION="$*"' -c $< -o $@

$(BUILD)/test-version/%: tests/test_version.c $(BUILD)/test-version/%.o include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/test-version/$*.o -o $@

$(BUILD)/test_tables: tests/test_tables.c $(BUILD)/libc2fx.a src/tables/c2_3200_tab.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/libc2fx.a -o $@

$(BUILD)/test_dsp: tests/test_dsp.c $(BUILD)/libc2fx.a src/dsp/c2fx_dsp.h src/dsp/c2fx_fxp.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) -Isrc/dsp $< $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_fx_frame: tests/test_fx_frame.c $(BUILD)/libc2fx.a $(MODEL_OBJS) src/c2fx_internal.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Isrc $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_fx_envelope: tests/test_fx_envelope.c $(BUILD)/libc2fx.a $(MODEL_OBJS) src/c2fx_internal.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Isrc -Isrc/dsp $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

$(BUILD)/test_fx_decode: tests/test_fx_decode.c $(BUILD)/libc2fx.a $(MODEL_OBJS) $(HOST_DSP_OBJS) include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Isrc $< $(MODEL_OBJS) $(BUILD)/libc2fx.a $(HOST_DSP_OBJS) -lm -o $@

test-external-dsp: $(BUILD)/test_fx_decode
	$(MAKE) check-external-dsp EXTERNAL_DSP=1 BUILD=$(BUILD)-external-dsp REFERENCE_BUILD=$(BUILD)

check-external-dsp: $(BUILD)/libc2fx.a
	$(NM) --defined-only $< > $(BUILD)/defined-symbols.txt
	@awk '$$NF ~ /^c2fx_(rfft512|irfft512|cos|log2_q11|exp2_q11)$$/ { \
		print "external DSP archive defines " $$NF; bad = 1 } END { exit bad }' $(BUILD)/defined-symbols.txt
	$(MAKE) $(BUILD)/test_fx_decode $(BUILD)/expected_crc.txt
	$(REFERENCE_BUILD)/test_fx_decode --pcm > $(BUILD)/reference.pcm
	$(BUILD)/test_fx_decode --pcm > $(BUILD)/external.pcm
	cmp $(BUILD)/reference.pcm $(BUILD)/external.pcm
	diff -u vectors/expected_crc.txt $(BUILD)/expected_crc.txt
	@printf '%s\n' 'external DSP: D3 PCM and vector CRCs match'

$(BUILD)/test_fx_robust: tests/test_fx_robust.c $(BUILD)/libc2fx.a $(MODEL_OBJS) include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) -Isrc $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

sanitize:
	$(MAKE) test BUILD=build/sanitize EXTRA_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -DC2FX_ROBUST_FRAMES=1000000'

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

build/c2m_enc: model/c2m_enc.c $(MODEL_OBJS) $(BUILD)/libc2fx.a $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(MODEL_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_CPPFLAGS) $< $(MODEL_OBJS) $(BUILD)/libc2fx.a -lm -o $@

enc: build/c2m_enc
	mkdir -p build/enc
	@set -eu; \
	for f in tests/data/voice/*.pcm tests/data/signals/*.pcm; do \
		base=$$(basename "$$f" .pcm); \
		build/c2m_enc "$$f" "build/enc/$$base.c2"; \
	done

build/c2fx_dec: host/c2fx_dec.c host/c2_header.h $(BUILD)/libc2fx.a include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/libc2fx.a -o $@

$(CRC_TOOL): host/c2fx_crc.c host/c2_header.h $(BUILD)/libc2fx.a $(HOST_DSP_OBJS) include/c2fx.h include/c2fx_types.h $(BUILD)/cflags.stamp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS) $< $(BUILD)/libc2fx.a $(HOST_DSP_OBJS) -o $@

$(BUILD)/expected_crc.txt: $(CRC_TOOL) $(wildcard vectors/*.c2) Makefile force
	@set -eu; export LC_ALL=C; \
	{ printf '%s\n' '# SPDX-License-Identifier: MIT' '# Copyright (c) 2026 The c2fx Authors' \
		'# Decoder CRC-32 of little-endian PCM. Regenerate with make crc.'; \
		$(CRC_TOOL) vectors/*.c2; } > $@.tmp
	mv $@.tmp $@

crc: $(BUILD)/expected_crc.txt
	cp $< vectors/expected_crc.txt

test-crc: $(BUILD)/expected_crc.txt
	$(CRC_TOOL) --self-test
	diff -u vectors/expected_crc.txt $<

wavfx: build/c2fx_dec
	mkdir -p build/wavfx
	@set -eu; \
	for f in vectors/*.c2; do \
		base=$$(basename "$$f" .c2); \
		build/c2fx_dec "$$f" "build/wavfx/$$base.pcm"; \
		python3 tools/pcm_to_wav.py "build/wavfx/$$base.pcm" "build/wavfx/$$base.wav"; \
		rm -f "build/wavfx/$$base.pcm"; \
	done

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
