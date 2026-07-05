// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/PostProcessing/SlangShaderChain.h"
#include "GS/PostProcessing/SlangShaderCommon.h"

#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSTexture.h"
#include "GS/GS.h"
#include "Config.h"
#include "Host.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "IconsFontAwesome.h"

#include <mutex>

std::unique_ptr<SlangShaderChain> g_slang_shader_chain;

namespace
{
	std::once_flag s_load_once_flag;
	libra_instance_t s_instance = {};
} // namespace

bool SlangShader::IsAvailable()
{
	std::call_once(s_load_once_flag, []() {
		s_instance = librashader_load_instance();
		if (s_instance.instance_loaded)
		{
			Console.WriteLn("librashader loaded (ABI %zu, API %zu).",
				static_cast<size_t>(s_instance.instance_abi_version()), static_cast<size_t>(s_instance.instance_api_version()));
		}
		else
		{
			ERROR_LOG("Failed to load librashader (library not found, or ABI mismatch with LIBRASHADER_CURRENT_ABI={}); "
					  "slang shader presets are unavailable.",
				static_cast<size_t>(LIBRASHADER_CURRENT_ABI));
		}
	});
	return s_instance.instance_loaded;
}

const libra_instance_t& SlangShader::GetInstance()
{
	// Make sure the loader has actually run, in case a caller skipped IsAvailable().
	IsAvailable();
	return s_instance;
}

void SlangShader::ConsumeError(libra_error_t err, Error* error)
{
	if (!err)
		return;

	const libra_instance_t& inst = GetInstance();
	char* message = nullptr;
	if (inst.error_write && inst.error_write(err, &message) == 0 && message)
	{
		if (error)
			Error::SetStringView(error, message);
		else
			Console.Error("librashader error: %s", message);

		if (inst.error_free_string)
			inst.error_free_string(&message);
	}
	else
	{
		Error::SetStringView(error, "Unknown librashader error.");
	}

	if (inst.error_free)
		inst.error_free(&err);
}

static std::string ResolveShaderPath(const std::string& preset_path)
{
	if (preset_path.empty() || Path::IsAbsolute(preset_path))
		return preset_path;

	return Path::Combine(EmuFolders::Shaders, preset_path);
}

static LIBRA_PRESET_CTX_RUNTIME GetPresetCtxRuntime()
{
	switch (g_gs_device->GetRenderAPI())
	{
		case RenderAPI::D3D11:
			return LIBRA_PRESET_CTX_RUNTIME_D3D11;
		case RenderAPI::D3D12:
			return LIBRA_PRESET_CTX_RUNTIME_D3D12;
		case RenderAPI::Vulkan:
			return LIBRA_PRESET_CTX_RUNTIME_VULKAN;
		case RenderAPI::OpenGL:
			return LIBRA_PRESET_CTX_RUNTIME_GL_CORE;
		case RenderAPI::Metal:
			return LIBRA_PRESET_CTX_RUNTIME_METAL;
		default:
			return LIBRA_PRESET_CTX_RUNTIME_NONE;
	}
}

/// Applies "name=value;name=value" runtime parameter overrides to a freshly-created preset, or
/// (via SlangShaderChain::SetParameter) to a live chain. Failures are logged but non-fatal - one
/// bad override shouldn't prevent the rest of the preset from loading.
template <typename ApplyFn>
static void ForEachParameterOverride(const std::string& overrides, const ApplyFn& apply)
{
	for (const std::string_view& kv : StringUtil::SplitString(overrides, ';', true))
	{
		const std::string_view::size_type eq = kv.find('=');
		if (eq == std::string_view::npos)
			continue;

		const std::string name(kv.substr(0, eq));
		const std::optional<float> value = StringUtil::FromChars<float>(kv.substr(eq + 1));
		if (!value.has_value())
		{
			Console.Warning("Ignoring malformed slang shader parameter override '%.*s'.",
				static_cast<int>(kv.length()), kv.data());
			continue;
		}

		apply(name, value.value());
	}
}

std::unique_ptr<SlangShaderChain> SlangShaderChain::Create(const std::string& preset_path, Error* error)
{
	if (!SlangShader::IsAvailable())
	{
		Error::SetStringView(error, "librashader could not be loaded; slang shader presets are unavailable.");
		return nullptr;
	}

	const libra_instance_t& inst = SlangShader::GetInstance();
	const std::string resolved_path = ResolveShaderPath(preset_path);

	libra_preset_ctx_t preset_ctx = nullptr;
	if (libra_error_t err = inst.preset_ctx_create(&preset_ctx))
	{
		SlangShader::ConsumeError(err, error);
		return nullptr;
	}

	inst.preset_ctx_set_runtime(&preset_ctx, GetPresetCtxRuntime());
	inst.preset_ctx_set_core_name(&preset_ctx, "PCSX2");

	libra_shader_preset_t preset = nullptr;
	if (libra_error_t err = inst.preset_create_with_context(resolved_path.c_str(), &preset_ctx, &preset))
	{
		inst.preset_ctx_free(&preset_ctx);
		SlangShader::ConsumeError(err, error);
		return nullptr;
	}
	inst.preset_ctx_free(&preset_ctx);

	ForEachParameterOverride(GSConfig.SlangShaderParameters, [&inst, &preset](const std::string& name, float value) {
		if (libra_error_t err = inst.preset_set_param(&preset, name.c_str(), value))
			SlangShader::ConsumeError(err, nullptr);
	});

	// A successful CreateSlangFilterChain() consumes/invalidates the preset handle; on failure it
	// does not, so we must free it ourselves.
	void* chain = nullptr;
	if (!g_gs_device->CreateSlangFilterChain(preset, &chain, error))
	{
		inst.preset_free(&preset);
		return nullptr;
	}

	std::unique_ptr<SlangShaderChain> result(new SlangShaderChain());
	result->m_preset_path = preset_path;
	result->m_chain = chain;
	return result;
}

