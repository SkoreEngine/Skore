namespace Skore
{
	void RegisterRenderPipelineNew();
	void RegisterForwardOpaquePassNew();
	void RegisterForwardTransparentPassNew();

	void RegisterDefaultRenderPipeline();
	void RegisterSwapchainRenderModule();
	void RegisterCascadeShadowMapModule();
	void RegisterDepthPrePassModule();
	void RegisterXeGTAORenderModule();
	void RegisterTAARenderModule();
	void RegisterFXAARenderModule();
	void RegisterBloomModule();
	void RegisterProfilerOverlayModule();
	void RegisterIndirectLightingModule();
	void RegisterMotionVectorModule();
	void RegisterRmlUiRenderModule();

	void RegisterCascadeShadowPass();
	void RegisterLightSetupPass();
	void RegisterCullingPass();
	void RegisterDeferredGBufferPass();
	void RegisterDeferredLightingPass();
	void RegisterForwardPass();
	void RegisterDepthLinearizePass();
	void RegisterCompositePass();
	void RegisterPostProcessPass();

	void RegisterPipelineTypes()
	{
		RegisterRenderPipelineNew();
		RegisterForwardOpaquePassNew();
		RegisterForwardTransparentPassNew();

		RegisterCascadeShadowMapModule();
		RegisterDepthPrePassModule();
		RegisterDefaultRenderPipeline();
		RegisterSwapchainRenderModule();
		RegisterXeGTAORenderModule();
		RegisterTAARenderModule();
		RegisterFXAARenderModule();
		RegisterBloomModule();
		RegisterProfilerOverlayModule();
		RegisterIndirectLightingModule();
		RegisterMotionVectorModule();
		RegisterRmlUiRenderModule();

		RegisterCascadeShadowPass();
		RegisterLightSetupPass();
		RegisterCullingPass();
		RegisterDeferredGBufferPass();
		RegisterDeferredLightingPass();
		RegisterForwardPass();
		RegisterDepthLinearizePass();
		RegisterCompositePass();
		RegisterPostProcessPass();
	}
}
