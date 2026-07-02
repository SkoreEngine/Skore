namespace Skore
{
	void RegisterRenderPipeline();
	void RegisterForwardPass();
	void RegisterLightSetupPass();
	void RegisterPostProcessPass();
	void RegisterCullingPass();
	void RegisterCascadeShadowPass();
	void RegisterBloomPass();
	void RegisterRmlUiRenderModule();

	void RegisterPipelineTypes()
	{
		RegisterRenderPipeline();
		RegisterForwardPass();
		RegisterLightSetupPass();
		RegisterPostProcessPass();
		RegisterCullingPass();
		RegisterCascadeShadowPass();
		RegisterBloomPass();
		RegisterRmlUiRenderModule();
	}
}
