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

#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

#define SK_ASSET_PAYLOAD "sk-asset-payload"
#define SK_ENTITY_PAYLOAD "sk-entity-payload"
#define SK_IMPORT_EXTENSION ".import"
#define SK_INFO_EXTENSION ".info"
#define SK_ASSET_EXTENSION ".asset"
#define SK_PROJECT_EXTENSION ".skore"

namespace Skore
{
	class AssetFileOld;

	using OnEntitySelection = EventType<"Skore::Editor::OnEntitySelection"_h, void(u32, UUID)>;
	using OnEntityDeselection = EventType<"Skore::Editor::OnEntityDeselection"_h, void(u32, UUID)>;

	using OnEntityRIDSelection = EventType<"Skore::Editor::OnEntityRIDSelection"_h, void(u32, RID)>;
	using OnEntityRIDDeselection = EventType<"Skore::Editor::OnEntityRIDDeselection"_h, void(u32, RID)>;

	using OnAssetSelection = EventType<"Skore::Editor::OnAssetSelection"_h, void(AssetFileOld*)>;

	enum class DockPosition
	{
		None        = 0,
		Center      = 1,
		Left        = 2,
		RightTop    = 3,
		RightBottom = 4,
		BottomLeft  = 5,
		BottomRight = 6
	};

	enum class PlayMode
	{
		Editing    = 0,
		Paused     = 1,
		Simulating = 2
	};

	struct EditorWindowProperties
	{
		DockPosition dockPosition{};
		bool         createOnInit{};
		i32          order = 0;
	};

	struct EditorWindow : Object
	{
		SK_CLASS(EditorWindow, Object);

		virtual void Init(u32 id, VoidPtr userData) {}
		virtual void Draw(u32 id, bool& open) = 0;
	};

	struct AssetPayload
	{
		AssetFileOld* assetFile = nullptr;
		TypeID     assetType = 0;
	};
}
