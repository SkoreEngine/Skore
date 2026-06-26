#include "Skore/ImGui/GraphEditor.hpp"

#include "Skore/ImGui/ImGui.hpp"
#include "imgui_internal.h"

namespace Skore
{
	void GraphEditor::Begin(const char* id)
	{
		m_frameNodes.Clear();
		m_framePins.Clear();
		m_frameLinks.Clear();
		m_currentNodeIndex = -1;

		// Snapshot selection before this frame's interaction
		m_prevSelectedNodes = m_selectedNodes;
		m_prevSelectedLinks = m_selectedLinks;
	}

	void GraphEditor::BeginNode(u64 id, const char* name, Vec2 position, const GraphNodeDesc& desc)
	{
		FrameNode node{};
		node.id = id;
		node.name = name;
		node.position = position;
		node.inputPosition = position;
		node.desc = desc;
		node.inputStart = (u32)m_framePins.Size();
		node.inputCount = 0;
		node.outputStart = 0;
		node.outputCount = 0;
		m_frameNodes.EmplaceBack(node);
		m_currentNodeIndex = (i32)m_frameNodes.Size() - 1;
	}

	void GraphEditor::InputPin(const char* name, GraphPinType type, ImColor color)
	{
		if (m_currentNodeIndex < 0) return;
		FramePin pin{};
		pin.name = name;
		pin.type = type;
		pin.color = color;
		m_framePins.EmplaceBack(pin);
		m_frameNodes[m_currentNodeIndex].inputCount++;
	}

	void GraphEditor::OutputPin(const char* name, GraphPinType type, ImColor color)
	{
		if (m_currentNodeIndex < 0) return;
		FramePin pin{};
		pin.name = name;
		pin.type = type;
		pin.color = color;
		m_framePins.EmplaceBack(pin);
		m_frameNodes[m_currentNodeIndex].outputCount++;
	}

	void GraphEditor::NodeThumbnail(ImTextureID texture)
	{
		if (m_currentNodeIndex < 0) return;
		m_frameNodes[m_currentNodeIndex].thumbnail = texture;
	}

	void GraphEditor::EndNode()
	{
		if (m_currentNodeIndex < 0) return;
		FrameNode& node = m_frameNodes[m_currentNodeIndex];
		node.outputStart = node.inputStart + node.inputCount;
		m_currentNodeIndex = -1;
	}

	void GraphEditor::PinWidgetDragFloat(f32* value, f32 speed)
	{
		if (m_currentNodeIndex < 0 || m_framePins.Empty()) return;
		FramePin& pin = m_framePins.Back();
		pin.widgetType = GraphWidgetType::InputFloat;
		pin.floatPtr = value;
		pin.dragSpeed = speed;
	}

	void GraphEditor::PinWidgetDragFloatN(f32* values, u32 count, f32 speed)
	{
		if (m_currentNodeIndex < 0 || m_framePins.Empty()) return;
		FramePin& pin = m_framePins.Back();
		pin.widgetType = GraphWidgetType::DragFloatN;
		pin.floatPtr = values;
		pin.floatCount = count < 1 ? 1 : count;
		pin.dragSpeed = speed;
	}

	void GraphEditor::PinWidgetColor(f32* rgb)
	{
		if (m_currentNodeIndex < 0 || m_framePins.Empty()) return;
		FramePin& pin = m_framePins.Back();
		pin.widgetType = GraphWidgetType::Color;
		pin.floatPtr = rgb;
		pin.floatCount = 3;
	}

	void GraphEditor::PinWidgetInputText(char* buffer, u32 bufferSize)
	{
		if (m_currentNodeIndex < 0 || m_framePins.Empty()) return;
		FramePin& pin = m_framePins.Back();
		pin.widgetType = GraphWidgetType::InputText;
		pin.textBuffer = buffer;
		pin.textBufferSize = bufferSize;
	}

	void GraphEditor::PinWidgetCheckbox(bool* value)
	{
		if (m_currentNodeIndex < 0 || m_framePins.Empty()) return;
		FramePin& pin = m_framePins.Back();
		pin.widgetType = GraphWidgetType::Checkbox;
		pin.boolPtr = value;
	}

	void GraphEditor::PinWidgetCombo(i32* selectedIndex, const char* options)
	{
		if (m_currentNodeIndex < 0 || m_framePins.Empty()) return;
		FramePin& pin = m_framePins.Back();
		pin.widgetType = GraphWidgetType::Combo;
		pin.comboIndexPtr = selectedIndex;
		pin.comboOptions = options;
	}

	void GraphEditor::Link(u64 id, u64 outputNodeId, u32 outputPinIndex, u64 inputNodeId, u32 inputPinIndex, ImColor color)
	{
		FrameLink link{};
		link.id = id;
		link.outputNodeId = outputNodeId;
		link.outputPinIndex = outputPinIndex;
		link.inputNodeId = inputNodeId;
		link.inputPinIndex = inputPinIndex;
		link.color = color;
		m_frameLinks.EmplaceBack(link);
	}

	GraphEditorResult GraphEditor::End()
	{
		GraphEditorResult result{};

		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImGui::GetContentRegionAvail();

		if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
		{
			return result;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImGuiIO& io = ImGui::GetIO();
		m_scale = ImGui::GetStyle().ScaleFactor;
		m_rowSpacing = ImGui::GetFrameHeightWithSpacing();

		ImVec2 screenMouse = io.MousePos;
		bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);

		// Zoom
		if (canvasHovered && io.MouseWheel != 0.0f)
		{
			f32 oldZoom = m_zoom;
			m_zoom = Math::Clamp(m_zoom * powf(1.1f, io.MouseWheel), ZoomMin, ZoomMax);

			f32 mouseLocalX = screenMouse.x - canvasPos.x;
			f32 mouseLocalY = screenMouse.y - canvasPos.y;
			f32 canvasPointX = (mouseLocalX - m_scrollOffset.x) / oldZoom;
			f32 canvasPointY = (mouseLocalY - m_scrollOffset.y) / oldZoom;
			m_scrollOffset.x = mouseLocalX - canvasPointX * m_zoom;
			m_scrollOffset.y = mouseLocalY - canvasPointY * m_zoom;
		}

		// Pan with middle mouse
		if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
		{
			m_draggingCanvas = true;
			m_canvasDragStart = {screenMouse.x, screenMouse.y};
		}

		if (m_draggingCanvas)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			{
				m_scrollOffset.x += screenMouse.x - m_canvasDragStart.x;
				m_scrollOffset.y += screenMouse.y - m_canvasDragStart.y;
				m_canvasDragStart = {screenMouse.x, screenMouse.y};
			}
			else
			{
				m_draggingCanvas = false;
			}
		}

