#include "RenderPipeline.hpp"

#include "RenderGraph.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	namespace
	{
		RenderPipelineContext* mainContext = nullptr;
	}

	DefaultRenderPipeline::~DefaultRenderPipeline()
	{
		for (DefaultPipelinePass* pass : passes)
		{
			DestroyAndFree(pass);
		}
		passes.Clear();
	}

	void DefaultRenderPipeline::BuildRenderGraph(RenderGraph& renderGraph)
	{
		if (!discovered)
		{
			for (TypeID typeId : Reflection::GetDerivedTypes(TypeInfo<DefaultPipelinePass>::ID()))
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

				if (DefaultPipelinePass* pass = object->SafeCast<DefaultPipelinePass>())
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

		for (DefaultPipelinePass* pass : passes)
		{
			pass->BuildRenderGraph(renderGraph);
		}
	}

	RenderPipelineContext::RenderPipelineContext(TypeID pipelineTypeId)
	{
		renderGraph = Alloc<RenderGraph>();

		if (ReflectType* reflectType = Reflection::FindTypeById(pipelineTypeId))
		{
			if (Object* object = reflectType->NewObject())
			{
				pipeline = object->SafeCast<RenderPipeline>();
				if (pipeline == nullptr)
				{
					DestroyAndFree(object);
				}
			}
		}
	}

	RenderPipelineContext::~RenderPipelineContext()
	{
		if (mainContext == this)
		{
			mainContext = nullptr;
		}

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

	RenderGraph& RenderPipelineContext::GetRenderGraph() const
	{
		return *renderGraph;
	}

	RenderPipeline* RenderPipelineContext::GetPipeline() const
	{
		return pipeline;
	}

	RenderPipelineContext* RenderPipelineContext::GetMainContext()
	{
		return mainContext;
	}

	void RenderPipelineContext::SetMainContext(RenderPipelineContext* context)
	{
		mainContext = context;
	}

	void RenderPipelineContext::Execute(GPUCommandBuffer* cmd, Scene* scene)
	{
		if (pipeline == nullptr)
		{
			return;
		}

		renderGraph->Begin(scene);
		pipeline->BuildRenderGraph(*renderGraph);
		renderGraph->Execute(cmd);
	}

	RenderPipelineContext* RenderPipelineContext::Create(TypeID pipelineTypeId)
	{
		return Alloc<RenderPipelineContext>(pipelineTypeId);
	}

	void RegisterRenderPipeline()
	{
		Reflection::Type<RenderPipeline>();
		Reflection::Type<DefaultPipelinePass>();
		Reflection::Type<DefaultRenderPipeline>();
		Reflection::Type<RenderPipelineContext>();
	}
}
