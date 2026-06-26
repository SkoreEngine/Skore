#pragma once

#include "Skore/Common.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	class EditorWorkspace;

	class SK_API MaterialEditor
	{
	public:
		explicit MaterialEditor(EditorWorkspace& workspace);

		void OpenMaterialGraph(RID graph);
		RID  GetMaterialGraph() const;

	private:
		EditorWorkspace& m_workspace;
		RID              m_graph = {};
	};
}
