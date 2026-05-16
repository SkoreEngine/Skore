namespace Skore
{
	void RegisterDefaultRenderPipeline();
	void RegisterSwapchainRenderModule();
	void RegisterCascadeShadowMapModule();
	void RegisterDepthPrePassModule();
	void RegisterPathTracerPipeline();
	void RegisterXeGTAORenderModule();
	void RegisterTAARenderModule();
	void RegisterFXAARenderModule();
	void RegisterBloomModule();

	void RegisterPipelineTypes()
	{
		RegisterCascadeShadowMapModule();
		RegisterDepthPrePassModule();
		RegisterDefaultRenderPipeline();
		RegisterSwapchainRenderModule();
		RegisterPathTracerPipeline();
		RegisterXeGTAORenderModule();
		RegisterTAARenderModule();
		RegisterFXAARenderModule();
		RegisterBloomModule();
	}
}
