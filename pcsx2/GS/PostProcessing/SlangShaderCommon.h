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

// The Vulkan/OpenGL runtime declarations below need vulkan.h/GL types to be visible first.
//
// Vulkan is routed through PCSX2's own VKLoader.h rather than a raw #include "vulkan/vulkan.h":
// PCSX2 suppresses vulkan.h's real function prototypes project-wide (VK_NO_PROTOTYPES) because it
// loads every entry point itself as a function pointer with the same name (VKEntryPoints.inl), and
// on Windows it also needs VK_USE_PLATFORM_WIN32_KHR defined (plus its own windows.h replacement,
// common/RedtapeWindows.h, included first) to expose the Win32 surface functions. vulkan.h/
// vulkan_core.h are include-guarded, so whichever header reaches them *first* in a given
// translation unit decides these macros for the *whole* TU - a raw, unguarded include here would
// "win" that race in any TU where this header happens to be included before VKLoader.h (e.g.
// GSDeviceVK.cpp, which includes this header alphabetically before its own GSDeviceVK.h), silently
// disabling VK_NO_PROTOTYPES/VK_USE_PLATFORM_WIN32_KHR for the rest of that TU and breaking the
// build with "redefinition" and "missing type specifier" errors on every Vulkan entry point.
// Including VKLoader.h here instead guarantees the correct macros are always set before vulkan.h
// is ever processed, regardless of include order.
#if defined(LIBRA_RUNTIME_VULKAN)
#include "GS/Renderers/Vulkan/VKLoader.h"
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
