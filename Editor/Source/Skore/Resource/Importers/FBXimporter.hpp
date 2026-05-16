#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"


namespace Skore
{
	struct UndoRedoScope;

	struct FBXImportSettings
	{
		bool generateNormals     = true;
		bool recalculateTangents = true;
	};

	SK_API bool ImportFBX(RID directory, const FBXImportSettings& settings, StringView path, UndoRedoScope* scope);
}
