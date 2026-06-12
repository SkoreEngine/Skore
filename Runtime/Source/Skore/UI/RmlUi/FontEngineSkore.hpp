#pragma once

#include <RmlUi/Core/FontEngineInterface.h>

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class FontEngineSkore : public Rml::FontEngineInterface
	{
	public:
		FontEngineSkore() = default;
		~FontEngineSkore() override;

		void RegisterFont(StringView family, RID fontResource);

		void Shutdown() override;

		Rml::FontFaceHandle     GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size) override;
		const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;

		int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& text_shaping_context, Rml::Character prior_character) override;
		int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle face_handle, Rml::FontEffectsHandle font_effects_handle, Rml::StringView string,
		                   Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& text_shaping_context,
		                   Rml::TexturedMeshList& mesh_list) override;

		int  GetVersion(Rml::FontFaceHandle handle) override;
		void ReleaseFontResources() override;
	};
}
