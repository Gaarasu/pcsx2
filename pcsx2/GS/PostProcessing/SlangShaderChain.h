// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GSVector.h"
#include "common/Pcsx2Defs.h"

#include <memory>
#include <string>
#include <vector>

class GSTexture;
class Error;

/// One #pragma parameter as reported by libra_preset_get_runtime_params().
struct SlangShaderParam
{
	std::string name; // libra_preset_param_t::name
	std::string description; // libra_preset_param_t::description
	float initial; // ::initial
	float minimum; // ::minimum
	float maximum; // ::maximum
	float step; // ::step
};

/// Backend-agnostic manager for a single active librashader slang shader preset. Owns the preset
/// handle, the per-backend chain handle (opaque, dispatched through GSDevice's virtuals), the
/// frame counter, and all error latching. Lives on the GS thread alongside g_gs_device.
///
/// As of this change, no GSDevice backend implements the real chain hooks yet (those land in
/// later changes); Create() will always fail with an OSD-surfaced error, and the emulator
/// continues presenting the unshaded frame.
class SlangShaderChain
{
public:
	~SlangShaderChain();

	/// GS thread only. Resolves the path (absolute, or relative to EmuFolders::Shaders), creates
	/// the librashader preset with a wildcard context set for the current RenderAPI, applies
	/// GSConfig.SlangShaderParameters overrides, then asks the active GSDevice to build the
	/// backend-specific chain. Returns nullptr and populates `error` on any failure (library
	/// missing, preset parse failure, or backend not supporting slang shaders yet).
	static std::unique_ptr<SlangShaderChain> Create(const std::string& preset_path, Error* error);

	/// Any thread, does not require a GSDevice (e.g. for a UI parameter dialog in a later change):
	/// loads the preset read-only and reports its runtime parameters.
	static bool EnumerateParameters(const std::string& preset_path, std::vector<SlangShaderParam>* params, Error* error);

	__fi const std::string& GetPresetPath() const { return m_preset_path; }
	__fi u64 GetFrameCount() const { return m_frame_count; }
	__fi bool IsErrored() const { return m_errored; }

	/// Runs the chain. `input` is the frame after CAS; the output is an internally managed
	/// window-sized render target. `viewport_rect` is the letterboxed draw rectangle in
	/// top-left-origin window space. `advance_frame` is true from VSync(), false from
	/// PresentCurrentFrame() redraws. Returns the output texture, or nullptr if the chain
	/// produced no output (caller should then present `input` unshaded).
	GSTexture* Apply(GSTexture* input, const GSVector2i& window_size, const GSVector4i& viewport_rect,
		float aspect_ratio, float frames_per_second, bool advance_frame);

	/// GS thread. Live parameter update - no chain rebuild required.
	bool SetParameter(const std::string& name, float value);

	/// GS thread. Re-parses GSConfig.SlangShaderParameters ("name=value;name=value") and applies
	/// each override via SetParameter(). Called from GSUpdateConfig() when only the parameter
	/// string changed (a full preset reload is not necessary).
	void ApplyParameterOverrides();

	/// Frees the intermediate output target and the backend chain. Must run before
	/// g_gs_device->Destroy() during device teardown/reopen.
	void ReleaseDeviceResources();

private:
	SlangShaderChain() = default;

	std::string m_preset_path;
	void* m_chain = nullptr; // backend-specific libra_*_filter_chain_t (opaque)
	GSTexture* m_output = nullptr; // window-sized RT, GSTexture::Format::Color
	u64 m_frame_count = 0;
	bool m_first_frame = true; // clear_history is set for exactly the first frame after creation
	bool m_errored = false; // latched on first frame failure until the preset is reloaded
};

/// GS-thread global, created/destroyed by GSUpdateConfig() and the GS device open/close paths.
extern std::unique_ptr<SlangShaderChain> g_slang_shader_chain;
