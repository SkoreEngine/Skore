#include "Skore/MaterialEditor.hpp"

namespace Skore
{
	MaterialEditor::MaterialEditor(EditorWorkspace& workspace) : m_workspace(workspace) {}

	void MaterialEditor::OpenMaterialGraph(RID graph)
	{
		m_graph = graph;
	}

	RID MaterialEditor::GetMaterialGraph() const
	{
		return m_graph;
	}
}
