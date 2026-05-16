#include "Skore/Scene/Components/UIComponents.hpp"

#include <algorithm>

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Scene.hpp"

#include "clay.h"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/IO/Input.hpp"

namespace Skore
{
	constexpr static EntityEventDesc UI_WIDGET = {.type = EntityEventType::ProcessUIWidget};

	namespace
	{
		Logger& logger = Logger::GetLogger("Skore::UIComponents");

		Clay_LayoutDirection CastLayoutDirection(LayoutDirection direction)
		{
			switch (direction)
			{
				case LayoutDirection::LeftToRight:
					return CLAY_LEFT_TO_RIGHT;
				case LayoutDirection::TopToButton:
					return CLAY_TOP_TO_BOTTOM;
			}
			return CLAY_LEFT_TO_RIGHT;
		}

		Clay_LayoutAlignmentX CastLayoutAlignmentX(LayoutAlignmentX alignment)
		{
			switch (alignment)
			{
				case LayoutAlignmentX::Left:
					return CLAY_ALIGN_X_LEFT;
				case LayoutAlignmentX::Right:
					return CLAY_ALIGN_X_RIGHT;
				case LayoutAlignmentX::Center:
					return CLAY_ALIGN_X_CENTER;
			}

			return CLAY_ALIGN_X_LEFT;
		}

		Clay_LayoutAlignmentY CastLayoutAlignmentY(LayoutAlignmentY alignment)
		{
			switch (alignment)
			{
				case LayoutAlignmentY::Top:
					return CLAY_ALIGN_Y_TOP;
				case LayoutAlignmentY::Bottom:
					return CLAY_ALIGN_Y_BOTTOM;
				case LayoutAlignmentY::Center:
					return CLAY_ALIGN_Y_CENTER;
			}

			return CLAY_ALIGN_Y_TOP;
		}

		Clay__SizingType CastSizingType(SizingType type)
		{
			switch (type)
			{
				case SizingType::Fit:
					return CLAY__SIZING_TYPE_FIT;
				case SizingType::Grow:
					return CLAY__SIZING_TYPE_GROW;
				case SizingType::Percent:
					return CLAY__SIZING_TYPE_PERCENT;
				case SizingType::Fixed:
					return CLAY__SIZING_TYPE_FIXED;
			}
			return CLAY__SIZING_TYPE_FIXED;
		}

		Clay_String ToString(StringView str)
		{
			return {
				.isStaticallyAllocated = true,
				.length = static_cast<i32>(strlen(str.Data())),
				.chars = str.Data()
			};
		}

		Clay_TextElementConfigWrapMode CastTextWrapMode(TextWrapMode wrapMode)
		{
			switch (wrapMode)
			{
				case TextWrapMode::Words:
					return CLAY_TEXT_WRAP_WORDS;
				case TextWrapMode::Newlines:
					return CLAY_TEXT_WRAP_NEWLINES;
				case TextWrapMode::None:
					return CLAY_TEXT_WRAP_NONE;
			}
			return CLAY_TEXT_WRAP_WORDS;
		}

		Clay_TextAlignment CastTextAlignment(TextAlignment alignment)
		{
			switch (alignment)
			{
				case TextAlignment::Left:
					return CLAY_TEXT_ALIGN_LEFT;
				case TextAlignment::Center:
					return CLAY_TEXT_ALIGN_CENTER;
				case TextAlignment::Right:
					return CLAY_TEXT_ALIGN_RIGHT;
			}
			return CLAY_TEXT_ALIGN_LEFT;
		}

		Clay_ElementId MakeId(Component* component)
		{
			char buffer[32] = {};
			snprintf(buffer, 32, "UI_%p", component);

			Clay_String str = {
				.isStaticallyAllocated = true,
				.length = static_cast<i32>(strlen(buffer)),
				.chars = buffer
			};
			return Clay__HashString(str, 43431233);
		}
	}

	void UICanvas::SetRenderIndex(i32 renderIndex)
	{
		m_renderIndex = renderIndex;
		RenderSceneObjects* objects = &scene->renderObjects;
		std::sort(objects->canvasList.begin(), objects->canvasList.end(), [](const UICanvas* a, const UICanvas* b) -> bool
		{
			return a->GetRenderIndex() < b->GetRenderIndex();
		});
	}

	i32 UICanvas::GetRenderIndex() const
	{
		return m_renderIndex;
	}

	//------------------------------ UICanvas ----------------------------------
	void UICanvas::Create(ComponentSettings& settings)
	{
		RenderSceneObjects* objects = &scene->renderObjects;
		objects->canvasList.EmplaceBack(this);

		std::sort(objects->canvasList.begin(), objects->canvasList.end(), [](const UICanvas* a, const UICanvas* b) -> bool
		{
			return a->GetRenderIndex() < b->GetRenderIndex();
		});
	}

