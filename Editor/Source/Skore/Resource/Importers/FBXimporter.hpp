#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"


namespace Skore
{
	struct UndoRedoScope;
	struct CookContext;

	struct FBXImportSettings
	{
		bool generateNormals     = true;
		bool recalculateTangents = true;
	};

	SK_API bool ImportFBX(CookContext& cookCtx, const FBXImportSettings& settings, StringView path);
}
