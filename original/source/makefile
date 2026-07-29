# My attempt to produce a working makefile for the CCSDS image compression software from U. Nebraska

# Compiler:
CC = gcc

# -ffp-contract=off: pin IEEE754-strict, per-operator floating-point
# semantics (no fused multiply-add) on every target. Without this, the
# compiler's FP_CONTRACT default is free to fuse a*b+c into a single
# rounding step wherever hardware FMA is available. x86-64 baseline
# targets have no FMA unit, so this was silently a no-op there; on
# ARM64 (e.g. Apple Silicon, where FMA is part of the base ISA) it
# isn't, and it measurably diverges from bpe-rs -- which never
# auto-fuses basic arithmetic operators -- across nearly every float
# DWT case, forward and inverse alike (found via cross-architecture
# verification on an Apple M5 Mac; see COMPATIBILITY_REPORT.md §3.10).
BPEFLAGS = -O2 -c -Wall -ffp-contract=off

BPEFILES = AC_BitPlaneCoding.c StagesCodingGaggles.c header.c AdjustOutput.c bitsIO.c lifting_97M.c BPEBlockCoding.c bpe_decoder.c lifting_97f.c CoeffGroup.c bpe_encoder.c main.c DC_EnDeCoding.c errorhandle.c ricecoding.c PatternCoding.c getopt.c waveletbpe.c

bpe: ${BPEFILES}

bpe: ${BPEFILES}
	${CC} ${BPEFLAGS} ${BPEFILES}
	${CC} *.o -o bpe -lm
	rm AC_BitPlaneCoding.o DC_EnDeCoding.o bpe_decoder.o header.o ricecoding.o AdjustOutput.o PatternCoding.o bpe_encoder.o lifting_97M.o waveletbpe.o BPEBlockCoding.o StagesCodingGaggles.o errorhandle.o lifting_97f.o CoeffGroup.o bitsIO.o getopt.o main.o


# Trace build: identical to `bpe` except for -DBPE_TRACE, which enables a
# handful of #ifdef-guarded dumps (see verify/compare_traces.py in the parent
# repo) gated at runtime by the BPE_TRACE_DIR env var. The plain `bpe` target
# above is never built with this flag, so its output stays byte-for-byte
# unaffected by this instrumentation.
BPEFLAGS_TRACE = -O2 -c -Wall -DBPE_TRACE -ffp-contract=off

bpe_trace: ${BPEFILES}
	${CC} ${BPEFLAGS_TRACE} ${BPEFILES}
	${CC} *.o -o bpe_trace -lm
	rm AC_BitPlaneCoding.o DC_EnDeCoding.o bpe_decoder.o header.o ricecoding.o AdjustOutput.o PatternCoding.o bpe_encoder.o lifting_97M.o waveletbpe.o BPEBlockCoding.o StagesCodingGaggles.o errorhandle.o lifting_97f.o CoeffGroup.o bitsIO.o getopt.o main.o


# Linker:
#LDFLAGS = -lm

# Default rule
