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

#include "SDL3/SDL_misc.h"
#include "Skore/Core/Reflection.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Scripting/PkPyScriptingEngine.hpp"

namespace Skore
{
	struct PkPyHandler : ResourceAssetHandler
	{
		SK_CLASS(PkPyHandler, ResourceAssetHandler);

		StringView Extension() override
		{
			return ".py";
		}

		void OpenAsset(RID asset) override
		{
			SDL_OpenURL(ResourceAssets::GetAbsolutePath(asset).CStr());
		}

		TypeID GetResourceTypeId() override
		{
			return TypeInfo<PkPyScriptResource>::ID();
		}

		StringView GetDesc() override
		{
			return "Python Script";
		}

		RID Load(RID asset, StringView absolutePath) override
		{
			String source = FileSystem::ReadFileAsString(absolutePath);
			String file = Path::Name(absolutePath) + Path::Extension(absolutePath);

			RID script = Resources::Create<PkPyScriptResource>();

			ResourceObject scriptObject = Resources::Write(script);
			scriptObject.SetString(PkPyScriptResource::Name, file);
			scriptObject.SetString(PkPyScriptResource::Source, source);
			scriptObject.Commit();

			ResourceAssets::WatchAsset(asset, absolutePath);

			return script;
		}

		void Reloaded(RID asset, StringView absolutePath) override
		{
			if (RID object = Resources::Read(asset).GetSubObject(ResourceAsset::Object))
			{
				String source = FileSystem::ReadFileAsString(absolutePath);
				String file = Path::Name(absolutePath) + Path::Extension(absolutePath);

				ResourceObject scriptObject = Resources::Write(object);
				scriptObject.SetString(PkPyScriptResource::Name, file);
				scriptObject.SetString(PkPyScriptResource::Source, source);
				scriptObject.Commit();
			}
		}

		void Save(RID object, StringView absolutePath) override
		{
			if (!FileSystem::GetFileStatus(absolutePath).exists)
			{
				FileSystem::SaveFileAsString(absolutePath, "");
			}
		}
	};

	void RegisterPkPyHandler()
	{
		Reflection::Type<PkPyHandler>();
	}
}
