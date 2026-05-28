namespace Skore
{
	void RegisterDefaultRenderPipeline();
	void RegisterSwapchainRenderModule();
	void RegisterCascadeShadowMapModule();
	void RegisterDepthPrePassModule();
	void RegisterXeGTAORenderModule();
	void RegisterTAARenderModule();
	void RegisterFXAARenderModule();
	void RegisterBloomModule();
	void RegisterProfilerOverlayModule();

	void RegisterPipelineTypes()
	{
		RegisterCascadeShadowMapModule();
		RegisterDepthPrePassModule();
		RegisterDefaultRenderPipeline();
		RegisterSwapchainRenderModule();
		RegisterXeGTAORenderModule();
		RegisterTAARenderModule();
		RegisterFXAARenderModule();
		RegisterBloomModule();
		RegisterProfilerOverlayModule();
	}
}
