#include "Skore/Graphics/UIRenderer.hpp"
#include "Skore/Graphics/DrawList.hpp"

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsCommon.hpp"
#include "Skore/Graphics/RenderResourceCache.hpp"
#include "Skore/Graphics/RenderSceneObjects.hpp"

#include "Skore/App.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Components/UIComponents.hpp"

namespace Skore
{
	struct UIRendererInternal
	{
		DrawListContext* drawList = nullptr;
	};

	namespace
	{
		Logger& logger = Logger::GetLogger("UIRenderer");

		Clay_Context* clayContext = nullptr;
		VoidPtr       clayArena = nullptr;
		RID           defaultFont;

		void HandleClayErrors(Clay_ErrorData errorData)
		{
			logger.Error("{}", errorData.errorText.chars);
		}

		Color CastColor(const Clay_Color& color)
		{
			return Color(color.r, color.g, color.b, color.a);
		}
	}

	// Calculate text dimensions using DrawList::MeasureText
	Clay_Dimensions MeasureText(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData)
	{
		RID font = defaultFont;
		if (FontResourceCache* fontCache = static_cast<FontResourceCache*>(config->userData))
		{
			font = fontCache->rid;
		}

		StringView textView(text.chars, static_cast<usize>(text.length));

		Vec2 dimensions = DrawList::MeasureText(font, config->fontSize, textView);

		return {.width = dimensions.x, .height = dimensions.y};
	}

	void UIRenderInit()
	{
		Allocator* heapAlloc = MemoryGlobals::GetHeapAllocator();
		static u64 totalMemorySize = Clay_MinMemorySize();

		clayArena = heapAlloc->MemAlloc(heapAlloc->allocator, totalMemorySize);
		Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, clayArena);
		clayContext = Clay_Initialize(arena, {800, 600}, static_cast<Clay_ErrorHandler>(HandleClayErrors));
		Clay_SetMeasureTextFunction(MeasureText, nullptr);