	void UICanvas::Destroy()
	{
		RenderSceneObjects* objects = &scene->renderObjects;
		objects->canvasList.Remove(this);
	}

	void UICanvas::RegisterType(NativeReflectType<UICanvas>& type)
	{
		type.Field<&UICanvas::m_renderIndex, &UICanvas::GetRenderIndex, &UICanvas::SetRenderIndex>("renderIndex");

		type.Function<&UICanvas::GetRenderIndex>("GetRenderIndex");
		type.Function<&UICanvas::SetRenderIndex>("SetRenderIndex", "renderIndex");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
	}

	//------------------------------ UICanvas ----------------------------------

	void UIElement::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::ProcessUILayout)
		{
			Clay__OpenElement();

			Clay_Sizing sizing = {};

			sizing.width.type = CastSizingType(m_sizingType);
			sizing.height.type = CastSizingType(m_sizingType);

			if (m_sizingType != SizingType::Percent)
			{
				sizing.width.size.minMax = {
					.min = m_minSize.width,
					.max = m_maxSize.width
				};
				sizing.height.size.minMax = {
					.min = m_minSize.height,
					.max = m_maxSize.height
				};
			}
			else
			{
				sizing.width.size.percent = m_percentSize.width / 100.f;
				sizing.height.size.percent = m_percentSize.height / 100.f;
			}

			Clay_ElementDeclaration declaration = {};
			//declaration.id = MakeId(this);
			declaration.layout = {
				.sizing = sizing,
				.padding = {
					.left = (u16)m_padding.x,
					.right = (u16)m_padding.y,
					.top = (u16)m_padding.z,
					.bottom = (u16)m_padding.w
				},
				.childGap = static_cast<u16>(m_childGap),
				.childAlignment = {
					.x = CastLayoutAlignmentX(m_childAlignmentX),
					.y = CastLayoutAlignmentY(m_childAlignmentY)
				},
				.layoutDirection = CastLayoutDirection(m_layoutDirection)
			};

			declaration.backgroundColor = {
				static_cast<f32>(m_backgroundColor.red),
				static_cast<f32>(m_backgroundColor.green),
				static_cast<f32>(m_backgroundColor.blue),
				static_cast<f32>(m_backgroundColor.alpha)
			};

			EntityEventDesc desc = {
				.type = EntityEventType::ProcessUICreation,
				.eventData = &declaration
			};

			for (Component* component : entity->GetComponents())
			{
				component->ProcessEvent(desc);
			}

			Clay__ConfigureOpenElement(declaration);


			for (Component* component : entity->GetComponents())
			{
				component->ProcessEvent(UI_WIDGET);
			}

			for (Entity* child : entity->GetChildren())
			{
				if (child->IsActive())
				{
					child->NotifyEvent(event, false);
				}
			}

			Clay__CloseElement();
		}
	}

	void UIElement::RegisterType(NativeReflectType<UIElement>& type)
	{
		type.Field<&UIElement::m_sizingType>("sizingType");
		type.Field<&UIElement::m_minSize>("minSize");
		type.Field<&UIElement::m_maxSize>("maxSize");
		type.Field<&UIElement::m_percentSize>("percentSize").Attribute<UISliderProperty>(UISliderProperty{.minValue = 0.0, .maxValue = 100.f, .format = "%.2f%%"});
		type.Field<&UIElement::m_padding>("padding");
		type.Field<&UIElement::m_childGap>("childGap");
		type.Field<&UIElement::m_childAlignmentX>("childAlignmentX");
		type.Field<&UIElement::m_childAlignmentY>("childAlignmentY");
		type.Field<&UIElement::m_layoutDirection>("layoutDirection");
		type.Field<&UIElement::m_backgroundColor>("backgroundColor");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
	}

	//------------------------------ UIImage ----------------------------------

	void UIImage::RegisterType(NativeReflectType<UIImage>& type)
	{
		type.Field<&UIImage::m_texture>("texture");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
	}

	void UIImage::SetTexture(RID texture)
	{
		m_texture = texture;
	}

	RID UIImage::GetTexture() const
	{
		return m_texture;
	}

	void UIImage::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::ProcessUICreation)
		{
			Clay_ElementDeclaration& declaration = *static_cast<Clay_ElementDeclaration*>(event.eventData);
			if (m_texture)
			{
				declaration.image.imageData = RenderResourceCache::GetTextureCache(m_texture);
			}
		}
	}

	//------------------------------ UIButton ----------------------------------


	void UIButton::BindClickEvent(OnClickCallback callback, VoidPtr userData)
	{
		m_clickEvents.EmplaceBack(callback, userData);
	}

	void UIButton::UnbindClickEvent(OnClickCallback callback, VoidPtr userData)
	{
		auto findFunc = [&](const OnClicKEventData& item) -> bool
		{
			return item.callback == callback && item.userData == userData;
		};
		m_clickEvents.Erase(std::find_if(m_clickEvents.begin(), m_clickEvents.end(), findFunc), m_clickEvents.end());
	}

	void UIButton::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::ProcessUICreation)
		{
			Clay_ElementDeclaration& declaration = *static_cast<Clay_ElementDeclaration*>(event.eventData);

			bool hovered = Clay_Hovered();
			bool mouseDown = hovered && Input::IsMouseDown(MouseButton::Left);
			bool mouseClick = hovered && Input::IsMouseClicked(MouseButton::Left);

			if (m_clickedTexture && mouseDown)
			{
				declaration.image.imageData = RenderResourceCache::GetTextureCache(m_clickedTexture);
			}
			else if (m_texture)
			{
				declaration.image.imageData = RenderResourceCache::GetTextureCache(m_texture);
			}

			if (mouseClick)
			{
				for (const OnClicKEventData& clickEvent : m_clickEvents)
				{
					clickEvent.callback(clickEvent.userData);
				}
			}
		}
	}

	void UIButton::RegisterType(NativeReflectType<UIButton>& type)
	{
		type.Field<&UIButton::m_texture>("texture");
		type.Field<&UIButton::m_clickedTexture>("clickedTexture");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
	}

	//------------------------------ UIText ----------------------------------

	void UIText::SetTextColor(const Color& color)
	{
		m_textColor = color;
	}

	Color UIText::GetTextColor() const
	{
		return m_textColor;
	}

	void UIText::SetText(StringView text)
	{
		m_text = text;
	}

	StringView UIText::GetText() const
	{
		return m_text;
	}

	void UIText::SetFontSize(f32 fontSize)
	{
		m_fontSize = fontSize;
	}

	f32 UIText::GetFontSize() const
	{
		return m_fontSize;
	}

	void UIText::SetFont(RID font)
	{
		m_font = font;
	}

	RID UIText::GetFont() const
	{
		return m_font;
	}

	void UIText::SetWrapMode(TextWrapMode wrapMode)
	{
		m_wrapMode = wrapMode;
	}

	TextWrapMode UIText::GetWrapMode() const
	{
		return m_wrapMode;
	}

	void UIText::SetTextAlignment(TextAlignment alignment)
	{
		m_textAlignment = alignment;
	}

	TextAlignment UIText::GetTextAlignment() const
	{
		return m_textAlignment;
	}

	void UIText::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::ProcessUIWidget)
		{
			if (m_font && !m_text.Empty())
			{
				FontResourceCache* font = RenderResourceCache::GetFontCache(m_font);

				Clay_TextElementConfig* textConfig = reinterpret_cast<Clay_TextElementConfig*>(cache);
				textConfig->userData = font;
				textConfig->fontId = font->fontId;
				textConfig->textColor = {
					static_cast<f32>(m_textColor.red),
					static_cast<f32>(m_textColor.green),
					static_cast<f32>(m_textColor.blue),
					static_cast<f32>(m_textColor.alpha)
				};
				textConfig->fontSize = m_fontSize;
				textConfig->wrapMode = CastTextWrapMode(m_wrapMode);
				textConfig->textAlignment = CastTextAlignment(m_textAlignment);
				Clay__OpenTextElement(ToString(m_text), textConfig);
			}
		}
	}

	void UIText::RegisterType(NativeReflectType<UIText>& type)
	{
		type.Field<&UIText::m_textColor, &UIText::GetTextColor, &UIText::SetTextColor>("textColor");
		type.Field<&UIText::m_text, &UIText::GetText, &UIText::SetText>("text").Attribute<UIStringProperty>(UIStringProperty{.multiline = true});
		type.Field<&UIText::m_fontSize, &UIText::GetFontSize, &UIText::SetFontSize>("fontSize");
		type.Field<&UIText::m_font, &UIText::GetFont, &UIText::SetFont>("font");
		type.Field<&UIText::m_wrapMode, &UIText::GetWrapMode, &UIText::SetWrapMode>("wrapMode");
		type.Field<&UIText::m_textAlignment, &UIText::GetTextAlignment, &UIText::SetTextAlignment>("textAlignment");
		type.Attribute<ComponentDesc>(ComponentDesc{.category = "UI"});
	}
}