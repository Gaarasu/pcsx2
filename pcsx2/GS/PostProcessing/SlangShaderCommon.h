// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// Single point of inclusion for the vendored librashader C headers (see
// 3rdparty/librashader/README.PCSX2.md). librashader_ld.h/librashader.h only
// declare a runtime's types and libra_instance_t members when the matching
// LIBRA_RUNTIME_* macro is defined *before* inclusion, and that macro set
// changes the memory layout of libra_instance_t - so every translation unit
// that touches librashader must go through this header with an identical
// macro set, or the instance struct will silently mismatch across TUs.
//
// Platform runtime sets mirror PCSX2's renderer availability
// (ENABLE_OPENGL/ENABLE_VULKAN are defined by the build system per-platform;
// see cmake/BuildParameters.cmake and pcsx2.vcxproj):
//   - Windows: D3D11 + D3D12 + Vulkan + OpenGL
//   - Linux:   Vulkan + OpenGL
//   - macOS:   Metal + Vulkan
#ifdef _WIN32
#define LIBRA_RUNTIME_D3D11 1
#define LIBRA_RUNTIME_D3D12 1
#endif
#ifdef ENABLE_VULKAN
#define LIBRA_RUNTIME_VULKAN 1
#endif
#ifdef ENABLE_OPENGL
#define LIBRA_RUNTIME_OPENGL 1
#endif
#if defined(__APPLE__)
#define LIBRA_RUNTIME_METAL 1
#endif

// The Vulkan/OpenGL runtime declarations below need vulkan.h/GL types to be
// visible first; pull in what PCSX2 already uses for each API.
#if defined(LIBRA_RUNTIME_VULKAN)
#include "vulkan/vulkan.h"
#endif
#if defined(LIBRA_RUNTIME_OPENGL)
#include "glad/gl.h"
#endif

#include "librashader_ld.h"

class Error;

namespace SlangShader
{
	/// Loads librashader once per process (guarded by a std::once_flag). Returns false if the
	/// library could not be found, or it failed the LIBRASHADER_CURRENT_ABI check. Safe to call
	/// from any thread; safe to call repeatedly.
	bool IsAvailable();

	/// The loaded (or null-stubbed) function table. Check IsAvailable() first; the null instance's
	/// functions are safe to call (they return errors), but IsAvailable() is how callers should
	/// decide whether to surface "feature unavailable" to the user.
	const libra_instance_t& GetInstance();

	/// Converts a libra_error_t into a description string via libra_error_write()/
	/// libra_error_free_string(), frees the error itself with libra_error_free(), and stores the
	/// resulting message into `error` (may be null, in which case the error is just freed).
	void ConsumeError(libra_error_t err, Error* error);
} // namespace SlangShader