		defaultFont = Resources::FindByPath("Skore://Fonts/DejaVuSans.font");
	}

	void UIRenderShutdown()
	{
		Allocator* heapAlloc = MemoryGlobals::GetHeapAllocator();
		heapAlloc->MemFree(heapAlloc->allocator, clayArena);
	}

	f32 GetUIScale()
	{
		RID settings = Settings::Get(TypeInfo<ProjectSettings>::ID(), TypeInfo<UISettings>::ID());
		ResourceObject settingsObject = Resources::Read(settings);
		f32 dpi =	Platform::GetWindowDPI(Graphics::GetWindow());

		if (settingsObject)
		{

			f32 scale = settingsObject.GetFloat(UISettings::GlobalScale);
			if (settingsObject.GetBool(UISettings::DPIScaling))
			{
				scale *= dpi;
			}
			return scale;
		}
		return dpi;
	}


	UIRenderer::UIRenderer()
	{
		static_assert(sizeof(m_context) >= sizeof(UIRendererInternal));

		UIRendererInternal* internal = new(m_context) UIRendererInternal{};
		internal->drawList = DrawList::CreateContext();
	}

	UIRenderer::~UIRenderer()
	{
		UIRendererInternal* internal = reinterpret_cast<UIRendererInternal*>(m_context);
		DrawList::DestroyContext(internal->drawList);
	}

	void UIRenderer::SetDebugModeEnabled(bool enabled)
	{
		UIRendererInternal* internal = reinterpret_cast<UIRendererInternal*>(m_context);
		Clay_SetDebugModeEnabled(enabled);
	}

	bool UIRenderer::IsDebugModeEnabled() const
	{
		const UIRendererInternal* internal = reinterpret_cast<const UIRendererInternal*>(m_context);
		return Clay_IsDebugModeEnabled();
	}


	void UIRenderer::DrawUI(Scene* scene, DrawUICursorState cursorState, GPURenderPass* renderPass, Extent extent, GPUCommandBuffer* cmd)
	{
		UIRendererInternal* internal = reinterpret_cast<UIRendererInternal*>(m_context);
		f32    scale = 1.0f / GetUIScale();
		Clay_SetLayoutDimensions({static_cast<f32>(extent.width) * scale, static_cast<f32>(extent.height) * scale});

		Clay_SetPointerState({cursorState.position.x * scale, cursorState.position.y * scale}, cursorState.cursorDown);
		Clay_UpdateScrollContainers(true, {cursorState.mouseWheel.x, cursorState.mouseWheel.y}, App::DeltaTime());

		if (scene)
		{
			for (auto it : scene->renderObjects.canvasList)
			{
				if (!it->entity->IsActive())
				{
					continue;
				}

				Clay_BeginLayout();

				EntityEventDesc eventDesc;
				eventDesc.type = EntityEventType::ProcessUILayout;
				it->entity->NotifyEvent(eventDesc, false);

				Clay_RenderCommandArray renderCommands = Clay_EndLayout();

				for (int i = 0; i < renderCommands.length; i++)
				{
					Clay_RenderCommand* renderCommand = &renderCommands.internalArray[i];

					Vec2 min = Vec2(renderCommand->boundingBox.x, renderCommand->boundingBox.y);
					Vec2 max = Vec2(renderCommand->boundingBox.x + renderCommand->boundingBox.width,
					                renderCommand->boundingBox.y + renderCommand->boundingBox.height);

					min /= scale;
					max /= scale;

					switch (renderCommand->commandType)
					{
						case CLAY_RENDER_COMMAND_TYPE_NONE:
							break;
						case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
						{
							DrawList::AddRectFilled(internal->drawList, min, max, CastColor(renderCommand->renderData.rectangle.backgroundColor));
							break;
						}
						case CLAY_RENDER_COMMAND_TYPE_TEXT:
						{
							RID font = defaultFont;
							if (FontResourceCache* fontCache = static_cast<FontResourceCache*>(renderCommand->userData))
							{
								font = fontCache->rid;
							}

							StringView str = StringView(renderCommand->renderData.text.stringContents.chars, static_cast<usize>(renderCommand->renderData.text.stringContents.length));
							DrawList::AddText(internal->drawList,
							                  font,
							                  min,
							                  renderCommand->renderData.text.fontSize / scale,
							                  str,
							                  CastColor(renderCommand->renderData.text.textColor));
							break;
						}
						case CLAY_RENDER_COMMAND_TYPE_IMAGE:
						{
							TextureResourceCache* textureCache = static_cast<TextureResourceCache*>(renderCommand->renderData.image.imageData);
							DrawList::AddImage(internal->drawList, textureCache->texture, min, max);
							break;
						}
						case CLAY_RENDER_COMMAND_TYPE_BORDER:
						{
							Clay_BorderRenderData* borderData = &renderCommand->renderData.border;
							Color borderColor = CastColor(borderData->color);

							// Draw border as four separate rectangles (top, right, bottom, left)
							f32 borderWidth = borderData->cornerRadius.bottomLeft / scale;

							// Top border
							if (borderData->cornerRadius.topRight > 0) {
								f32 topWidth = borderData->cornerRadius.topRight / scale;
								DrawList::AddRectFilled(internal->drawList, 
									Vec2(min.x, min.y), 
									Vec2(max.x, min.y + topWidth), 
									borderColor);
							}

							// Right border  
							if (borderData->cornerRadius.topLeft > 0) {
								f32 rightWidth = borderData->cornerRadius.topLeft / scale;
								DrawList::AddRectFilled(internal->drawList,
									Vec2(max.x - rightWidth, min.y),
									Vec2(max.x, max.y),
									borderColor);
							}

							// Bottom border
							if (borderData->cornerRadius.bottomRight > 0) {
								f32 bottomWidth = borderData->cornerRadius.bottomRight / scale;
								DrawList::AddRectFilled(internal->drawList,
									Vec2(min.x, max.y - bottomWidth),
									Vec2(max.x, max.y),
									borderColor);
							}

							// Left border
							if (borderData->cornerRadius.bottomLeft > 0) {
								f32 leftWidth = borderData->cornerRadius.bottomLeft / scale;
								DrawList::AddRectFilled(internal->drawList,
									Vec2(min.x, min.y),
									Vec2(min.x + leftWidth, max.y),
									borderColor);
							}
							break;
						}
						case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
						{
							DrawList::PushScissorRect(internal->drawList, min, max);
							break;
						}
						case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
						{
							DrawList::PopScissorRect(internal->drawList);
							break;
						}
						// case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
						default:
							//ogger.Error("Unknown render command type: {}", static_cast<i32>(renderCommand->commandType));
							break;
					}
				}
			}
		}
		DrawList::Flush(internal->drawList, renderPass, extent, cmd);
	}
}