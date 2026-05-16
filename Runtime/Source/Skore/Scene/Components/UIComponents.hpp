#pragma once
#include "Skore/Core/Color.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceCommon.hpp"
#include "Skore/Scene/Component.hpp"

namespace Skore
{
	enum class LayoutDirection : u8
	{
		LeftToRight,
		TopToButton
	};

	enum class LayoutAlignmentX : u8
	{
		Left,
		Right,
		Center,
	};

	enum class LayoutAlignmentY : u8
	{
		Top,
		Bottom,
		Center,
	};

	enum class SizingType : u8
	{
		Fit,
		Grow,
		Percent,
		Fixed,
	};

	enum class TextWrapMode : u8
	{
		Words,
		Newlines,
		None
	};

	enum class TextAlignment : u8
	{
		Left,
		Center,
		Right
	};

	class UICanvas : public Component
	{
	public:
		SK_CLASS(UICanvas, Component);

		void SetRenderIndex(i32 renderIndex);
		i32  GetRenderIndex() const;

		void        Create(ComponentSettings& settings) override;
		void        Destroy() override;
		static void RegisterType(NativeReflectType<UICanvas>& type);

	private:
		i32 m_renderIndex = 0;
	};


	class SK_API UIElement : public Component
	{
	public:
		SK_CLASS(UIElement, Component);

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<UIElement>& type);

	private:
		SizingType m_sizingType = SizingType::Grow;
		Vec2       m_minSize{0, 0};
		Vec2       m_maxSize{0, 0};
		Vec2       m_percentSize{0, 0};
		Vec4       m_padding{8, 8, 8, 8};
		u32        m_childGap = 8;

		Color           m_backgroundColor = Color::TRANSPARENT_WHITE;
		LayoutDirection m_layoutDirection = LayoutDirection::LeftToRight;

		LayoutAlignmentX m_childAlignmentX = LayoutAlignmentX::Left;
		LayoutAlignmentY m_childAlignmentY = LayoutAlignmentY::Top;
	};


	class SK_API UIImage : public Component
	{
	public:
		SK_CLASS(UIImage, Component);
		static void RegisterType(NativeReflectType<UIImage>& type);

		void SetTexture(RID texture);
		RID  GetTexture() const;

		void ProcessEvent(const EntityEventDesc& event) override;

	private:
		TypedRID<TextureResource> m_texture;
	};

	template <auto Func, typename T>
	struct UIEventHandler {};

	template <auto Func, typename Owner>
	struct UIEventHandler<Func, void(Owner::*)()>
	{
		static void EventCallback(VoidPtr userData)
		{
			(*static_cast<Owner*>(userData).*Func)();
		}
	};


	class SK_API UIButton : public Component
	{
	public:
		typedef void (*OnClickCallback)(VoidPtr userData);

		SK_CLASS(UIButton, Component);

		void BindClickEvent(OnClickCallback callback, VoidPtr userData);
		void UnbindClickEvent(OnClickCallback callback, VoidPtr userData);


		template <auto Func, typename T>
		void BindClickEvent(T* instance)
		{
			BindClickEvent(UIEventHandler<Func, decltype(Func)>::EventCallback, instance);
		}

		template <auto Func, typename T>
		void UnbindClickEvent(T* instance)
		{
			UnbindClickEvent(UIEventHandler<Func, decltype(Func)>::EventCallback, instance);
		}

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<UIButton>& type);

	private:
		struct OnClicKEventData
		{
			OnClickCallback callback;
			VoidPtr         userData;
		};


		TypedRID<TextureResource> m_texture;
		TypedRID<TextureResource> m_clickedTexture;
		Array<OnClicKEventData>   m_clickEvents;
	};

	class SK_API UIText : public Component
	{
	public:
		SK_CLASS(UIText, Component);

		void          SetTextColor(const Color& color);
		Color         GetTextColor() const;
		void          SetText(StringView text);
		StringView    GetText() const;
		void          SetFontSize(f32 fontSize);
		f32           GetFontSize() const;
		void          SetFont(RID font);
		RID           GetFont() const;
		void          SetWrapMode(TextWrapMode wrapMode);
		TextWrapMode  GetWrapMode() const;
		void          SetTextAlignment(TextAlignment alignment);
		TextAlignment GetTextAlignment() const;

		void ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<UIText>& type);

	private:
		Color                  m_textColor = Color::WHITE;
		String                 m_text;
		f32                    m_fontSize = 16.0f;
		TypedRID<FontResource> m_font;
		TextWrapMode           m_wrapMode = TextWrapMode::Words;
		TextAlignment          m_textAlignment = TextAlignment::Left;

		u8 cache[40] = {};
	};
}