		// Background
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(15, 15, 15, 255));

		// Begin canvas
		if (!m_canvas.Begin("graph_canvas", canvasSize))
		{
			return result;
		}
		m_canvas.SetView(ImVec2(m_scrollOffset.x, m_scrollOffset.y), m_zoom);

		// Grid in screen space
		m_canvas.Suspend();
		DrawGrid(drawList, canvasPos, canvasSize);
		m_canvas.Resume();

		// Compute all node sizes before interaction (needed for hit testing)
		for (auto& node : m_frameNodes)
		{
			ComputeNodeSize(node);
		}

		// Interaction
		HandleInteraction(result);

		// Node under the mouse this frame (world coords), used by callers for per-node drop targets.
		if (i32 hoveredIdx = FindHoveredNode(); hoveredIdx >= 0)
		{
			const FrameNode& hovered = m_frameNodes[hoveredIdx];
			result.hoveredNodeId = hovered.id;
			result.hoveredNodeMin = hovered.position;
			result.hoveredNodeMax = hovered.position + hovered.size;
		}

		// Detect selection changes
		bool selChanged = (m_selectedNodes.Size() != m_prevSelectedNodes.Size()) ||
		                  (m_selectedLinks.Size() != m_prevSelectedLinks.Size());

		if (!selChanged)
		{
			for (u32 i = 0; i < m_selectedNodes.Size(); i++)
			{
				if (m_selectedNodes[i] != m_prevSelectedNodes[i]) { selChanged = true; break; }
			}
		}
		if (!selChanged)
		{
			for (u32 i = 0; i < m_selectedLinks.Size(); i++)
			{
				if (m_selectedLinks[i] != m_prevSelectedLinks[i]) { selChanged = true; break; }
			}
		}

		if (selChanged)
		{
			result.hasSelectionChanged = true;
			result.selectionChanged.oldSelectedNodes = m_prevSelectedNodes;
			result.selectionChanged.oldSelectedLinks = m_prevSelectedLinks;
			result.selectionChanged.newSelectedNodes = m_selectedNodes;
			result.selectionChanged.newSelectedLinks = m_selectedLinks;
		}

		// Draw links
		for (u32 i = 0; i < m_frameLinks.Size(); i++)
		{
			const FrameNode* outNode = FindNodeById(m_frameLinks[i].outputNodeId);
			const FrameNode* inNode = FindNodeById(m_frameLinks[i].inputNodeId);

			bool isStateMachineLink = outNode && inNode &&
				outNode->desc.nodeType == GraphNodeType::StateMachine &&
				inNode->desc.nodeType == GraphNodeType::StateMachine;

			if (isStateMachineLink)
			{
				DrawStateMachineLink(drawList, m_frameLinks[i], IsLinkSelected(m_frameLinks[i].id));
			}
			else
			{
				DrawLink(drawList, m_frameLinks[i], IsLinkSelected(m_frameLinks[i].id));
			}
		}

		// In-progress link
		if (m_creatingLink)
		{
			const FrameNode* startNode = FindNodeById(m_linkStartNodeId);
			if (startNode)
			{
				ImVec2 mousePos = ImGui::GetMousePos();
				Vec2 endPos = {mousePos.x, mousePos.y};

				if (startNode->desc.nodeType == GraphNodeType::StateMachine)
				{
					Vec2 startPos = GetRectEdgePoint(*startNode, endPos);
					drawList->AddLine(ImVec2(startPos.x, startPos.y), ImVec2(endPos.x, endPos.y),
						IM_COL32(200, 200, 100, 180), LinkThickness * m_scale);
				}
				else
				{
					Vec2 startPos = GetPinPos(*startNode, m_linkStartKind, m_linkStartPinIndex);

					f32 dist = Math::Abs(endPos.x - startPos.x) * 0.5f;
					f32 tangent = Math::Max(dist, 50.0f * m_scale);

					ImVec2 p1(startPos.x, startPos.y);
					ImVec2 p4(endPos.x, endPos.y);
					ImVec2 p2, p3;

					if (m_linkStartKind == GraphPinKind::Output)
					{
						p2 = ImVec2(p1.x + tangent, p1.y);
						p3 = ImVec2(p4.x - tangent, p4.y);
					}
					else
					{
						p2 = ImVec2(p1.x - tangent, p1.y);
						p3 = ImVec2(p4.x + tangent, p4.y);
					}

					drawList->AddBezierCubic(p1, p2, p3, p4, IM_COL32(200, 200, 100, 180), LinkThickness * m_scale);
				}
			}
		}

		// Draw node shapes
		for (auto& node : m_frameNodes)
		{
			if (node.desc.nodeType == GraphNodeType::StateMachine)
				DrawStateMachineNode(drawList, node);
			else
				DrawNode(drawList, node);
		}

		// Draw text in screen space for crisp rendering
		m_canvas.Suspend();
		for (const auto& node : m_frameNodes)
		{
			if (node.desc.nodeType == GraphNodeType::StateMachine)
				DrawStateMachineNodeText(drawList, node);
			else
				DrawNodeText(drawList, node);
		}
		m_canvas.Resume();

		// Draw widgets in canvas space
		m_nodeWidgetActive = false;
		m_nodeWidgetHovered = false;
		m_hasActiveValuePin = false;
		m_committedPinValues.Clear();
		ImGui::SetFontRasterizerDensity(m_zoom);
		for (auto& node : m_frameNodes)
		{
			if (node.desc.nodeType == GraphNodeType::Blueprint)
				DrawNodeWidgets(node);
		}
		ImGui::SetFontRasterizerDensity(1.0f);

		result.pinValueActive = m_hasActiveValuePin;
		result.activePinValue = m_activeValuePin;
		result.committedPinValues = m_committedPinValues;

		// Box selection rectangle
		if (m_boxSelecting)
		{
			ImVec2 bMin(Math::Min(m_boxSelectStart.x, m_boxSelectEnd.x), Math::Min(m_boxSelectStart.y, m_boxSelectEnd.y));
			ImVec2 bMax(Math::Max(m_boxSelectStart.x, m_boxSelectEnd.x), Math::Max(m_boxSelectStart.y, m_boxSelectEnd.y));
			drawList->AddRectFilled(bMin, bMax, IM_COL32(255, 165, 0, 30));
			drawList->AddRect(bMin, bMax, IM_COL32(255, 165, 0, 200), 0.0f, 0, 1.5f);
		}

		m_canvas.End();

		return result;
	}

	// --- Queries ---

	bool GraphEditor::IsNodeSelected(u64 nodeId) const
	{
		for (u64 id : m_selectedNodes)
		{
			if (id == nodeId) return true;
		}
		return false;
	}

	bool GraphEditor::IsLinkSelected(u64 linkId) const
	{
		for (u64 id : m_selectedLinks)
		{
			if (id == linkId) return true;
		}
		return false;
	}

	void GraphEditor::ClearSelection()
	{
		m_selectedNodes.Clear();
		m_selectedLinks.Clear();
	}

	void GraphEditor::SetSelectedNodes(const Array<u64>& nodes)
	{
		m_selectedNodes = nodes;
	}

	void GraphEditor::SetSelectedLinks(const Array<u64>& links)
	{
		m_selectedLinks = links;
	}

	void GraphEditor::StartLinkFromNode(u64 nodeId)
	{
		m_creatingLink = true;
		m_linkStartNodeId = nodeId;
		m_linkStartPinIndex = 0;
		m_linkStartKind = GraphPinKind::Output;
	}

	// --- Internal helpers ---

	GraphEditor::FrameNode* GraphEditor::FindNodeById(u64 id)
	{
		for (auto& node : m_frameNodes)
		{
			if (node.id == id) return &node;
		}
		return nullptr;
	}

	const GraphEditor::FrameNode* GraphEditor::FindNodeById(u64 id) const
	{
		for (const auto& node : m_frameNodes)
		{
			if (node.id == id) return &node;
		}
		return nullptr;
	}

	Vec2 GraphEditor::GetPinPos(const FrameNode& node, GraphPinKind kind, u32 pinIndex) const
	{
		f32 s = m_scale;
		f32 headerH = NodeHeaderHeight * s;
		f32 padding = NodePadding * s;
		f32 spacing = m_rowSpacing;

		// Count pins by type
		u32 flowInputCount = 0, flowOutputCount = 0;
		u32 valueOutputCount = 0;

		for (u32 i = 0; i < node.inputCount; i++)
		{
			if (m_framePins[node.inputStart + i].type == GraphPinType::Flow) flowInputCount++;
		}
		for (u32 i = 0; i < node.outputCount; i++)
		{
			if (m_framePins[node.outputStart + i].type == GraphPinType::Flow) flowOutputCount++;
			else valueOutputCount++;
		}

		u32 flowRows = Math::Max(flowInputCount, flowOutputCount);

		f32 flowStartY = node.position.y + headerH + padding;
		f32 valueOutputStartY = flowStartY + flowRows * spacing;
		f32 valueInputStartY = valueOutputStartY + valueOutputCount * spacing;

		u32 pinStart = (kind == GraphPinKind::Input) ? node.inputStart : node.outputStart;
		const FramePin& pin = m_framePins[pinStart + pinIndex];

		// Count same-type pins before this one
		u32 visualIdx = 0;
		for (u32 i = 0; i < pinIndex; i++)
		{
			const FramePin& p = m_framePins[pinStart + i];
			if (pin.type == GraphPinType::Flow)
			{
				if (p.type == GraphPinType::Flow) visualIdx++;
			}
			else
			{
				if (p.type != GraphPinType::Flow) visualIdx++;
			}
		}

		f32 y;
		if (pin.type == GraphPinType::Flow)
		{
			y = flowStartY + visualIdx * spacing + spacing * 0.5f;
		}
		else if (kind == GraphPinKind::Output)
		{
			y = valueOutputStartY + visualIdx * spacing + spacing * 0.5f;
		}
		else
		{
			y = valueInputStartY + visualIdx * spacing + spacing * 0.5f;
		}

		f32 x = (kind == GraphPinKind::Input) ? node.position.x : node.position.x + node.size.x;
		return {x, y};
	}

	bool GraphEditor::FindHoveredPin(u64& outNodeId, GraphPinKind& outKind, u32& outPinIndex) const
	{
		ImVec2 mousePos = ImGui::GetMousePos();
		Vec2 mouse = {mousePos.x, mousePos.y};
		f32 hitRadius = (PinRadius + 4.0f) * m_scale;

		for (const auto& node : m_frameNodes)
		{
			if (node.desc.nodeType == GraphNodeType::StateMachine) continue;

			for (u32 i = 0; i < node.inputCount; i++)
			{
				Vec2 pinPos = GetPinPos(node, GraphPinKind::Input, i);
				if (Vec2::Distance(mouse, pinPos) <= hitRadius)
				{
					outNodeId = node.id;
					outKind = GraphPinKind::Input;
					outPinIndex = i;
					return true;
				}
			}
			for (u32 i = 0; i < node.outputCount; i++)
			{
				Vec2 pinPos = GetPinPos(node, GraphPinKind::Output, i);
				if (Vec2::Distance(mouse, pinPos) <= hitRadius)
				{
					outNodeId = node.id;
					outKind = GraphPinKind::Output;
					outPinIndex = i;
					return true;
				}
			}
		}
		return false;
	}

	i32 GraphEditor::FindHoveredNode() const
	{
		ImVec2 mousePos = ImGui::GetMousePos();
		Vec2 mouse = {mousePos.x, mousePos.y};

		for (i32 i = (i32)m_frameNodes.Size() - 1; i >= 0; i--)
		{
			const auto& node = m_frameNodes[i];
			if (mouse.x >= node.position.x && mouse.x <= node.position.x + node.size.x &&
				mouse.y >= node.position.y && mouse.y <= node.position.y + node.size.y)
			{
				return i;
			}
		}
		return -1;
	}

	i32 GraphEditor::FindHoveredLink() const
	{
		ImVec2 mousePos = ImGui::GetMousePos();
		Vec2 mouse = {mousePos.x, mousePos.y};
		f32 bestDist = LinkHitDistance * m_scale;
		i32 bestLink = -1;
		for (i32 i = 0; i < (i32)m_frameLinks.Size(); i++)
		{
			f32 d = DistanceToLink(m_frameLinks[i], mouse);
			if (d < bestDist)
			{
				bestDist = d;
				bestLink = i;
			}
		}
		return bestLink;
	}

	// --- Size computation ---

	void GraphEditor::ComputeNodeSize(FrameNode& node)
	{
		f32 s = m_scale;
		f32 fontSize = ImGui::GetFontSize();

		if (node.desc.nodeType == GraphNodeType::StateMachine)
		{
			f32 padding = NodePadding * s * 2.0f;
			ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, node.name.CStr());
			f32 nodeW = Math::Max(StateMachineMinWidth * s, titleSize.x + padding * 2.0f);
			f32 nodeH = Math::Max(StateMachineMinHeight * s, titleSize.y + padding);
			node.size = {nodeW, nodeH};
			return;
		}

		f32 headerH = NodeHeaderHeight * s;
		f32 padding = NodePadding * s;
		f32 spacing = m_rowSpacing;
		f32 pinR = PinRadius * s;
		f32 gap = 6.0f * s;

		u32 flowInputCount = 0, flowOutputCount = 0;
		u32 valueInputCount = 0, valueOutputCount = 0;

		for (u32 i = 0; i < node.inputCount; i++)
		{
			if (m_framePins[node.inputStart + i].type == GraphPinType::Flow) flowInputCount++;
			else valueInputCount++;
		}
		for (u32 i = 0; i < node.outputCount; i++)
		{
			if (m_framePins[node.outputStart + i].type == GraphPinType::Flow) flowOutputCount++;
			else valueOutputCount++;
		}

		u32 flowRows = Math::Max(flowInputCount, flowOutputCount);
		f32 contentHeight = padding + flowRows * spacing + valueOutputCount * spacing + valueInputCount * spacing + padding;

		f32 pinArea = pinR + gap;
		ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, node.name.CStr());

		f32 maxOutputLabelW = 0.0f;
		for (u32 i = 0; i < node.outputCount; i++)
		{
			const char* pinName = m_framePins[node.outputStart + i].name.CStr();
			if (pinName && pinName[0] != '\0')
			{
				ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, pinName);
				maxOutputLabelW = Math::Max(maxOutputLabelW, sz.x);
			}
		}

		f32 maxInputLabelW = 0.0f;
		bool hasInputWidgets = false;
		f32 maxWidgetW = 0.0f;
		for (u32 i = 0; i < node.inputCount; i++)
		{
			const FramePin& pin = m_framePins[node.inputStart + i];
			if (pin.name && pin.name[0] != '\0')
			{
				ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, pin.name.CStr());
				maxInputLabelW = Math::Max(maxInputLabelW, sz.x);
			}
			if (pin.widgetType != GraphWidgetType::None)
			{
				hasInputWidgets = true;
				f32 wantW = 60.0f;
				switch (pin.widgetType)
				{
					case GraphWidgetType::Color:      wantW = 40.0f; break;
					case GraphWidgetType::Checkbox:   wantW = 24.0f; break;
					case GraphWidgetType::Combo:      wantW = 90.0f; break;
					case GraphWidgetType::DragFloatN: wantW = pin.floatCount <= 1 ? 55.0f : (pin.floatCount == 2 ? 84.0f : (pin.floatCount == 3 ? 112.0f : 140.0f)); break;
					default:                          wantW = 60.0f; break;
				}
				maxWidgetW = Math::Max(maxWidgetW, wantW);
			}
		}

		f32 minWidgetW = hasInputWidgets ? maxWidgetW * s : 0.0f;

		f32 titleW = titleSize.x + padding * 4.0f;
		f32 outputW = pinArea + maxOutputLabelW + gap + pinArea;
		f32 inputW = pinArea + maxInputLabelW + (hasInputWidgets ? gap + minWidgetW + gap : gap) + pinArea;
		f32 nodeW = Math::Max(NodeMinWidth * s, Math::Max(titleW, Math::Max(outputW, inputW)));

		f32 nodeH = headerH + contentHeight;

		//reserve a square preview area below the pins; the trailing content padding doubles as the gap
		if (node.thumbnail)
		{
			f32 thumb = NodeThumbnailSize * s;
			nodeW = Math::Max(nodeW, thumb + padding * 2.0f);
			nodeH += thumb + padding;
		}

		node.size = {nodeW, nodeH};
	}

	// --- Drawing ---

	void GraphEditor::DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize) const
	{
		f32 gridStep = GridSize * m_scale * m_zoom;
		ImU32 gridColor = IM_COL32(255, 255, 255, 15);
		ImU32 gridColorBold = IM_COL32(255, 255, 255, 30);

		f32 offsetX = fmodf(m_scrollOffset.x, gridStep);
		f32 offsetY = fmodf(m_scrollOffset.y, gridStep);

		i32 gridCountX = (i32)(canvasSize.x / gridStep) + 2;
		i32 gridCountY = (i32)(canvasSize.y / gridStep) + 2;

		for (i32 i = 0; i < gridCountX; i++)
		{
			f32 x = canvasPos.x + offsetX + (f32)i * gridStep;
			i32 lineIndex = (i32)((x - canvasPos.x - m_scrollOffset.x) / gridStep);
			ImU32 col = (lineIndex % 4 == 0) ? gridColorBold : gridColor;
			drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), col);
		}
		for (i32 i = 0; i < gridCountY; i++)
		{
			f32 y = canvasPos.y + offsetY + (f32)i * gridStep;
			i32 lineIndex = (i32)((y - canvasPos.y - m_scrollOffset.y) / gridStep);
			ImU32 col = (lineIndex % 4 == 0) ? gridColorBold : gridColor;
			drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y), col);
		}
	}

	void GraphEditor::DrawNode(ImDrawList* drawList, FrameNode& node)
	{
		f32 s = m_scale;
		f32 headerH = NodeHeaderHeight * s;
		f32 rounding = NodeRounding * s;
		f32 pinR = PinRadius * s;

		ImVec2 nodeMin(node.position.x, node.position.y);
		ImVec2 nodeMax(node.position.x + node.size.x, node.position.y + node.size.y);
		ImVec2 headerMax(node.position.x + node.size.x, node.position.y + headerH);

		// Shadow
		drawList->AddRectFilled(
			ImVec2(nodeMin.x + 4 * s, nodeMin.y + 4 * s),
			ImVec2(nodeMax.x + 4 * s, nodeMax.y + 4 * s),
			IM_COL32(0, 0, 0, 60), rounding);

		// Body
		drawList->AddRectFilled(nodeMin, nodeMax, node.desc.color, rounding);

		// Header
		drawList->AddRectFilled(nodeMin, headerMax, IM_COL32(38, 38, 40, 230), rounding, ImDrawFlags_RoundCornersTop);

		// Separator
		drawList->AddLine(
			ImVec2(nodeMin.x, headerMax.y - 0.5f),
			ImVec2(headerMax.x, headerMax.y - 0.5f),
			IM_COL32(255, 255, 255, 20), 1.0f);

		// Border
		if (IsNodeSelected(node.id))
		{
			drawList->AddRect(nodeMin, nodeMax, IM_COL32(66, 150, 250, 255), rounding, 0, 2.0f);
		}
		else
		{
			drawList->AddRect(nodeMin, nodeMax, IM_COL32(3, 3, 5, 200), rounding, 0, 1.0f);
		}

		// Draw pin shapes
		for (u32 i = 0; i < node.inputCount; i++)
		{
			const FramePin& pin = m_framePins[node.inputStart + i];
			Vec2 pinPos = GetPinPos(node, GraphPinKind::Input, i);

			if (pin.type == GraphPinType::Flow)
			{
				f32 arrowW = pinR * 1.6f;
				f32 arrowH = pinR * 2.0f;
				ImVec2 p1(pinPos.x - arrowW * 0.5f, pinPos.y - arrowH * 0.5f);
				ImVec2 p2(pinPos.x - arrowW * 0.5f, pinPos.y + arrowH * 0.5f);
				ImVec2 p3(pinPos.x + arrowW * 0.5f, pinPos.y);
				drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(255, 255, 255, 255));
				drawList->AddTriangle(p1, p2, p3, IM_COL32(200, 200, 200, 200), 1.0f);
			}
			else
			{
				drawList->AddCircleFilled(ImVec2(pinPos.x, pinPos.y), pinR, pin.color);
				drawList->AddCircle(ImVec2(pinPos.x, pinPos.y), pinR, IM_COL32(200, 200, 200, 200), 0, 1.0f);
			}
		}

		for (u32 i = 0; i < node.outputCount; i++)
		{
			const FramePin& pin = m_framePins[node.outputStart + i];
			Vec2 pinPos = GetPinPos(node, GraphPinKind::Output, i);

			if (pin.type == GraphPinType::Flow)
			{
				f32 arrowW = pinR * 1.6f;
				f32 arrowH = pinR * 2.0f;
				ImVec2 p1(pinPos.x - arrowW * 0.5f, pinPos.y - arrowH * 0.5f);
				ImVec2 p2(pinPos.x - arrowW * 0.5f, pinPos.y + arrowH * 0.5f);
				ImVec2 p3(pinPos.x + arrowW * 0.5f, pinPos.y);
				drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(255, 255, 255, 255));
				drawList->AddTriangle(p1, p2, p3, IM_COL32(200, 200, 200, 200), 1.0f);
			}
			else
			{
				drawList->AddCircleFilled(ImVec2(pinPos.x, pinPos.y), pinR, pin.color);
				drawList->AddCircle(ImVec2(pinPos.x, pinPos.y), pinR, IM_COL32(200, 200, 200, 200), 0, 1.0f);
			}
		}

		// Preview thumbnail (e.g. a texture node's assigned image) centered along the node bottom
		if (node.thumbnail)
		{
			f32    padding = NodePadding * s;
			f32    thumb = NodeThumbnailSize * s;
			f32    imgX = node.position.x + (node.size.x - thumb) * 0.5f;
			f32    imgY = node.position.y + node.size.y - thumb - padding;
			ImVec2 imgMin(imgX, imgY);
			ImVec2 imgMax(imgX + thumb, imgY + thumb);

			drawList->AddRectFilled(imgMin, imgMax, IM_COL32(20, 20, 22, 255), 2.0f * s);
			drawList->AddImage(node.thumbnail, imgMin, imgMax);
			drawList->AddRect(imgMin, imgMax, IM_COL32(0, 0, 0, 200), 2.0f * s, 0, 1.0f);
		}
	}

	void GraphEditor::DrawNodeText(ImDrawList* drawList, const FrameNode& node) const
	{
		f32 screenFontSize = ImGui::GetFontSize() * m_zoom;
		f32 s = m_scale;

		ImVec2 screenNodeMin = m_canvas.FromLocal(ImVec2(node.position.x, node.position.y));
		ImVec2 screenNodeSize = m_canvas.FromLocalV(ImVec2(node.size.x, node.size.y));

		// Title centered
		const char* titleText = node.name.CStr() ? node.name.CStr() : "Untitled" ;
		ImVec2 titleSize = ImGui::GetFont()->CalcTextSizeA(screenFontSize, FLT_MAX, 0.0f, titleText);
		f32 titleX = screenNodeMin.x + (screenNodeSize.x - titleSize.x) * 0.5f;
		f32 titleY = screenNodeMin.y + (NodeHeaderHeight * s * m_zoom - titleSize.y) * 0.5f;
		drawList->AddText(ImGui::GetFont(), screenFontSize, ImVec2(titleX, titleY), IM_COL32(190, 190, 190, 255), titleText);

		// Input pin labels
		for (u32 i = 0; i < node.inputCount; i++)
		{
			const FramePin& pin = m_framePins[node.inputStart + i];
			Vec2 pinPos = GetPinPos(node, GraphPinKind::Input, i);
			ImVec2 screenPinPos = m_canvas.FromLocal(ImVec2(pinPos.x, pinPos.y));

			f32 pinExtent = (pin.type == GraphPinType::Flow) ? PinRadius * 1.6f * 0.5f : PinRadius;
			f32 screenPinExtent = pinExtent * s * m_zoom;
			ImVec2 textPos(screenPinPos.x + screenPinExtent + 6.0f * s * m_zoom, screenPinPos.y - screenFontSize * 0.5f);
			drawList->AddText(ImGui::GetFont(), screenFontSize, textPos, IM_COL32(200, 200, 200, 255), pin.name.CStr());
		}

		// Output pin labels
		for (u32 i = 0; i < node.outputCount; i++)
		{
			const FramePin& pin = m_framePins[node.outputStart + i];
			Vec2 pinPos = GetPinPos(node, GraphPinKind::Output, i);
			ImVec2 screenPinPos = m_canvas.FromLocal(ImVec2(pinPos.x, pinPos.y));

			const char* text = pin.name.CStr();
			ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(screenFontSize, FLT_MAX, 0.0f, text);
			f32 pinExtent = (pin.type == GraphPinType::Flow) ? PinRadius * 1.6f * 0.5f : PinRadius;
			f32 screenPinExtent = pinExtent * s * m_zoom;
			ImVec2 textPos(screenPinPos.x - screenPinExtent - 6.0f * s * m_zoom - textSize.x, screenPinPos.y - screenFontSize * 0.5f);
			drawList->AddText(ImGui::GetFont(), screenFontSize, textPos, IM_COL32(200, 200, 200, 255), text);
		}
	}

	void GraphEditor::DrawNodeWidgets(FrameNode& node)
	{
		bool hasWidgets = false;
		for (u32 i = 0; i < node.inputCount; i++)
		{
			if (m_framePins[node.inputStart + i].widgetType != GraphWidgetType::None) { hasWidgets = true; break; }
		}
		if (!hasWidgets) return;

		f32 s = m_scale;
		f32 fontSize = ImGui::GetFontSize();
		f32 pinR = PinRadius * s;
		f32 gap = 6.0f * s;

		// Max label width for alignment
		f32 maxLabelW = 0.0f;
		for (u32 i = 0; i < node.inputCount; i++)
		{
			const FramePin& pin = m_framePins[node.inputStart + i];
			if (pin.type == GraphPinType::Flow) continue;
			if (pin.name && pin.name[0] != '\0')
			{
				ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, pin.name.CStr());
				maxLabelW = Math::Max(maxLabelW, sz.x);
			}
		}

		f32 widgetColumnX = node.position.x + pinR + gap + maxLabelW + gap;
		f32 frameH = ImGui::GetFrameHeight();

		ImGui::PushID((int)node.id);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.65f, 0.85f, 0.65f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

		for (u32 i = 0; i < node.inputCount; i++)
		{
			FramePin& pin = m_framePins[node.inputStart + i];
			if (pin.widgetType == GraphWidgetType::None) continue;

			Vec2 pinPos = GetPinPos(node, GraphPinKind::Input, i);
			f32 widgetX = widgetColumnX;
			f32 widgetY = pinPos.y - frameH * 0.5f;
			f32 widgetW = node.position.x + node.size.x - pinR - gap - widgetX;

			if (widgetW < 30.0f) continue;

			ImGui::SetCursorScreenPos(ImVec2(widgetX, widgetY));
			ImGui::PushItemWidth(widgetW);
			ImGui::PushID(i);

			switch (pin.widgetType)
			{
				case GraphWidgetType::InputFloat:
					if (pin.floatPtr)
						ImGui::DragFloat("##f", pin.floatPtr, pin.dragSpeed, 0.0f, 0.0f, "%.2f");
					break;

				case GraphWidgetType::DragFloatN:
					if (pin.floatPtr)
						ImGui::DragScalarN("##fn", ImGuiDataType_Float, pin.floatPtr, (i32)pin.floatCount, pin.dragSpeed, nullptr, nullptr, "%.2f");
					break;

				case GraphWidgetType::Color:
				{
					if (!pin.floatPtr) break;

					m_canvas.Suspend();
					ImGui::SetFontRasterizerDensity(m_zoom);
					ImGui::SetWindowFontScale(m_zoom);

					ImVec2 screenPos = m_canvas.FromLocal(ImVec2(widgetX, widgetY));
					f32 screenW = widgetW * m_zoom;

					ImGui::SetCursorScreenPos(screenPos);
					ImGui::PushItemWidth(screenW);
					ImGui::ColorEdit3("##col", pin.floatPtr, ImGuiColorEditFlags_NoInputs);
					ImGui::PopItemWidth();

					ImGui::SetWindowFontScale(1.0f);
					ImGui::SetFontRasterizerDensity(m_zoom);
					m_canvas.Resume();
					break;
				}

				case GraphWidgetType::InputText:
					if (pin.textBuffer)
						ImGuiInputText(44444, pin.textBuffer);
					break;

				case GraphWidgetType::Checkbox:
					if (pin.boolPtr)
						ImGui::Checkbox("##c", pin.boolPtr);
					break;

				case GraphWidgetType::Combo:
				{
					if (!pin.comboIndexPtr || !pin.comboOptions) break;

					const char* preview = "...";
					Array<const char*> items;
					String optionsCopy = pin.comboOptions;
					char* str = optionsCopy.begin();
					char* start = str;
					while (*str)
					{
						if (*str == ';')
						{
							*str = '\0';
							items.EmplaceBack(start);
							start = str + 1;
						}
						str++;
					}
					if (start != str) items.EmplaceBack(start);

					if (*pin.comboIndexPtr >= 0 && *pin.comboIndexPtr < (i32)items.Size())
						preview = items[*pin.comboIndexPtr];

					m_canvas.Suspend();
					ImGui::SetFontRasterizerDensity(m_zoom);
					ImGui::SetWindowFontScale(m_zoom);

					ImVec2 screenPos = m_canvas.FromLocal(ImVec2(widgetX, widgetY));
					f32 screenW = widgetW * m_zoom;

					ImGui::SetCursorScreenPos(screenPos);
					ImGui::PushItemWidth(screenW);

					if (ImGui::BeginCombo("##cb", preview))
					{
						ImGui::SetWindowFontScale(m_zoom);
						for (i32 j = 0; j < (i32)items.Size(); j++)
						{
							bool selected = (*pin.comboIndexPtr == j);
							if (ImGui::Selectable(items[j], selected))
								*pin.comboIndexPtr = j;
						}
						ImGui::EndCombo();
					}

					ImGui::PopItemWidth();
					ImGui::SetWindowFontScale(1.0f);
					ImGui::SetFontRasterizerDensity(1.0f);
					m_canvas.Resume();
					break;
				}

				default:
					break;
			}

			if (ImGui::IsItemActive()) m_nodeWidgetActive = true;
			if (ImGui::IsItemHovered()) m_nodeWidgetHovered = true;

			if (pin.widgetType == GraphWidgetType::DragFloatN || pin.widgetType == GraphWidgetType::Color)
			{
				if (ImGui::IsItemActive())
				{
					m_hasActiveValuePin = true;
					m_activeValuePin = GraphEditorPinEdit{node.id, i};
				}
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					m_committedPinValues.EmplaceBack(GraphEditorPinEdit{node.id, i});
				}
			}

			ImGui::PopID();
			ImGui::PopItemWidth();
		}

		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(5);
		ImGui::PopID();
	}

	void GraphEditor::DrawLink(ImDrawList* drawList, const FrameLink& link, bool selected) const
	{
		const FrameNode* outputNode = FindNodeById(link.outputNodeId);
		const FrameNode* inputNode = FindNodeById(link.inputNodeId);
		if (!outputNode || !inputNode) return;

		Vec2 startPos = GetPinPos(*outputNode, GraphPinKind::Output, link.outputPinIndex);
		Vec2 endPos = GetPinPos(*inputNode, GraphPinKind::Input, link.inputPinIndex);

		f32 dist = Math::Abs(endPos.x - startPos.x) * 0.5f;
		f32 tangent = Math::Max(dist, 50.0f * m_scale);

		ImVec2 p1(startPos.x, startPos.y);
		ImVec2 p2(startPos.x + tangent, startPos.y);
		ImVec2 p3(endPos.x - tangent, endPos.y);
		ImVec2 p4(endPos.x, endPos.y);

		ImU32 color = selected ? IM_COL32(255, 165, 0, 255) : (ImU32)link.color;
		f32 thickness = (selected ? LinkThickness + 1.5f : LinkThickness) * m_scale;
		drawList->AddBezierCubic(p1, p2, p3, p4, color, thickness);
	}

	f32 GraphEditor::DistanceToLink(const FrameLink& link, const Vec2& point) const
	{
		const FrameNode* outputNode = FindNodeById(link.outputNodeId);
		const FrameNode* inputNode = FindNodeById(link.inputNodeId);
		if (!outputNode || !inputNode) return FLT_MAX;

		bool isStateMachine = outputNode->desc.nodeType == GraphNodeType::StateMachine &&
			inputNode->desc.nodeType == GraphNodeType::StateMachine;

		if (isStateMachine)
		{
			f32 s = m_scale;
			Vec2 outputCenter = outputNode->position + outputNode->size * 0.5f;
			Vec2 inputCenter = inputNode->position + inputNode->size * 0.5f;

			u64 pairIdLow = Math::Min(outputNode->id, inputNode->id);
			u64 pairIdHigh = Math::Max(outputNode->id, inputNode->id);
			const FrameNode* lowNode = (outputNode->id == pairIdLow) ? outputNode : inputNode;
			const FrameNode* highNode = (outputNode->id == pairIdLow) ? inputNode : outputNode;
			Vec2 lowCenter = lowNode->position + lowNode->size * 0.5f;
			Vec2 highCenter = highNode->position + highNode->size * 0.5f;

			Vec2 dir = highCenter - lowCenter;
			f32 len = Math::Sqrt(dir.x * dir.x + dir.y * dir.y);
			Vec2 perp = (len > 0.001f) ? Vec2{-dir.y / len, dir.x / len} : Vec2{0.0f, 1.0f};

			i32 totalLinks = CountLinksBetween(pairIdLow, pairIdHigh);
			i32 linkIndex = GetLinkIndexBetween(link, pairIdLow, pairIdHigh);
			f32 offset = 0.0f;
			if (totalLinks > 1)
			{
				f32 step = MultiLinkOffset * s;
				offset = ((f32)linkIndex - (f32)(totalLinks - 1) * 0.5f) * step;
			}
			Vec2 offsetVec = perp * offset;

			Vec2 startPos = GetRectEdgePoint(*outputNode, inputCenter) + offsetVec;
			Vec2 endPos = GetRectEdgePoint(*inputNode, outputCenter) + offsetVec;

			Vec2 ab = endPos - startPos;
			Vec2 ap = point - startPos;
			f32 dotAB = Vec2::Dot(ab, ab);
			f32 tProj = dotAB > 0.0f ? Math::Clamp(Vec2::Dot(ap, ab) / dotAB, 0.0f, 1.0f) : 0.0f;
			Vec2 closest = startPos + ab * tProj;
			return Vec2::Distance(point, closest);
		}

		// Blueprint: bezier distance
		Vec2 startPos = GetPinPos(*outputNode, GraphPinKind::Output, link.outputPinIndex);
		Vec2 endPos = GetPinPos(*inputNode, GraphPinKind::Input, link.inputPinIndex);

		f32 dist = Math::Abs(endPos.x - startPos.x) * 0.5f;
		f32 tangent = Math::Max(dist, 50.0f * m_scale);

		ImVec2 p1(startPos.x, startPos.y);
		ImVec2 p2(startPos.x + tangent, startPos.y);
		ImVec2 p3(endPos.x - tangent, endPos.y);
		ImVec2 p4(endPos.x, endPos.y);

		f32 minDist = FLT_MAX;
		ImVec2 prev = p1;
		constexpr i32 segments = 20;
		for (i32 i = 1; i <= segments; i++)
		{
			f32 t = (f32)i / (f32)segments;
			f32 u = 1.0f - t;
			ImVec2 pt;
			pt.x = u * u * u * p1.x + 3 * u * u * t * p2.x + 3 * u * t * t * p3.x + t * t * t * p4.x;
			pt.y = u * u * u * p1.y + 3 * u * u * t * p2.y + 3 * u * t * t * p3.y + t * t * t * p4.y;

			Vec2 a = {prev.x, prev.y};
			Vec2 b = {pt.x, pt.y};
			Vec2 ab = b - a;
			Vec2 ap = point - a;
			f32 dotAB = Vec2::Dot(ab, ab);
			f32 tProj = dotAB > 0.0f ? Math::Clamp(Vec2::Dot(ap, ab) / dotAB, 0.0f, 1.0f) : 0.0f;
			Vec2 closest = a + ab * tProj;
			f32 d = Vec2::Distance(point, closest);
			if (d < minDist) minDist = d;

			prev = pt;
		}
		return minDist;
	}

	// --- Interaction ---

	void GraphEditor::HandleInteraction(GraphEditorResult& result)
	{
		ImGuiIO& io = ImGui::GetIO();
		ImVec2 mousePos = ImGui::GetMousePos();
		bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);

		// Pin interaction
		u64 hoveredNodeId{};
		GraphPinKind hoveredKind{};
		u32 hoveredPinIndex{};
		bool hoveredPin = FindHoveredPin(hoveredNodeId, hoveredKind, hoveredPinIndex);

		// Start link creation from pin
		if (hoveredPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_creatingLink)
		{
			m_creatingLink = true;
			m_linkStartNodeId = hoveredNodeId;
			m_linkStartPinIndex = hoveredPinIndex;
			m_linkStartKind = hoveredKind;
		}

		// Complete link creation
		if (m_creatingLink && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bool completed = false;

			if (hoveredPin && hoveredNodeId != m_linkStartNodeId && hoveredKind != m_linkStartKind)
			{
				// Blueprint-style pin-to-pin connection
				GraphEditorNewLink newLink{};
				if (m_linkStartKind == GraphPinKind::Output)
				{
					newLink.outputNodeId = m_linkStartNodeId;
					newLink.outputPinIndex = m_linkStartPinIndex;
					newLink.inputNodeId = hoveredNodeId;
					newLink.inputPinIndex = hoveredPinIndex;
				}
				else
				{
					newLink.outputNodeId = hoveredNodeId;
					newLink.outputPinIndex = hoveredPinIndex;
					newLink.inputNodeId = m_linkStartNodeId;
					newLink.inputPinIndex = m_linkStartPinIndex;
				}
				result.createdLinks.EmplaceBack(newLink);
				completed = true;
			}
			else if (!hoveredPin)
			{
				// State machine: click on a node to complete
				i32 nodeIdx = FindHoveredNode();
				if (nodeIdx >= 0)
				{
					u64 targetId = m_frameNodes[nodeIdx].id;
					if (targetId != m_linkStartNodeId)
					{
						GraphEditorNewLink newLink{};
						newLink.outputNodeId = m_linkStartNodeId;
						newLink.outputPinIndex = 0;
						newLink.inputNodeId = targetId;
						newLink.inputPinIndex = 0;
						result.createdLinks.EmplaceBack(newLink);
						completed = true;
					}
				}
			}

			if (completed)
			{
				m_creatingLink = false;
			}
		}

		// Cancel link creation on right-click or ESC
		if (m_creatingLink && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)))
		{
			m_creatingLink = false;
		}

		// Box selection update
		if (m_boxSelecting)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				m_boxSelectEnd = {mousePos.x, mousePos.y};
			}
			else
			{
				Vec2 bMin = {Math::Min(m_boxSelectStart.x, m_boxSelectEnd.x), Math::Min(m_boxSelectStart.y, m_boxSelectEnd.y)};
				Vec2 bMax = {Math::Max(m_boxSelectStart.x, m_boxSelectEnd.x), Math::Max(m_boxSelectStart.y, m_boxSelectEnd.y)};

				if (!io.KeyShift)
				{
					ClearSelection();
				}

				for (const auto& node : m_frameNodes)
				{
					Vec2 nodeEnd = node.position + node.size;

					if (node.position.x <= bMax.x && nodeEnd.x >= bMin.x &&
						node.position.y <= bMax.y && nodeEnd.y >= bMin.y)
					{
						if (!IsNodeSelected(node.id))
						{
							m_selectedNodes.EmplaceBack(node.id);
						}
					}
				}

				for (const auto& link : m_frameLinks)
				{
					const FrameNode* outNode = FindNodeById(link.outputNodeId);
					const FrameNode* inNode = FindNodeById(link.inputNodeId);
					if (!outNode || !inNode) continue;

					Vec2 startPos, endPos;
					bool isStateMachine = outNode->desc.nodeType == GraphNodeType::StateMachine &&
						inNode->desc.nodeType == GraphNodeType::StateMachine;

					if (isStateMachine)
					{
						Vec2 outCenter = outNode->position + outNode->size * 0.5f;
						Vec2 inCenter = inNode->position + inNode->size * 0.5f;
						startPos = GetRectEdgePoint(*outNode, inCenter);
						endPos = GetRectEdgePoint(*inNode, outCenter);
					}
					else
					{
						startPos = GetPinPos(*outNode, GraphPinKind::Output, link.outputPinIndex);
						endPos = GetPinPos(*inNode, GraphPinKind::Input, link.inputPinIndex);
					}

					if (startPos.x >= bMin.x && startPos.x <= bMax.x && startPos.y >= bMin.y && startPos.y <= bMax.y &&
						endPos.x >= bMin.x && endPos.x <= bMax.x && endPos.y >= bMin.y && endPos.y <= bMax.y)
					{
						if (!IsLinkSelected(link.id))
						{
							m_selectedLinks.EmplaceBack(link.id);
						}
					}
				}

				m_boxSelecting = false;
			}
		}

		// Node dragging and selection
		if (!hoveredPin && !m_creatingLink && !m_boxSelecting && canvasHovered)
		{
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_nodeWidgetActive)
			{
				i32 nodeIdx = FindHoveredNode();

				if (nodeIdx >= 0)
				{
					u64 clickedId = m_frameNodes[nodeIdx].id;

					if (io.KeyShift)
					{
						if (IsNodeSelected(clickedId))
						{
							for (u32 i = 0; i < m_selectedNodes.Size(); i++)
							{
								if (m_selectedNodes[i] == clickedId)
								{
									m_selectedNodes.RemoveAt(i);
									break;
								}
							}
						}
						else
						{
							m_selectedNodes.EmplaceBack(clickedId);
						}
					}
					else
					{
						if (!IsNodeSelected(clickedId))
						{
							ClearSelection();
							m_selectedNodes.EmplaceBack(clickedId);
						}
					}

					// Start dragging all selected nodes
					m_draggedNode = nodeIdx;
					m_dragOffsets.Clear();
					m_dragStartPositions.Clear();
					for (u64 selId : m_selectedNodes)
					{
						const FrameNode* n = FindNodeById(selId);
						if (n)
						{
							m_dragOffsets[selId] = {mousePos.x - n->position.x, mousePos.y - n->position.y};
							m_dragStartPositions[selId] = n->position;
						}
					}
				}
				else
				{
					// Check if clicking a link
					i32 bestLink = FindHoveredLink();

					if (bestLink >= 0)
					{
						u64 linkId = m_frameLinks[bestLink].id;
						if (io.KeyShift)
						{
							if (IsLinkSelected(linkId))
							{
								for (u32 i = 0; i < m_selectedLinks.Size(); i++)
								{
									if (m_selectedLinks[i] == linkId)
									{
										m_selectedLinks.RemoveAt(i);
										break;
									}
								}
							}
							else
							{
								m_selectedLinks.EmplaceBack(linkId);
							}
						}
						else
						{
							ClearSelection();
							m_selectedLinks.EmplaceBack(linkId);
						}
					}
					else
					{
						// Empty space - box selection
						if (!io.KeyShift)
						{
							ClearSelection();
						}
						m_boxSelecting = true;
						m_boxSelectStart = {mousePos.x, mousePos.y};
						m_boxSelectEnd = m_boxSelectStart;
					}
				}
			}

			// Right-click selects node/link if not already selected
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				i32 nodeIdx = FindHoveredNode();
				if (nodeIdx >= 0)
				{
					u64 clickedId = m_frameNodes[nodeIdx].id;
					if (!IsNodeSelected(clickedId))
					{
						ClearSelection();
						m_selectedNodes.EmplaceBack(clickedId);
					}
				}
				else
				{
					i32 bestLink = FindHoveredLink();
					if (bestLink >= 0)
					{
						u64 linkId = m_frameLinks[bestLink].id;
						if (!IsLinkSelected(linkId))
						{
							ClearSelection();
							m_selectedLinks.EmplaceBack(linkId);
						}
					}
					else
					{
						ClearSelection();
					}
				}
			}
		}

		// Drag selected nodes
		if (m_draggedNode >= 0)
		{
			if (m_nodeWidgetActive)
			{
				m_draggedNode = -1;
				m_dragOffsets.Clear();
				m_dragStartPositions.Clear();
			}
			else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				for (auto& node : m_frameNodes)
				{
					if (IsNodeSelected(node.id))
					{
						auto it = m_dragOffsets.Find(node.id);
						if (it != m_dragOffsets.end())
						{
							node.position = {mousePos.x - it->second.x, mousePos.y - it->second.y};
						}
					}
				}
			}
			else
			{
				// Drag ended - compute final positions from offsets and report
				for (auto& node : m_frameNodes)
				{
					if (IsNodeSelected(node.id))
					{
						auto startIt = m_dragStartPositions.Find(node.id);
						auto offsetIt = m_dragOffsets.Find(node.id);
						if (startIt != m_dragStartPositions.end() && offsetIt != m_dragOffsets.end())
						{
							Vec2 startPos = startIt->second;
							Vec2 finalPos = {mousePos.x - offsetIt->second.x, mousePos.y - offsetIt->second.y};
							node.position = finalPos;
							if (finalPos.x != startPos.x || finalPos.y != startPos.y)
							{
								result.movedNodes.EmplaceBack(GraphEditorNodeMoved{node.id, startPos, finalPos});
							}
						}
					}
				}
				m_draggedNode = -1;
				m_dragOffsets.Clear();
				m_dragStartPositions.Clear();
			}
		}

		// Delete selected
		if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_nodeWidgetActive)
		{
			for (u64 nodeId : m_selectedNodes)
			{
				result.deletedNodeIds.EmplaceBack(nodeId);
			}
			for (u64 linkId : m_selectedLinks)
			{
				result.deletedLinkIds.EmplaceBack(linkId);
			}

			if (!m_selectedNodes.Empty() || !m_selectedLinks.Empty())
			{
				ClearSelection();
			}
			m_draggedNode = -1;
		}
	}

	// --- State Machine ---

	void GraphEditor::DrawStateMachineNode(ImDrawList* drawList, FrameNode& node)
	{
		f32 s = m_scale;
		f32 rounding = (node.desc.rounding >= 0.0f ? node.desc.rounding : StateMachineRounding) * s;

		ImVec2 nodeMin(node.position.x, node.position.y);
		ImVec2 nodeMax(node.position.x + node.size.x, node.position.y + node.size.y);

		// Shadow
		drawList->AddRectFilled(
			ImVec2(nodeMin.x + 4 * s, nodeMin.y + 4 * s),
			ImVec2(nodeMax.x + 4 * s, nodeMax.y + 4 * s),
			IM_COL32(0, 0, 0, 60), rounding);

		// Body
		drawList->AddRectFilled(nodeMin, nodeMax, node.desc.color, rounding);

		// Border
		if (IsNodeSelected(node.id))
		{
			drawList->AddRect(nodeMin, nodeMax, IM_COL32(66, 150, 250, 255), rounding, 0, 2.0f);
		}
		else
		{
			drawList->AddRect(nodeMin, nodeMax, IM_COL32(180, 180, 180, 100), rounding, 0, 1.0f);
		}
	}

	void GraphEditor::DrawStateMachineNodeText(ImDrawList* drawList, const FrameNode& node) const
	{
		f32 screenFontSize = ImGui::GetFontSize() * m_zoom;

		ImVec2 screenNodeMin = m_canvas.FromLocal(ImVec2(node.position.x, node.position.y));
		ImVec2 screenNodeSize = m_canvas.FromLocalV(ImVec2(node.size.x, node.size.y));

		const char* text = node.name.CStr();
		ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(screenFontSize, FLT_MAX, 0.0f, text);
		f32 textX = screenNodeMin.x + (screenNodeSize.x - textSize.x) * 0.5f;
		f32 textY = screenNodeMin.y + (screenNodeSize.y - textSize.y) * 0.5f;
		drawList->AddText(ImGui::GetFont(), screenFontSize, ImVec2(textX, textY), IM_COL32(220, 220, 220, 255), text);
	}

	void GraphEditor::DrawStateMachineLink(ImDrawList* drawList, const FrameLink& link, bool selected) const
	{
		const FrameNode* outputNode = FindNodeById(link.outputNodeId);
		const FrameNode* inputNode = FindNodeById(link.inputNodeId);
		if (!outputNode || !inputNode) return;

		f32 s = m_scale;
		Vec2 outputCenter = outputNode->position + outputNode->size * 0.5f;
		Vec2 inputCenter = inputNode->position + inputNode->size * 0.5f;

		u64 pairIdLow = Math::Min(outputNode->id, inputNode->id);
		u64 pairIdHigh = Math::Max(outputNode->id, inputNode->id);
		i32 totalLinks = CountLinksBetween(pairIdLow, pairIdHigh);
		i32 linkIndex = GetLinkIndexBetween(link, pairIdLow, pairIdHigh);

		const FrameNode* lowNode = (outputNode->id == pairIdLow) ? outputNode : inputNode;
		const FrameNode* highNode = (outputNode->id == pairIdLow) ? inputNode : outputNode;
		Vec2 lowCenter = lowNode->position + lowNode->size * 0.5f;
		Vec2 highCenter = highNode->position + highNode->size * 0.5f;

		Vec2 dir = highCenter - lowCenter;
		f32 len = Math::Sqrt(dir.x * dir.x + dir.y * dir.y);
		Vec2 perp = (len > 0.001f) ? Vec2{-dir.y / len, dir.x / len} : Vec2{0.0f, 1.0f};

		f32 offset = 0.0f;
		if (totalLinks > 1)
		{
			f32 step = MultiLinkOffset * s;
			offset = ((f32)linkIndex - (f32)(totalLinks - 1) * 0.5f) * step;
		}

		Vec2 offsetVec = perp * offset;

		Vec2 startPos = GetRectEdgePoint(*outputNode, inputCenter) + offsetVec;
		Vec2 endPos = GetRectEdgePoint(*inputNode, outputCenter) + offsetVec;

		ImU32 color = selected ? IM_COL32(255, 165, 0, 255) : (ImU32)link.color;
		f32 thickness = (selected ? LinkThickness + 1.5f : LinkThickness) * s;

		drawList->AddLine(ImVec2(startPos.x, startPos.y), ImVec2(endPos.x, endPos.y), color, thickness);

		// Arrow at midpoint
		Vec2 lineDir = endPos - startPos;
		f32 lineLen = Math::Sqrt(lineDir.x * lineDir.x + lineDir.y * lineDir.y);
		if (lineLen > 0.001f)
		{
			Vec2 normDir = lineDir * (1.0f / lineLen);
			Vec2 normPerp = {-normDir.y, normDir.x};
			Vec2 mid = (startPos + endPos) * 0.5f;

			f32 arrowS = ArrowSize * s;
			Vec2 tip = mid + normDir * arrowS;
			Vec2 base1 = mid - normDir * arrowS * 0.5f + normPerp * arrowS * 0.5f;
			Vec2 base2 = mid - normDir * arrowS * 0.5f - normPerp * arrowS * 0.5f;

			drawList->AddTriangleFilled(
				ImVec2(tip.x, tip.y),
				ImVec2(base1.x, base1.y),
				ImVec2(base2.x, base2.y),
				color);
		}
	}

	Vec2 GraphEditor::GetRectEdgePoint(const FrameNode& node, const Vec2& target) const
	{
		Vec2 center = node.position + node.size * 0.5f;
		Vec2 dir = target - center;

		if (dir.x == 0.0f && dir.y == 0.0f)
		{
			return center;
		}

		f32 halfW = node.size.x * 0.5f;
		f32 halfH = node.size.y * 0.5f;

		f32 scaleX = (dir.x != 0.0f) ? halfW / Math::Abs(dir.x) : FLT_MAX;
		f32 scaleY = (dir.y != 0.0f) ? halfH / Math::Abs(dir.y) : FLT_MAX;
		f32 scale = Math::Min(scaleX, scaleY);

		return center + dir * scale;
	}

	i32 GraphEditor::CountLinksBetween(u64 nodeA, u64 nodeB) const
	{
		i32 count = 0;
		for (const auto& link : m_frameLinks)
		{
			if ((link.outputNodeId == nodeA && link.inputNodeId == nodeB) ||
				(link.outputNodeId == nodeB && link.inputNodeId == nodeA))
			{
				count++;
			}
		}
		return count;
	}

	i32 GraphEditor::GetLinkIndexBetween(const FrameLink& link, u64 nodeA, u64 nodeB) const
	{
		i32 index = 0;
		for (const auto& l : m_frameLinks)
		{
			if ((l.outputNodeId == nodeA && l.inputNodeId == nodeB) ||
				(l.outputNodeId == nodeB && l.inputNodeId == nodeA))
			{
				if (l.id == link.id) return index;
				index++;
			}
		}
		return 0;
	}
}
