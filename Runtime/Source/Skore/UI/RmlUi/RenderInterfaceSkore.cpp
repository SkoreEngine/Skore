#include "RenderInterfaceSkore.hpp"

namespace Skore
{
	Rml::CompiledGeometryHandle RenderInterfaceSkore::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
	{
		return {};
	}

	void RenderInterfaceSkore::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {}
	void RenderInterfaceSkore::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {}

	Rml::TextureHandle RenderInterfaceSkore::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
	{
		return {};
	}

	Rml::TextureHandle RenderInterfaceSkore::GenerateTexture(Rml::Span<const unsigned char> source, Rml::Vector2i source_dimensions)
	{
		return {};
	}

	void RenderInterfaceSkore::ReleaseTexture(Rml::TextureHandle texture) {}
	void RenderInterfaceSkore::EnableScissorRegion(bool enable) {}
	void RenderInterfaceSkore::SetScissorRegion(Rml::Rectanglei region) {}
}