bool SlangShaderChain::EnumerateParameters(const std::string& preset_path, std::vector<SlangShaderParam>* params, Error* error)
{
	if (!SlangShader::IsAvailable())
	{
		Error::SetStringView(error, "librashader could not be loaded; slang shader presets are unavailable.");
		return false;
	}

	const libra_instance_t& inst = SlangShader::GetInstance();
	const std::string resolved_path = ResolveShaderPath(preset_path);

	libra_shader_preset_t preset = nullptr;
	if (libra_error_t err = inst.preset_create(resolved_path.c_str(), &preset))
	{
		SlangShader::ConsumeError(err, error);
		return false;
	}

	libra_preset_param_list_t list = {};
	if (libra_error_t err = inst.preset_get_runtime_params(&preset, &list))
	{
		SlangShader::ConsumeError(err, error);
		inst.preset_free(&preset);
		return false;
	}

	if (params)
	{
		params->clear();
		params->reserve(static_cast<size_t>(list.length));
		for (u64 i = 0; i < list.length; i++)
		{
			const libra_preset_param_t& p = list.parameters[i];
			params->push_back(SlangShaderParam{
				std::string(p.name ? p.name : ""),
				std::string(p.description ? p.description : ""),
				p.initial, p.minimum, p.maximum, p.step});
		}
	}

	inst.preset_free_runtime_params(list);
	inst.preset_free(&preset);
	return true;
}

SlangShaderChain::~SlangShaderChain()
{
	ReleaseDeviceResources();
}

GSTexture* SlangShaderChain::Apply(GSTexture* input, const GSVector2i& window_size, const GSVector4i& viewport_rect,
	float aspect_ratio, float frames_per_second, bool advance_frame)
{
	if (!m_chain || m_errored || !input)
		return nullptr;

	if (!g_gs_device->ResizeRenderTarget(&m_output, window_size.x, window_size.y, false, false))
	{
		m_errored = true;
		return nullptr;
	}

	g_gs_device->ClearRenderTarget(m_output, 0);

	SlangFrameOptions options = {};
	options.aspect_ratio = aspect_ratio;
	options.frames_per_second = frames_per_second;
	options.frame_direction = 1; // PCSX2 has no rewind support.
	options.rotation = 0; // PS2 output is never rotated.
	options.clear_history = m_first_frame;

	if (!g_gs_device->DoSlangFilterChainFrame(m_chain, m_frame_count, input, m_output, viewport_rect, options))
	{
		if (!m_errored)
		{
			m_errored = true;
			Host::AddIconOSDMessage("SlangShaderFrame", ICON_FA_TRIANGLE_EXCLAMATION,
				TRANSLATE_SV("GS", "Failed to render the slang shader preset; it will be disabled until reloaded."),
				Host::OSD_ERROR_DURATION);
		}
		return nullptr;
	}

	// librashader just wrote the real shaded output directly into m_output's native texture/image
	// handle, entirely bypassing GSDevice's own render-target bind path - so m_output is still
	// marked State::Cleared from the ClearRenderTarget() call above (nothing about the backend call
	// touches PCSX2's own texture-state tracking). If left as Cleared, the *next* time m_output is
	// used as a source texture (PresentRect -> DoStretchRect, on every backend), CommitClear() sees
	// State::Cleared and "resolves" it by issuing a real clear-to-black immediately before sampling,
	// silently overwriting the shaded content that was just rendered with flat black - no error,
	// just a solid black frame. Mark it Dirty now so that lazy-clear resolution doesn't fire.
	m_output->SetState(GSTexture::State::Dirty);

	m_first_frame = false;
	if (advance_frame)
		m_frame_count++;

	return m_output;
}

bool SlangShaderChain::SetParameter(const std::string& name, float value)
{
	if (!m_chain)
		return false;

	return g_gs_device->SetSlangFilterChainParam(m_chain, name.c_str(), value);
}

void SlangShaderChain::ApplyParameterOverrides()
{
	ForEachParameterOverride(GSConfig.SlangShaderParameters, [this](const std::string& name, float value) {
		SetParameter(name, value);
	});
}

void SlangShaderChain::ReleaseDeviceResources()
{
	if (m_chain)
	{
		g_gs_device->DestroySlangFilterChain(m_chain);
		m_chain = nullptr;
	}

	if (m_output)
	{
		g_gs_device->Recycle(m_output);
		m_output = nullptr;
	}
}
