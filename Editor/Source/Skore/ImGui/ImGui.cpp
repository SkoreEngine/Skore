// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "ImGui.hpp"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>

#include "IconsFontAwesome6.h"
#include "ImGuizmo.h"
#include "imgui_impl_vulkan.h"
#include "Skore/App.hpp"
#include "Skore/Editor.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/Reflection.hpp"
#include "../Utils/StaticContent.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Devices/VulkanDevice.hpp"
#include "Skore/IO/InputTypes.hpp"

namespace Skore
{
	namespace
	{
		f32                       scaleFactor = 1.0;
		ImGuiKey                  keys[static_cast<u32>(Key::MAX)];
		usize                     renamingItem = 0;
		Array<ImGuiFieldRenderer> fieldRenderers;
		Logger&                   logger = Logger::GetLogger("Skore::ImGui");

		struct InputTextCallback_UserData
		{
			String& str;
		};

		struct DrawFieldContext
		{
			FnDrawField drawField;
			VoidPtr     context;
		};

		struct ObjectTypeFieldRenderer
		{
			String                   label;
			ReflectType*             reflectType;
			ReflectField*            reflectField;
			FnObjectFieldVisibilityControl fieldVisibilityControl;
			Array<DrawFieldContext>  drawFn;
		};

		struct FieldVisibilityControl
		{
			HashMap<String, FnObjectFieldVisibilityControl> fieldVisibilityControls;
		};

		struct ResourceFieldVisibilityControl
		{
			HashMap<String, FnResourceFieldVisibilityControl> resourceFieldVisibilityControls;
		};

		struct ObjectTypeRenderer
		{
			ReflectType*                   reflectType;
			Array<ObjectTypeFieldRenderer> fields;
		};

		struct ResourceFieldRenderer
		{
			u32                              index;
			String                           label;
			FieldProps                       fieldProps = {};
			ReflectType*                     reflectFieldType;
			FnResourceFieldVisibilityControl visibilityControl;
			Array<DrawFieldContext>          drawFn;
		};

		struct ResourceTypeRenderer
		{
			Array<ResourceFieldRenderer> fields;
		};

		HashMap<TypeID, ObjectTypeRenderer>             objectTypeRenderers;
		HashMap<TypeID, FieldVisibilityControl>         objectVisibilityControl;
		HashMap<TypeID, ResourceFieldVisibilityControl> resourceVisibilityControl;
		HashMap<TypeID, ResourceTypeRenderer>           resourceTypeRenders;
	}

	SK_API SDL_Window*   GraphicsGetWindow();
	SK_API GPUSwapchain* GraphicsGetSwapchain();
	SK_API void AddSDLEventCallback(void(*callback)(SDL_Event* event));

	static int InputTextCallback(ImGuiInputTextCallbackData* data)
	{
		auto userData = (InputTextCallback_UserData*)data->UserData;
		if (data->BufTextLen >= (userData->str.Capacity() - 1))
		{
			auto newSize = userData->str.Capacity() + (userData->str.Capacity() * 2) / 3;
			userData->str.Reserve(newSize);
			data->Buf = userData->str.begin();
		}
		userData->str.SetSize(data->BufTextLen);
		return 0;
	}

	void ApplyFonts()
	{
		f32 fontSize = 15.f;

		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		{
			Array<u8> bytes = StaticContent::GetBinaryFile("Content/Fonts/DejaVuSans.ttf");
			auto      font = ImFontConfig();
			font.SizePixels = fontSize * scaleFactor;
			memcpy(font.Name, "DejaVuSans", 9);
			font.FontDataOwnedByAtlas = false;
			io.Fonts->AddFontFromMemoryTTF(bytes.Data(), bytes.Size(), font.SizePixels, &font);
		}

		{
			static constexpr ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

			ImFontConfig config = ImFontConfig();
			config.SizePixels = fontSize * scaleFactor;
			config.MergeMode = true;
			config.GlyphMinAdvanceX = fontSize * scaleFactor;
			config.GlyphMaxAdvanceX = fontSize * scaleFactor;
			config.FontDataOwnedByAtlas = false;
			memcpy(config.Name, "FontAwesome", 11);

			Array<u8> bytes = StaticContent::GetBinaryFile("Content/Fonts/fa-solid-900.otf");
			io.Fonts->AddFontFromMemoryTTF(bytes.Data(), bytes.Size(), config.SizePixels, &config, icon_ranges);
		}
	}

	void SetupDefaultStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4*     colors = style.Colors;

		colors[ImGuiCol_Text] = ImVec4(0.71f, 0.72f, 0.71f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
		colors[ImGuiCol_Border] = ImVec4(0.01f, 0.01f, 0.02f, 1.00f);
		colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.42f, 0.42f, 0.42f, 0.40f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.67f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.11f, 0.12f, 0.13f, 0.53f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.59f, 0.60f, 0.59f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.59f, 0.60f, 0.59f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.64f, 0.64f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
		colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
		colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(0.01f, 0.02f, 0.04f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.36f, 0.46f, 0.54f, 1.00f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.55f, 0.78f, 1.00f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.58f, 0.71f, 0.82f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.50f, 0.50f, 0.50f, 0.35f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);

		style.PopupRounding = 3;

		style.WindowPadding = ImVec2(6, 6);
		style.FramePadding = ImVec2(5, 4);
		style.ItemSpacing = ImVec2(8, 2);
		style.CellPadding = ImVec2(4, 1);
		style.ScrollbarSize = 15;
		style.WindowBorderSize = 1;
		style.ChildBorderSize = 0;
		style.PopupBorderSize = 1;
		style.FrameBorderSize = 1;
		style.WindowRounding = 3;
		style.ChildRounding = 0;
		style.FrameRounding = 3;
		style.ScrollbarRounding = 2;
		style.GrabRounding = 3;

		style.TabBorderSize = 0;
		style.TabRounding = 2;
		style.IndentSpacing = 10;

		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);

