#include "RenderPipelineNew.hpp"

#include "RenderGraph.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	DefaultRenderPipelineNew::~DefaultRenderPipelineNew()
	{
		for (DefaultPipelinePassNew* pass : passes)
		{
			DestroyAndFree(pass);
		}
		passes.Clear();
	}

	void DefaultRenderPipelineNew::BuildRenderGraph(RenderGraph& renderGraph)
	{
		if (!discovered)
		{
			for (TypeID typeId : Reflection::GetDerivedTypes(TypeInfo<DefaultPipelinePassNew>::ID()))
			{
				ReflectType* reflectType = Reflection::FindTypeById(typeId);
				if (reflectType == nullptr)
				{
					continue;
				}

				Object* object = reflectType->NewObject();
				if (object == nullptr)
				{
					continue;
				}

				if (DefaultPipelinePassNew* pass = object->SafeCast<DefaultPipelinePassNew>())
				{
					passes.EmplaceBack(pass);
				}
				else
				{
					DestroyAndFree(object);
				}
			}
			discovered = true;
		}

		for (DefaultPipelinePassNew* pass : passes)
		{
			pass->BuildRenderGraph(renderGraph);
		}
	}

	RenderPipelineContextNew::RenderPipelineContextNew(TypeID pipelineTypeId)
	{
		renderGraph = Alloc<RenderGraph>();

		if (ReflectType* reflectType = Reflection::FindTypeById(pipelineTypeId))
		{
			if (Object* object = reflectType->NewObject())
			{
				pipeline = object->SafeCast<RenderPipelineNew>();
				if (pipeline == nullptr)
				{
					DestroyAndFree(object);
				}
			}
		}
	}

	RenderPipelineContextNew::~RenderPipelineContextNew()
	{
		if (pipeline)
		{
			DestroyAndFree(pipeline);
			pipeline = nullptr;
		}

		if (renderGraph)
		{
			DestroyAndFree(renderGraph);
			renderGraph = nullptr;
		}
	}

	RenderGraph& RenderPipelineContextNew::GetRenderGraph() const
	{
		return *renderGraph;
	}

	void RenderPipelineContextNew::Execute(GPUCommandBuffer* cmd, Scene* scene)
	{
		if (pipeline == nullptr)
		{
			return;
		}

		renderGraph->Begin(scene);
		pipeline->BuildRenderGraph(*renderGraph);
		renderGraph->Execute(cmd);
	}

	RenderPipelineContextNew* RenderPipelineContextNew::Create(TypeID pipelineTypeId)
	{
		return Alloc<RenderPipelineContextNew>(pipelineTypeId);
	}

	void RegisterRenderPipelineNew()
	{
		Reflection::Type<RenderPipelineNew>();
		Reflection::Type<DefaultPipelinePassNew>();
		Reflection::Type<DefaultRenderPipelineNew>();
		Reflection::Type<RenderPipelineContextNew>();
	}
}
