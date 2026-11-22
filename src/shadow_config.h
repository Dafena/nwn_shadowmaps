#pragma once

// SHADOW_HIDDEN -- ELF visibility, and ONLY on ELF.
//
// Hidden visibility matters on Linux: this is an LD_PRELOAD library, so an
// internal module API that stays default-visible becomes a process-wide
// interposition symbol.
//
// It means NOTHING on Windows -- a PE exports only what the .def file lists --
// and MinGW warns on every declaration that carries it ("visibility attribute
// not supported in this configuration"). Ten warnings per Windows build, which
// is ten places for a REAL warning to hide. Guarding on _WIN32 rather than
// __GNUC__ is the fix; the attribute is an ELF concept, not a compiler one.
//
// Reported by a tester's toolchain on 2026-08-15 during a forced clean rebuild,
// where the noise obscured the actual build state.
#if defined(__GNUC__) && !defined(_WIN32)
#  define SHADOW_HIDDEN __attribute__((visibility("hidden")))
#else
#  define SHADOW_HIDDEN
#endif

// All NWN_SHADOWMAP_* lookups go through this read-only, memoised accessor.
SHADOW_HIDDEN const char* shadow_getenv(const char* name);
SHADOW_HIDDEN unsigned shadow_default_env_count();
