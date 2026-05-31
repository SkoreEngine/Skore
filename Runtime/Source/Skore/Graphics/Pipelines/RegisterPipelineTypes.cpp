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
	void RegisterIndirectLightingModule();

	void RegisterCascadeShadowPass();
	void RegisterLightSetupPass();
	void RegisterCullingPass();
	void RegisterDeferredGBufferPass();
	void RegisterDeferredLightingPass();
	void RegisterForwardPass();
	void RegisterDepthLinearizePass();
	void RegisterCompositePass();
	void RegisterCameraMotionVectorPass();
	void RegisterPostProcessPass();

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
		RegisterIndirectLightingModule();

		RegisterCascadeShadowPass();
		RegisterLightSetupPass();
		RegisterCullingPass();
		RegisterDeferredGBufferPass();
		RegisterDeferredLightingPass();
		RegisterForwardPass();
		RegisterDepthLinearizePass();
		RegisterCompositePass();
		RegisterCameraMotionVectorPass();
		RegisterPostProcessPass();
	}
}