		colors[ImGuiCol_Tab] = colors[ImGuiCol_TitleBg];
		colors[ImGuiCol_TabHovered] = colors[ImGuiCol_WindowBg];
		colors[ImGuiCol_TabActive] = colors[ImGuiCol_WindowBg];
		colors[ImGuiCol_TabUnfocused] = colors[ImGuiCol_TitleBg];
		colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_WindowBg];
		colors[ImGuiCol_DockingPreview] = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
		style.ScaleAllSizes(scaleFactor);

		// //guizmo
		f32   guizmoScaleFactor = scaleFactor * 1.1;
		auto& guizmoSize = ImGuizmo::GetStyle();
		new(&guizmoSize) ImGuizmo::Style{};
		//
		guizmoSize.CenterCircleSize = guizmoSize.CenterCircleSize * guizmoScaleFactor;
		guizmoSize.HatchedAxisLineThickness = guizmoSize.HatchedAxisLineThickness * guizmoScaleFactor;
		guizmoSize.RotationLineThickness = guizmoSize.RotationLineThickness * guizmoScaleFactor;
		guizmoSize.RotationOuterLineThickness = guizmoSize.RotationOuterLineThickness * guizmoScaleFactor;
		guizmoSize.ScaleLineCircleSize = guizmoSize.ScaleLineCircleSize * guizmoScaleFactor;
		guizmoSize.ScaleLineThickness = guizmoSize.ScaleLineThickness * guizmoScaleFactor;
		guizmoSize.TranslationLineArrowSize = guizmoSize.TranslationLineArrowSize * guizmoScaleFactor;
		guizmoSize.TranslationLineThickness = guizmoSize.TranslationLineThickness * guizmoScaleFactor;
	}

	void RegisterKeys()
	{
		keys[static_cast<u32>(Key::Space)] = ImGuiKey_Space;
		keys[static_cast<u32>(Key::Apostrophe)] = ImGuiKey_Apostrophe;
		keys[static_cast<u32>(Key::Comma)] = ImGuiKey_Comma;
		keys[static_cast<u32>(Key::Minus)] = ImGuiKey_Minus;
		keys[static_cast<u32>(Key::Period)] = ImGuiKey_Period;
		keys[static_cast<u32>(Key::Slash)] = ImGuiKey_Slash;
		keys[static_cast<u32>(Key::Num0)] = ImGuiKey_0;
		keys[static_cast<u32>(Key::Num1)] = ImGuiKey_1;
		keys[static_cast<u32>(Key::Num2)] = ImGuiKey_2;
		keys[static_cast<u32>(Key::Num3)] = ImGuiKey_3;
		keys[static_cast<u32>(Key::Num4)] = ImGuiKey_4;
		keys[static_cast<u32>(Key::Num5)] = ImGuiKey_5;
		keys[static_cast<u32>(Key::Num6)] = ImGuiKey_6;
		keys[static_cast<u32>(Key::Num7)] = ImGuiKey_7;
		keys[static_cast<u32>(Key::Num8)] = ImGuiKey_8;
		keys[static_cast<u32>(Key::Num9)] = ImGuiKey_9;
		keys[static_cast<u32>(Key::Semicolon)] = ImGuiKey_Semicolon;
		keys[static_cast<u32>(Key::Equal)] = ImGuiKey_Equal;
		keys[static_cast<u32>(Key::A)] = ImGuiKey_A;
		keys[static_cast<u32>(Key::B)] = ImGuiKey_B;
		keys[static_cast<u32>(Key::C)] = ImGuiKey_C;
		keys[static_cast<u32>(Key::D)] = ImGuiKey_D;
		keys[static_cast<u32>(Key::E)] = ImGuiKey_E;
		keys[static_cast<u32>(Key::F)] = ImGuiKey_F;
		keys[static_cast<u32>(Key::G)] = ImGuiKey_G;
		keys[static_cast<u32>(Key::H)] = ImGuiKey_H;
		keys[static_cast<u32>(Key::I)] = ImGuiKey_I;
		keys[static_cast<u32>(Key::J)] = ImGuiKey_J;
		keys[static_cast<u32>(Key::K)] = ImGuiKey_K;
		keys[static_cast<u32>(Key::L)] = ImGuiKey_L;
		keys[static_cast<u32>(Key::M)] = ImGuiKey_M;
		keys[static_cast<u32>(Key::N)] = ImGuiKey_N;
		keys[static_cast<u32>(Key::O)] = ImGuiKey_O;
		keys[static_cast<u32>(Key::P)] = ImGuiKey_P;
		keys[static_cast<u32>(Key::Q)] = ImGuiKey_Q;
		keys[static_cast<u32>(Key::R)] = ImGuiKey_R;
		keys[static_cast<u32>(Key::S)] = ImGuiKey_S;
		keys[static_cast<u32>(Key::T)] = ImGuiKey_T;
		keys[static_cast<u32>(Key::U)] = ImGuiKey_U;
		keys[static_cast<u32>(Key::V)] = ImGuiKey_V;
		keys[static_cast<u32>(Key::W)] = ImGuiKey_W;
		keys[static_cast<u32>(Key::X)] = ImGuiKey_X;
		keys[static_cast<u32>(Key::Y)] = ImGuiKey_Y;
		keys[static_cast<u32>(Key::Z)] = ImGuiKey_Z;
		keys[static_cast<u32>(Key::LeftBracket)] = ImGuiKey_LeftBracket;
		keys[static_cast<u32>(Key::Backslash)] = ImGuiKey_Backslash;
		keys[static_cast<u32>(Key::RightBracket)] = ImGuiKey_RightBracket;
		keys[static_cast<u32>(Key::GraveAccent)] = ImGuiKey_GraveAccent;
		//		keys[static_cast<u32>(Key::Scene1)] = ImGuiKey_Scene1;
		//		keys[static_cast<u32>(Key::Scene2)] = ImGuiKey_Scene2;
		keys[static_cast<u32>(Key::Escape)] = ImGuiKey_Escape;
		keys[static_cast<u32>(Key::Enter)] = ImGuiKey_Enter;
		keys[static_cast<u32>(Key::Tab)] = ImGuiKey_Tab;
		keys[static_cast<u32>(Key::Backspace)] = ImGuiKey_Backspace;
		keys[static_cast<u32>(Key::Insert)] = ImGuiKey_Insert;
		keys[static_cast<u32>(Key::Delete)] = ImGuiKey_Delete;
		keys[static_cast<u32>(Key::Right)] = ImGuiKey_RightArrow;
		keys[static_cast<u32>(Key::Left)] = ImGuiKey_LeftArrow;
		keys[static_cast<u32>(Key::Down)] = ImGuiKey_DownArrow;
		keys[static_cast<u32>(Key::Up)] = ImGuiKey_UpArrow;
		keys[static_cast<u32>(Key::PageUp)] = ImGuiKey_PageUp;
		keys[static_cast<u32>(Key::PageDown)] = ImGuiKey_PageDown;
		keys[static_cast<u32>(Key::Home)] = ImGuiKey_Home;
		keys[static_cast<u32>(Key::End)] = ImGuiKey_End;
		keys[static_cast<u32>(Key::CapsLock)] = ImGuiKey_CapsLock;
		keys[static_cast<u32>(Key::ScrollLock)] = ImGuiKey_ScrollLock;
		keys[static_cast<u32>(Key::NumLock)] = ImGuiKey_NumLock;
		keys[static_cast<u32>(Key::PrintScreen)] = ImGuiKey_PrintScreen;
		keys[static_cast<u32>(Key::Pause)] = ImGuiKey_Pause;
		keys[static_cast<u32>(Key::F1)] = ImGuiKey_F1;
		keys[static_cast<u32>(Key::F2)] = ImGuiKey_F2;
		keys[static_cast<u32>(Key::F3)] = ImGuiKey_F3;
		keys[static_cast<u32>(Key::F4)] = ImGuiKey_F4;
		keys[static_cast<u32>(Key::F5)] = ImGuiKey_F5;
		keys[static_cast<u32>(Key::F6)] = ImGuiKey_F6;
		keys[static_cast<u32>(Key::F7)] = ImGuiKey_F7;
		keys[static_cast<u32>(Key::F8)] = ImGuiKey_F8;
		keys[static_cast<u32>(Key::F9)] = ImGuiKey_F9;
		keys[static_cast<u32>(Key::F10)] = ImGuiKey_F10;
		keys[static_cast<u32>(Key::F11)] = ImGuiKey_F11;
		keys[static_cast<u32>(Key::F12)] = ImGuiKey_F12;
		keys[static_cast<u32>(Key::F13)] = ImGuiKey_F13;
		keys[static_cast<u32>(Key::F14)] = ImGuiKey_F14;
		keys[static_cast<u32>(Key::F15)] = ImGuiKey_F15;
		keys[static_cast<u32>(Key::F16)] = ImGuiKey_F16;
		keys[static_cast<u32>(Key::F17)] = ImGuiKey_F17;
		keys[static_cast<u32>(Key::F18)] = ImGuiKey_F18;
		keys[static_cast<u32>(Key::F19)] = ImGuiKey_F19;
		keys[static_cast<u32>(Key::F20)] = ImGuiKey_F20;
		keys[static_cast<u32>(Key::F21)] = ImGuiKey_F21;
		keys[static_cast<u32>(Key::F22)] = ImGuiKey_F22;
		keys[static_cast<u32>(Key::F23)] = ImGuiKey_F23;
		keys[static_cast<u32>(Key::F24)] = ImGuiKey_F24;
		keys[static_cast<u32>(Key::Keypad0)] = ImGuiKey_Keypad0;
		keys[static_cast<u32>(Key::Keypad1)] = ImGuiKey_Keypad1;
		keys[static_cast<u32>(Key::Keypad2)] = ImGuiKey_Keypad2;
		keys[static_cast<u32>(Key::Keypad3)] = ImGuiKey_Keypad3;
		keys[static_cast<u32>(Key::Keypad4)] = ImGuiKey_Keypad4;
		keys[static_cast<u32>(Key::Keypad5)] = ImGuiKey_Keypad5;
		keys[static_cast<u32>(Key::Keypad6)] = ImGuiKey_Keypad6;
		keys[static_cast<u32>(Key::Keypad7)] = ImGuiKey_Keypad7;
		keys[static_cast<u32>(Key::Keypad8)] = ImGuiKey_Keypad8;
		keys[static_cast<u32>(Key::Keypad9)] = ImGuiKey_Keypad9;
		keys[static_cast<u32>(Key::KeypadDecimal)] = ImGuiKey_KeypadDecimal;
		keys[static_cast<u32>(Key::KeypadDivide)] = ImGuiKey_KeypadDivide;
		keys[static_cast<u32>(Key::KeypadMultiply)] = ImGuiKey_KeypadMultiply;
		keys[static_cast<u32>(Key::KeypadSubtract)] = ImGuiKey_KeypadSubtract;
		keys[static_cast<u32>(Key::KeypadAdd)] = ImGuiKey_KeypadAdd;
		keys[static_cast<u32>(Key::KeypadEnter)] = ImGuiKey_KeypadEnter;
		keys[static_cast<u32>(Key::KeypadEqual)] = ImGuiKey_KeypadEqual;
		keys[static_cast<u32>(Key::LeftShift)] = ImGuiKey_LeftShift;
		keys[static_cast<u32>(Key::LeftCtrl)] = ImGuiKey_LeftCtrl;
		keys[static_cast<u32>(Key::LeftAlt)] = ImGuiKey_LeftAlt;
		keys[static_cast<u32>(Key::LeftSuper)] = ImGuiKey_LeftSuper;
		keys[static_cast<u32>(Key::RightShift)] = ImGuiKey_RightShift;
		keys[static_cast<u32>(Key::RightCtrl)] = ImGuiKey_RightCtrl;
		keys[static_cast<u32>(Key::RightAlt)] = ImGuiKey_RightAlt;
		keys[static_cast<u32>(Key::RightSuper)] = ImGuiKey_RightSuper;
		keys[static_cast<u32>(Key::Menu)] = ImGuiKey_Menu;
	}

	void RegisterFieldRenderers();
	void RegisterFieldVisibilityControls();
	void ImGuiNewFrame();
	void ImGuiEndFrame();
	void ImGuiRender();
	void ImGuiDestroy();

	void SDLProcessEvent(SDL_Event* event)
	{
		ImGui_ImplSDL3_ProcessEvent(event);
	}

	void ImGuiInit()
	{
		AddSDLEventCallback(SDLProcessEvent);

		SDL_Window*   window = GraphicsGetWindow();
		GPUSwapchain* swapchain = GraphicsGetSwapchain();

		scaleFactor = SDL_GetWindowDisplayScale(window);

		//init imgui
		//Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		io.IniFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigViewportsNoTaskBarIcon = true;

		ApplyFonts();
		SetupDefaultStyle();

		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
			{
				if (volkInitialize() != VK_SUCCESS)
				{
					logger.Error("vulkan cannot be initialized");
					return;
				}
				VulkanDevice* device = static_cast<VulkanDevice*>(Graphics::GetDevice());
				volkLoadInstance(device->instance);

				ImGui_ImplSDL3_InitForVulkan(window);
				ImGui_ImplVulkan_InitInfo info;
				info.Instance = device->instance;
				info.PhysicalDevice = device->selectedAdapter->device;
				info.Device = device->device;
				info.QueueFamily = device->selectedAdapter->graphicsFamily;
				info.Queue = device->graphicsQueue;
				info.DescriptorPool = device->descriptorPool;
				info.RenderPass = static_cast<VulkanRenderPass*>(swapchain->GetCurrentRenderPass())->renderPass;
				info.MinImageCount = 2;
				info.ImageCount = swapchain->GetImageCount();
				info.PipelineCache = nullptr;
				info.Subpass = 0;
				info.UseDynamicRendering = false;
				info.Allocator = nullptr;
				info.CheckVkResultFn = nullptr;
				info.MinAllocationSize = 0;
				info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

				ImGui_ImplVulkan_Init(&info);
				ImGui_ImplVulkan_CreateFontsTexture();
				break;
			}
			case GraphicsAPI::D3D12:
				break;
			case GraphicsAPI::Metal:
				break;
			case GraphicsAPI::None:
				break;
		}


		RegisterKeys();
		RegisterFieldRenderers();
		RegisterFieldVisibilityControls();

		Event::Bind<OnUIRender, ImGuiRender>();
		Event::Bind<OnBeginFrame, &ImGuiNewFrame>();
		Event::Bind<OnEndFrame, &ImGuiEndFrame>();
		Event::Bind<OnShutdown, ImGuiDestroy>();
	}

	void ImGuiNewFrame()
	{
		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
				ImGui_ImplVulkan_NewFrame();
				break;
			case GraphicsAPI::D3D12:
				break;
			case GraphicsAPI::Metal:
				break;
			case GraphicsAPI::None:
				break;
		}

		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiEndFrame()
	{
		ImGui::EndFrame();
	}

	void ImGuiRender()
	{
		ImGui::Render();

		GPUCommandBuffer* cmd = Graphics::GetCurrentCommandBuffer();

		cmd->BeginDebugMarker("ImGui", Vec4(0, 0, 0, 1));

		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VulkanCommandBuffer*>(cmd)->commandBuffer);
				break;
			case GraphicsAPI::D3D12:
				break;
			case GraphicsAPI::Metal:
				break;
			case GraphicsAPI::None:
				break;
		}

		cmd->EndDebugMarker();
	}

	void ImGuiDestroy()
	{
		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
				ImGui_ImplVulkan_Shutdown();
				break;
			case GraphicsAPI::D3D12:
				break;
			case GraphicsAPI::Metal:
				break;
			case GraphicsAPI::None:
				break;
		}
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}

	ImGuiKey AsImGuiKey(Key key)
	{
		return keys[static_cast<usize>(key)];
	}

	float GetScaleFactor()
	{
		return scaleFactor;
	}

	void ImGuiCreateDockSpace(ImGuiID dockSpaceId)
	{
		static ImGuiDockNodeFlags dockNodeFlags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		auto             viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 20, 23, 255));
		windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		dockNodeFlags |= ImGuiDockNodeFlags_NoWindowMenuButton;

		ImGui::Begin("DockSpace", 0, windowFlags);
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(1);

		ImGuiStyle& style = ImGui::GetStyle();
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, viewport->WorkSize.y - 40 * scaleFactor), dockNodeFlags);

		//		auto* drawList = ImGui::GetWindowDrawList();
		//		drawList->AddLine(ImVec2(0.0f, viewport->WorkSize.y - 30), ImVec2(viewport->WorkSize.x, viewport->WorkSize.y), IM_COL32(0, 0, 0, 255), 1.f * style.ScaleFactor);
	}

	bool ImGuiBegin(u32 id, const char* name, bool* pOpen, ImGuiWindowFlags flags)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		ImGui::SetNextWindowSize(ImVec2(1024, 576) * scaleFactor, ImGuiCond_Once);

		char str[100];
		sprintf(str, "%s###%d", name, id);
		bool open = ImGui::Begin(str, pOpen, flags);
		if (open && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
		{
			//hoveredWindowId = id;
		}
		return open;
	}

	bool ImGuiBeginFullscreen(u32 id, bool* pOpen, ImGuiWindowFlags flags)
	{
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | flags;
		auto             viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		char str[20];
		sprintf(str, "###%d", id);
		bool open = ImGui::Begin(str, pOpen, windowFlags);

		ImGui::PopStyleVar(); //ImGuiStyleVar_WindowRounding
		ImGui::PopStyleVar(); //ImGuiStyleVar_WindowBorderSize

		return open;
	}

	void ImGuiDockBuilderReset(ImGuiID dockSpaceId)
	{
		auto viewport = ImGui::GetMainViewport();
		ImGui::DockBuilderRemoveNode(dockSpaceId);
		ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);
	}

	void ImGuiDockBuilderDockWindow(ImGuiID windowId, ImGuiID nodeId)
	{
		char str[20];
		sprintf(str, "###%d", windowId);
		ImGui::DockBuilderDockWindow(str, nodeId);
	}

	bool ImGuiInputText(u32 idx, String& string, ImGuiInputTextFlags flags, ImGuiInputTextExtraFlags extraFlags)
	{
		char str[100];
		sprintf(str, "###txtid%d", idx);

		InputTextCallback_UserData user_data{string};
		flags |= ImGuiInputTextFlags_CallbackResize;

		auto ret = ImGui::InputText(str, string.begin(), string.Capacity() + 1, flags, InputTextCallback, &user_data);

		auto& imguiContext = *ImGui::GetCurrentContext();

		auto& rect = imguiContext.LastItemData.Rect;
		auto* drawList = ImGui::GetWindowDrawList();


		if (extraFlags & ImGuiInputTextExtraFlags_ShowError)
		{
			drawList->AddRect(rect.Min, ImVec2(rect.Max.x - ImGui::GetStyle().ScaleFactor, rect.Max.y), IM_COL32(199, 66, 66, 255), ImGui::GetStyle().FrameRounding, 0,
			                  1 * ImGui::GetStyle().ScaleFactor);
		}
		else if (ImGui::IsItemFocused())
		{
			drawList->AddRect(rect.Min, ImVec2(rect.Max.x - ImGui::GetStyle().ScaleFactor, rect.Max.y), IM_COL32(66, 140, 199, 255), ImGui::GetStyle().FrameRounding, 0,
			                  1 * ImGui::GetStyle().ScaleFactor);
		}

		return ret;
	}

	void ImGuiInputTextReadOnly(u32 idx, StringView string, ImGuiInputTextFlags flags)
	{
		flags |= ImGuiInputTextFlags_ReadOnly;

		char str[50];
		sprintf(str, "###readonlytxtid%d", idx);
		ImGui::InputText(str, const_cast<char*>(string.CStr()), string.Size(), flags, nullptr, nullptr);

		auto& imguiContext = *ImGui::GetCurrentContext();
		auto& rect = imguiContext.LastItemData.Rect;
		auto* drawList = ImGui::GetWindowDrawList();
		if (ImGui::IsItemFocused())
		{
			drawList->AddRect(rect.Min, ImVec2(rect.Max.x - ImGui::GetStyle().ScaleFactor, rect.Max.y), IM_COL32(66, 140, 199, 255), ImGui::GetStyle().FrameRounding, 0,
			                  1 * ImGui::GetStyle().ScaleFactor);
		}
	}


	bool ImGuiSearchInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags)
	{
		const auto searching = !string.Empty();

		auto&       style = ImGui::GetStyle();
		const float newPadding = 28.0f * ImGui::GetStyle().ScaleFactor;
		auto&       context = *ImGui::GetCurrentContext();
		auto*       drawList = ImGui::GetWindowDrawList();
		auto&       rect = context.LastItemData.Rect;

		ScopedStyleVar styleVar{ImGuiStyleVar_FramePadding, ImVec2(newPadding, style.FramePadding.y)};

		auto modified = ImGuiInputText(idx, string, flags);

		if (!searching)
		{
			drawList->AddText(ImVec2(rect.Min.x + newPadding, rect.Min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), "Search");
		}

		drawList->AddText(ImVec2(rect.Min.x + style.ItemInnerSpacing.x, rect.Min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_MAGNIFYING_GLASS);

		return modified;
	}

	bool ImGuiPathInputText(ImGuiID idx, String& string, ImGuiInputTextFlags flags)
	{
		return false;
	}

	void ImGuiBeginTreeNodeStyle()
	{
		// ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, std::ceil(1 * ImGui::GetStyle().ScaleFactor)));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.67f)); //TODO - get from config (selected item)
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0, 0.0f, 0.0f, 0.0f));
	}

	void ImGuiEndTreeNodeStyle()
	{
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
	}

	bool ImGuiTreeNode(VoidPtr id, const char* label, ImGuiTreeNodeFlags flags)
	{
		flags |= ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_OpenOnDoubleClick |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_SpanFullWidth |
			ImGuiTreeNodeFlags_FramePadding;

		return ImGui::TreeNodeEx(id, flags, "%s", label);
	}

	bool ImGuiTreeLeaf(VoidPtr id, const char* label, ImGuiTreeNodeFlags flags)
	{
		flags |= ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_OpenOnDoubleClick |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_Leaf |
			ImGuiTreeNodeFlags_SpanFullWidth |
			ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_FramePadding;

		return ImGui::TreeNodeEx(id, flags, "%s", label);
	}

	bool ImGuiBeginPopupMenu(const char* str, ImGuiWindowFlags popupFlags, bool setSize)
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6 * style.ScaleFactor, 4 * style.ScaleFactor});
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{1, 1});

		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));
		ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));

		if (setSize)
		{
			ImGui::SetNextWindowSize(ImVec2{300, 0,}, ImGuiCond_Once);
		}
		bool res = ImGui::BeginPopup(str, popupFlags);
		return res;
	}

	void ImGuiEndPopupMenu(bool closePopup)
	{
		if (closePopup)
		{
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
	}

	void ImGuiTextWithLabel(StringView label, StringView text)
	{
		ImGui::TextDisabled(label.CStr());
		ImGui::SameLine();
		ImGui::Text(text.CStr());
	}

	bool ImGuiSelectionButton(const char* label, bool selected, const ImVec2& sizeArg)
	{
		//TODO make select
		return ImGui::Button(label, sizeArg);
	}

	bool ImGuiBorderedButton(const char* label, const ImVec2& size)
	{
		ScopedStyleColor border(ImGuiCol_Border, ImVec4(0.46f, 0.49f, 0.50f, 0.67f));
		return ImGui::Button(label, size);
	}

	bool ImGuiCurrentTableHovered()
	{
		auto currentTable = ImGui::GetCurrentTable();
		return currentTable && ImGui::TableGetHoveredRow() == currentTable->CurrentRow;
	}

	void ImGuiCentralizedText(const char* text)
	{
		ImGui::BeginVertical("vertical", ImGui::GetCurrentWindow()->Size);
		ImGui::Spring(1.0);
		ImGui::BeginHorizontal("horizontal", ImGui::GetCurrentWindow()->Size);
		ImGui::Spring(1.0);
		ImGui::Text(text);
		ImGui::Spring(1.0);
		ImGui::EndHorizontal();
		ImGui::Spring(1.0);
		ImGui::EndVertical();
	}

	ImTextureID GetImGuiTextureId(GPUTextureView* textureView)
	{
		ImTextureID userTextureId = nullptr;

		switch (Graphics::GetAPI())
		{
			case GraphicsAPI::Vulkan:
			{
				VulkanTextureView* vulkanTextureView = static_cast<VulkanTextureView*>(textureView);
				if (!vulkanTextureView->viewDescriptorSet)
				{
					vulkanTextureView->viewDescriptorSet = ImGui_ImplVulkan_AddTexture(
						static_cast<VulkanSampler*>(Graphics::GetLinearSampler())->sampler,
						vulkanTextureView->imageView,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				}
				userTextureId = vulkanTextureView->viewDescriptorSet;
				break;
			}
			default: SK_ASSERT(false, "not implemented");
		}
		return userTextureId;
	}

	void ImGuiTextureItem(GPUTexture* texture, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		if (!texture) return;
		ImGui::Image(GetImGuiTextureId(texture->GetTextureView()), image_size, uv0, uv1, tint_col, border_col);
	}


	void ImGuiDrawTexture(GPUTexture* texture, const Rect& rect, const ImVec4& tintCol)
	{
		if (!texture) return;
		ImGuiDrawTextureView(texture->GetTextureView(), rect, tintCol);
	}

	void ImGuiDrawTextureView(GPUTextureView* textureView, const Rect& rect, const ImVec4& tintCol)
	{
		if (!textureView) return;

		ImTextureID userTextureId = GetImGuiTextureId(textureView);

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddImage(userTextureId,
		                   ImVec2(rect.x, rect.y),
		                   ImVec2(rect.width, rect.height),
		                   ImVec2(0, 0),
		                   ImVec2(1, 1),
		                   ImGui::ColorConvertFloat4ToU32(tintCol));
	}

	Span<ImGuiFieldRenderer> ImGuiGetFieldRenders()
	{
		return fieldRenderers;
	}

	bool ImGuiBeginContentTable(const char* id, f32 thumbnailScale)
	{
		u32 thumbnailSize = (thumbnailScale * 112.f) * ImGui::GetStyle().ScaleFactor;

		static ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedSame;
		u32                    columns = Math::Max(static_cast<u32>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x) / thumbnailSize), 1u);

		bool ret = ImGui::BeginTable(id, columns, tableFlags);
		if (ret)
		{
			for (int i = 0; i < columns; ++i)
			{
				char buffer[20]{};
				sprintf(buffer, "%ld", i);
				ImGui::TableSetupColumn(buffer, ImGuiTableColumnFlags_WidthFixed, thumbnailSize);
			}
		}
		return ret;
	}

	ImGuiContentItemState ImGuiContentItem(const ImGuiContentItemDesc& contentItemDesc)
	{
		ImGuiStyle& style = ImGui::GetStyle();
		u32         thumbnailSize = (contentItemDesc.thumbnailScale * 112.f) * ImGui::GetStyle().ScaleFactor;

		ImGui::TableNextColumn();
		auto* drawList = ImGui::GetWindowDrawList();
		auto  screenCursorPos = ImGui::GetCursorScreenPos();

		f32 imagePadding = thumbnailSize * 0.08f;


		ImGuiContext* context = ImGui::GetCurrentContext();
		bool windowHovered = context->HoveredWindow == context->CurrentWindow;

		auto posEnd = ImVec2(screenCursorPos.x + thumbnailSize, screenCursorPos.y + thumbnailSize);
		bool hovered = ImGui::IsMouseHoveringRect(screenCursorPos, posEnd, true) &&  windowHovered;

		i32  mouseCount = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
		bool isDoubleClicked = mouseCount >= 2 && (mouseCount % 2) == 0;
		bool isDoubleClickedAction = isDoubleClicked && hovered && !contentItemDesc.renameItem;

		static u64 idPressed = U64_MAX;

		if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && hovered)
		{
			idPressed = contentItemDesc.id;
		}

		bool released = false;

		if ((ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right)) && hovered)
		{
			released = idPressed == contentItemDesc.id;
			idPressed = U64_MAX;
		}

		bool clicked = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
		bool isEnter = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && contentItemDesc.selected && !contentItemDesc.renameItem;

		if (hovered)
		{
			drawList->AddRectFilled(screenCursorPos,
			                        posEnd,
			                        IM_COL32(40, 41, 43, 255),
			                        0.0f);
		}

		ImGuiContentItemState state = ImGuiContentItemState{
			.hovered = hovered,
			.clicked = clicked && hovered && !contentItemDesc.renameItem,
			.released = released,
			.enter = isDoubleClickedAction || isEnter,
			.screenStartPos = screenCursorPos,
			.size = ImVec2(thumbnailSize, thumbnailSize)
		};

		ImVec2 rectTexture = ImVec2{(f32)thumbnailSize, (f32)thumbnailSize - imagePadding * 3.f};

		const ImRect bb(screenCursorPos, screenCursorPos + rectTexture);
		ImGui::ItemSize(rectTexture);
		if (ImGui::ItemAdd(bb, 0))
		{
			ImGuiDrawTexture(contentItemDesc.texture, {
				                 i32(screenCursorPos.x + imagePadding * 2),
				                 i32(screenCursorPos.y + imagePadding),
				                 (u32)(screenCursorPos.x + thumbnailSize - imagePadding * 2),
				                 (u32)(screenCursorPos.y + thumbnailSize - imagePadding * 3)
			                 });
		}

		if (contentItemDesc.showError)
		{
			static f32 textSize = ImGui::CalcTextSize(ICON_FA_CIRCLE_EXCLAMATION).x;
			drawList->AddText(ImVec2(posEnd.x - (textSize + style.WindowPadding.x), screenCursorPos.y + style.WindowPadding.y), IM_COL32(202, 98, 87, 255), ICON_FA_CIRCLE_EXCLAMATION);
		}

		char str[40];
		sprintf(str, "###BeginVertical1%llu", contentItemDesc.id);
		ImGui::BeginVertical(str, ImVec2(thumbnailSize, thumbnailSize - (ImGui::GetCursorScreenPos().y - screenCursorPos.y)));
		{
			ImGui::Spring();

			auto textSize = ImGui::CalcTextSize(contentItemDesc.label.CStr());
			auto textPadding = textSize.y / 1.5f;
			textSize.x = Math::Min(textSize.x, f32(thumbnailSize - textPadding));

			sprintf(str, "###BeginVertical2%llu", contentItemDesc.id);
			ImGui::BeginHorizontal(str, ImVec2(thumbnailSize, 0.0f));
			ImGui::Spring(1.0);
			{
				if (!contentItemDesc.renameItem)
				{
					ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + textSize, true);
					drawList->AddText(ImGui::GetCursorScreenPos(), ImGui::GetColorU32(ImGuiCol_Text), contentItemDesc.label.CStr());
					ImGui::PopClipRect();
					ImGui::Dummy(textSize);
				}
				else
				{
					static String renameStringCache = {};
					ImGui::SetNextItemWidth(thumbnailSize - textPadding);

					if (renamingItem == 0)
					{
						renameStringCache = contentItemDesc.label;
						ImGui::SetKeyboardFocusHere();
					}

					ScopedStyleColor frameColor(ImGuiCol_FrameBg, IM_COL32(52, 53, 55, 255));
					ImGuiInputText(contentItemDesc.id, renameStringCache);

					if (!ImGui::IsItemActive() && renamingItem != 0)
					{
						if (!ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
						{
							state.newName = renameStringCache;
						}
						state.renameFinish = true;
						renamingItem = 0;
					}
					else if (renamingItem == 0)
					{
						renamingItem = contentItemDesc.id;
					}
				}
			}

			ImGui::Spring(1.0);
			ImGui::EndHorizontal();
			ImGui::Spring();
		}
		ImGui::EndVertical();


		if (contentItemDesc.selected)
		{
			//selected
			drawList->AddRect(ImVec2(screenCursorPos.x, screenCursorPos.y),
			                  ImVec2(screenCursorPos.x + thumbnailSize - 1, ImGui::GetCursorScreenPos().y - 1),
			                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.26f, 0.59f, 0.98f, 1.0f)),
			                  0.0f, 0, 2);
		}

		return state;
	}

	void ImGuiEndContentTable()
	{
		ImGui::EndTable();
	}

	void ImGuiRegisterResourceFieldVisibilityControl(TypeID typeId, StringView fieldName, FnResourceFieldVisibilityControl visibilityControl)
	{
		auto it = resourceVisibilityControl.Find(typeId);
		if (it == resourceVisibilityControl.end())
		{
			it = resourceVisibilityControl.Insert(typeId, ResourceFieldVisibilityControl{}).first;
		}
		it->second.resourceFieldVisibilityControls.Insert(fieldName, visibilityControl);
	}

	// Custom Basic Slider with flags
	bool ImGuiBasicSlider(const char* label, float* value, float min, float max, ImGuiBasicSliderFlags flags, const char* format)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext&     g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID     id = window->GetID(label);

		// Get flag states
		const bool show_input = !(flags & ImGuiBasicSliderFlags_NoInput);
		const bool highlight = (flags & ImGuiBasicSliderFlags_Highlight) != 0;
		const bool show_label = !(flags & ImGuiBasicSliderFlags_NoLabel);

		// Calculate the positions for the entire widget
		const float labelWidth = show_label ? ImGui::CalcTextSize(label).x : 0.0f;
		const float sliderWidth = 200.0f;
		const float valueWidth = 40.0f;
		const float spacing = 10.0f;
		const float height = 6.0f; // Thin slider track

		// Calculate positions
		const ImVec2 pos = window->DC.CursorPos;

		// Position for label (left)
		const ImVec2 labelPos = pos;

		// Position for slider (middle, adjusted if no label)
		const ImVec2 sliderStart(
			pos.x + (show_label ? labelWidth + spacing : 0.0f),
			pos.y + ImGui::GetTextLineHeight() / 2.0f - height / 2.0f
		);
		const ImVec2 sliderEnd(sliderStart.x + sliderWidth, sliderStart.y + height);

		// Position for value (right, fixed position, only if shown)
		const ImVec2 valuePos(sliderEnd.x + (show_input ? spacing : 0.0f), pos.y);

		// Define the bounding box for the entire widget
		ImRect total_bb(
			ImVec2(pos.x, pos.y),
			ImVec2(
				show_input ? valuePos.x + valueWidth : sliderEnd.x,
				pos.y + ImGui::GetTextLineHeight()
			)
		);

		// Increase the height of the BB for better interaction
		ImRect slider_bb(sliderStart, sliderEnd);
		ImRect slider_interact_bb = slider_bb;
		slider_interact_bb.Min.y -= 10;
		slider_interact_bb.Max.y += 10;

		ImGui::ItemSize(total_bb, style.FramePadding.y);
		if (!ImGui::ItemAdd(slider_interact_bb, id))
			return false;

		// Draw label if enabled
		if (show_label)
		{
			ImGui::RenderText(labelPos, label);
		}

		// Draw slider background (thin track)
		const float cornerRadius = 2.0f;
		ImU32       sliderBgColor = ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		window->DrawList->AddRectFilled(slider_bb.Min, slider_bb.Max, sliderBgColor, cornerRadius);

		// Calculate slider value position
		const float  t = (*value - min) / (max - min);
		const float  thumbRadius = 8.0f;
		const float  xPos = slider_bb.Min.x + t * (slider_bb.Max.x - slider_bb.Min.x);
		const ImVec2 thumbCenter(xPos, slider_bb.Min.y + height / 2.0f);

		// Interaction logic
		bool       value_changed = false;
		const bool is_hovered = ImGui::IsItemHovered();

		// Handle drag interaction
		if (g.ActiveId == id)
		{
			if (g.IO.MouseDown[0])
			{
				// Update value based on mouse position
				float new_t = ImSaturate((g.IO.MousePos.x - slider_bb.Min.x) / (slider_bb.Max.x - slider_bb.Min.x));
				float new_value = min + new_t * (max - min);

				if (*value != new_value)
				{
					*value = new_value;
					value_changed = true;
				}
			}
			else
			{
				// Release the active ID when mouse is released
				ImGui::ClearActiveID();
			}
		}
		else if (is_hovered && g.IO.MouseClicked[0])
		{
			// Set active ID when clicked
			ImGui::SetActiveID(id, window);
			ImGui::SetFocusID(id, window);

			// Initial value update on click
			float new_t = ImSaturate((g.IO.MousePos.x - slider_bb.Min.x) / (slider_bb.Max.x - slider_bb.Min.x));
			float new_value = min + new_t * (max - min);

			if (*value != new_value)
			{
				*value = new_value;
				value_changed = true;
			}
		}

		// Draw thumb (circle) with appropriate color based on highlight flag
		ImU32 thumbColor = highlight
			                   ? ImGui::GetColorU32(ImVec4(0.4f, 0.6f, 1.0f, 1.0f))
			                   :                                                   // Blue highlight color
			                   ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray color

		if (g.ActiveId == id || is_hovered)
		{
			// Make thumb slightly bigger on hover/active
			window->DrawList->AddCircleFilled(thumbCenter, thumbRadius + 2.0f, thumbColor);
		}
		else
		{
			window->DrawList->AddCircleFilled(thumbCenter, thumbRadius, thumbColor);
		}

		// Create the value input box at a fixed position (if enabled)
		if (show_input)
		{
			ImGui::PushID((ImGuiID)(id + 1));

			// Save cursor position
			ImVec2 oldCursorPos = ImGui::GetCursorScreenPos();

			// Set cursor to value position
			ImGui::SetCursorScreenPos(valuePos);

			// Style the value input box
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

			if (highlight)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
			}

			// Create the value input box
			char buf[32];
			snprintf(buf, sizeof(buf), format, *value);

			ImGui::PushItemWidth(valueWidth);
			if (ImGui::InputText("##value", buf, IM_ARRAYSIZE(buf),
			                     ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue))
			{
				float new_value = (float)atof(buf);
				if (new_value != *value)
				{
					*value = ImClamp(new_value, min, max);
					value_changed = true;
				}
			}

			ImGui::PopItemWidth();
			if (highlight)
			{
				ImGui::PopStyleColor();
			}
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			// Restore cursor position
			ImGui::SetCursorScreenPos(ImVec2(oldCursorPos.x, total_bb.Max.y + style.ItemSpacing.y));

			ImGui::PopID();
		}
		else
		{
			// If input is disabled, still move cursor past the widget
			ImGui::SetCursorPosY(total_bb.Max.y + style.ItemSpacing.y);
		}

		return value_changed;
	}

	bool ImGuiCollapsingHeaderProps(i32 id, const char* label, bool* buttonClicked)
	{
		auto& style = ImGui::GetStyle();

		ImGui::PushID(id);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowItemOverlap;
		ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
		bool   open = ImGui::CollapsingHeader(label, flags);
		bool   rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		bool   hovered = ImGui::IsItemHovered();
		ImVec2 size = ImGui::GetItemRectSize();

		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20 * style.ScaleFactor);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 * style.ScaleFactor);
		{
			ScopedStyleColor colBorder(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
			if (hovered)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
			}
			if (buttonClicked)
			{
				if (ImGui::Button(ICON_FA_ELLIPSIS_VERTICAL, ImVec2{size.y, size.y - 4 * style.ScaleFactor}) || rightClicked)
				{
					*buttonClicked = true;
				}
			}
			if (hovered)
			{
				ImGui::PopStyleColor(1);
			}
		}
		ImGui::PopID();

		return open;
	}

	void ImGuiDrawObject(const ImGuiDrawObjectInfo& info)
	{
		 char buffer[1024];

		if (!info.object) return;
		Object* object = info.object;

		auto it = objectTypeRenderers.Find(object->GetTypeId());
		if (it == objectTypeRenderers.end())
		{
			auto itVisibilityControl = objectVisibilityControl.Find(object->GetTypeId());

			ObjectTypeRenderer typeRenderer;
			typeRenderer.reflectType = Reflection::FindTypeById(object->GetTypeId());
			for (ReflectField* field : typeRenderer.reflectType->GetFields())
			{
				TypeID typeId = field->GetProps().typeId;

				ObjectTypeFieldRenderer& objectFieldRenderer = typeRenderer.fields.EmplaceBack(ObjectTypeFieldRenderer{
					.label = FormatName(field->GetName()),
					.reflectType = Reflection::FindTypeById(typeId),
					.reflectField = field,
				});

				if (itVisibilityControl != objectVisibilityControl.end())
				{
					auto itVisibilityControlField = itVisibilityControl->second.fieldVisibilityControls.Find(field->GetName());
					if (itVisibilityControlField != itVisibilityControl->second.fieldVisibilityControls.end())
					{
						objectFieldRenderer.fieldVisibilityControl = itVisibilityControlField->second;
					}
				}

				ImGuiDrawFieldDrawCheck check;
				check.fieldProps = field->GetProps();
				check.reflectField = field;
				check.reflectFieldType = objectFieldRenderer.reflectType;

				for (ImGuiFieldRenderer fieldRenderer : fieldRenderers)
				{
					if (fieldRenderer.canDrawField(check))
					{
						DrawFieldContext fieldContext = DrawFieldContext{
							.drawField = fieldRenderer.drawField
						};

						if (fieldRenderer.createCustomContext)
						{
							fieldContext.context = fieldRenderer.createCustomContext(check);
						}
						objectFieldRenderer.drawFn.EmplaceBack(fieldContext);
					}
				}
			}
			it = objectTypeRenderers.Insert(object->GetTypeId(), typeRenderer).first;
		}

		ObjectTypeRenderer& typeRenderer = it->second;
		if (!typeRenderer.fields.Empty())
		{
			if (ImGui::BeginTable("##object-table", 2))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

				u64 c = 0;

				for (ObjectTypeFieldRenderer& field : typeRenderer.fields)
				{
					c++;

					if (field.fieldVisibilityControl && !field.fieldVisibilityControl(object))
					{
						continue;
					}

					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();

					u64 id = 0;
					HashCombine(id, typeRenderer.reflectType->GetProps().typeId, HashValue(c));

					ImGui::BeginHorizontal(id, ImVec2(ImGui::GetColumnWidth(0), 0));
					ImGui::Text("%s", field.label.CStr());

					ImGui::Spring(1.0);

					// if (Button(ICON_FA_ARROWS_ROTATE))
					// {
					//
					// }

					ImGui::EndHorizontal();
					ImGui::TableNextColumn();

					ImGuiDrawFieldContext context;
					context.id = id + 1;
					context.fieldProps = field.reflectField->GetProps();
					context.reflectField = field.reflectField;
					context.reflectFieldType = field.reflectType;
					context.object = object;
					context.userData = info.userData;
					context.callback = info.callback;
					field.reflectField->Get(object, buffer, 1000);

					for (auto& drawField : field.drawFn)
					{
						context.customContext = drawField.context;
						drawField.drawField(context, buffer);
					}
				}
				ImGui::EndTable();
			}
		}
	}

	void ImGuiRegisterFieldRenderer(ImGuiFieldRenderer fieldRenderer)
	{
		fieldRenderers.EmplaceBack(fieldRenderer);
	}

	void ImGuiRegisterFieldVisibilityControl(TypeID typeId, StringView fieldName, FnObjectFieldVisibilityControl fieldVisibilityControl)
	{
		auto it = objectVisibilityControl.Find(typeId);
		if (it == objectVisibilityControl.end())
		{
			it = objectVisibilityControl.Insert(typeId, FieldVisibilityControl{}).first;
		}
		it->second.fieldVisibilityControls.Insert(String(fieldName), fieldVisibilityControl);
	}


	void ImGuiCommitFieldChanges(const ImGuiDrawFieldContext& context, VoidPtr pointer, usize size)
	{
		if (context.reflectField)
		{
			context.reflectField->Set(context.object, pointer, size);
		}

		if (context.rid && context.resourceField)
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope(!context.scopeName.Empty() ? context.scopeName : "Update Field");
			ResourceObject resourceObject = Resources::Write(context.rid);

			switch (context.resourceField->GetType())
			{
				case ResourceFieldType::String:
				case ResourceFieldType::Blob:
				case ResourceFieldType::SubObject:
				case ResourceFieldType::SubObjectSet:
					//TODO: not handled yet
					break;
				default:
					resourceObject.SetValue(context.resourceField->GetIndex(), pointer, size);
					break;
			}

			resourceObject.Commit(scope);
		}

		if (context.callback)
		{
			context.callback(context, pointer, size);
		}
	}

	void ImGuiDrawResource(const ImGuiDrawResourceInfo& drawResourceInfo)
	{
		char buffer[128];

		ResourceObject object = Resources::Read(drawResourceInfo.rid);
		if (!object) return;
		ResourceType* resourceType = object.GetType();

		auto it = resourceTypeRenders.Find(resourceType->GetID());
		if (it == resourceTypeRenders.end())
		{

			auto itVisibilityControl = resourceVisibilityControl.Find(resourceType->GetID());

			ResourceTypeRenderer typeRenderer;

			for (ResourceField* field: resourceType->GetFields())
			{
				ReflectType* reflectFieldType = nullptr;
				if (field->GetSubType() != 0)
				{
					reflectFieldType = Reflection::FindTypeById(field->GetSubType());
				}

				ImGuiDrawFieldDrawCheck check;
				check.fieldProps = field->GetProps();
				check.resourceFieldType = field->GetType();
				check.reflectFieldType = reflectFieldType;

				Array<DrawFieldContext> drawFieldContexts;

				for (ImGuiFieldRenderer fieldRenderer : fieldRenderers)
				{
					if (fieldRenderer.canDrawField(check))
					{
						DrawFieldContext fieldContext = DrawFieldContext{
							.drawField = fieldRenderer.drawField
						};

						if (fieldRenderer.createCustomContext)
						{
							fieldContext.context = fieldRenderer.createCustomContext(check);
						}

						drawFieldContexts.EmplaceBack(fieldContext);
					}
				}

				if (!drawFieldContexts.Empty())
				{

					ResourceFieldRenderer resourceFieldRenderer = {
						.index = field->GetIndex(),
						.label = FormatName(field->GetName()),
						.fieldProps = field->GetProps(),
						.reflectFieldType = reflectFieldType,
						.drawFn = drawFieldContexts
					};

					if (itVisibilityControl != resourceVisibilityControl.end())
					{
						auto itVisibilityControlField = itVisibilityControl->second.resourceFieldVisibilityControls.Find(field->GetName());
						if (itVisibilityControlField != itVisibilityControl->second.resourceFieldVisibilityControls.end())
						{
							resourceFieldRenderer.visibilityControl = itVisibilityControlField->second;
						}
					}

					typeRenderer.fields.EmplaceBack(Traits::Move(resourceFieldRenderer));
				}
			}
			it = resourceTypeRenders.Insert(resourceType->GetID(), typeRenderer).first;
		}

		ResourceTypeRenderer& typeRenderer = it->second;
		if (!typeRenderer.fields.Empty())
		{
			if (ImGui::BeginTable("##object-table", 2))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);

				u64 c = 0;

				for (ResourceFieldRenderer& field : typeRenderer.fields)
				{
					c++;
					ResourceField* resourceField = resourceType->GetFields()[field.index];

					if (field.visibilityControl && !field.visibilityControl(object))
					{
						continue;
					}

					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();

					u64 id = 0;
					HashCombine(id, resourceType->GetID(), HashValue(c));

					ImGui::BeginHorizontal(id, ImVec2(ImGui::GetColumnWidth(0), 0));
					ImGui::Text("%s", field.label.CStr());

					ImGui::Spring(1.0);

					// if (ImGui::Button(ICON_FA_ARROWS_ROTATE))
					// {
					//
					// }

					ImGui::EndHorizontal();
					ImGui::TableNextColumn();

					ImGuiDrawFieldContext context;
					context.id = id + 1;
					context.rid = drawResourceInfo.rid;
					context.fieldProps = field.fieldProps;
					context.userData = drawResourceInfo.userData;
					context.callback = drawResourceInfo.callback;
					context.reflectFieldType = field.reflectFieldType;
					context.resourceField = resourceField;
					context.scopeName = drawResourceInfo.scopeName;

					for (auto& drawField : field.drawFn)
					{
						context.customContext = drawField.context;

						switch (resourceField->GetType())
						{
							case ResourceFieldType::String:
							case ResourceFieldType::Blob:
							case ResourceFieldType::SubObject:
							case ResourceFieldType::SubObjectSet:
								break;
							case ResourceFieldType::ReferenceArray:
							{
								drawField.drawField(context, nullptr);
								break;
							}
							default:
								object.CopyValue(field.index, buffer, 1000);
								drawField.drawField(context, buffer);
								break;
						}
					}
				}

				ImGui::EndTable();
			}
		}
	}
}
