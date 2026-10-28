# libwebp (decoder subset)

Vendored so the Windows build can decode the WebP box art without depending on the
"Webp Image Extensions" package that WIC needs. Only the decoder is included:
`src/dec`, `src/dsp`, `src/utils` and the two encoder headers that `src/dsp/lossless.h`
pulls in - no encoder sources.

Version: libwebp 1.5.0 (https://github.com/webmproject/libwebp, tag v1.5.0).
Licence: BSD-3-Clause, see COPYING.

Do not edit these files; update by re-vendoring from the upstream tag.

The SSE2/SSE4.1 variants of `src/dsp` are included as well. This is not a speed
decision: the generic dsp files declare `VP8DspInitSSE2()`, `WebPInitAlphaProcessingSSE41()`
and the rest unconditionally, so leaving the SIMD files out produces unresolved externals
at link time. MSVC defines `WEBP_USE_SSE2`/`WEBP_USE_SSE41` for x64 targets by itself
(`_M_X64` in `src/dsp/cpu.h`), and cpu.c still gates every selected entry point on the
run-time CPU flags. The AVX2 and NEON variants are not compiled: the CMake list and the MSVC
project only name the SSE files.
