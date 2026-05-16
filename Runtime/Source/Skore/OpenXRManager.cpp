#include "Skore/OpenXRManager.hpp"

#include "Skore/Core/Settings.hpp"
#include "Skore/Resource/Resources.hpp"

#if defined(SK_WIN)
#include <Windows.h>
#endif


#ifdef SK_VULKAN
#define XR_USE_GRAPHICS_API_VULKAN
#include "volk.h"
#endif

#include <csignal>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "Skore/App.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/Devices/Vulkan/VulkanDevice.hpp"


namespace Skore
{
	static Logger& logger = Logger::GetLogger("Skore::OpenXR");

	struct OpenXrSwapchain
	{
		bool AcquireNextImage(u32 currentFrame)
		{
			XrSwapchainImageAcquireInfo acquireImageInfo{};
			acquireImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

			XrResult result = xrAcquireSwapchainImage(xrSwapchain, &acquireImageInfo, &imageIndex);
			if (result != XR_SUCCESS)
			{
				logger.Error("Failed to acquire swapchain image: {}", static_cast<i32>(result));
				return false;
			}

			XrSwapchainImageWaitInfo waitImageInfo{};
			waitImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
			waitImageInfo.timeout = std::numeric_limits<int64_t>::max();

			result = xrWaitSwapchainImage(xrSwapchain, &waitImageInfo);

			if (result != XR_SUCCESS)
			{
				logger.Error("Failed to wait for swapchain image: {}", static_cast<i32>(result));
				return false;
			}

			return true;
		}


		void Release()
		{
			XrSwapchainImageReleaseInfo releaseImageInfo{};
			releaseImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
			XrResult result = xrReleaseSwapchainImage(xrSwapchain, &releaseImageInfo);

			if (result != XR_SUCCESS)
			{
				logger.Error("Failed to release swapchain image: {}", static_cast<i32>(result));
			}
		}

		void Destroy()
		{
			for (GPURenderPass* renderPass : renderPasses)
			{
				renderPass->Destroy();
			}

			xrDestroySwapchain(xrSwapchain);
			DestroyAndFree(this);
		}

		XrSwapchain xrSwapchain = {};
		u32         imageIndex;
		Extent        extent = {};
		Array<GPURenderPass*> renderPasses;
	};


	namespace
	{
		bool                     openXREnabled = false;
		XrInstance               instance = nullptr;
		XrDebugUtilsMessengerEXT debugMessenger = nullptr;
		XrSystemId               systemID;
		XrSession                session = nullptr;
		bool                     sessionRunning = false;
		XrSpace                  space = nullptr;


		Array<OpenXrSwapchain*> openXRSwapchains;

		PFN_xrVoidFunction GetXRFunction(XrInstance instance, const char* name)
		{
			PFN_xrVoidFunction func;

			XrResult result = xrGetInstanceProcAddr(instance, name, &func);

			if (result != XR_SUCCESS)
			{
				logger.Error("Failed to load OpenXR extension function '{}': {}", name, static_cast<i32>(result));
				return nullptr;
			}

			return func;
		}


		XrBool32 HandleXRError(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type, const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
		{
			// switch (type)
			// {
			// 	case XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT :
			// 		cerr << "general ";
			// 		break;
			// 	case XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT :
			// 		cerr << "validation ";
			// 		break;
			// 	case XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT :
			// 		cerr << "performance ";
			// 		break;
			// 	case XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT :
			// 		cerr << "conformance ";
			// 		break;
			// }

			switch (severity)
			{
				case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
					logger.Trace("{}", callbackData->message);
					break;
				case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
					logger.Info("{}", callbackData->message);
					break;
				case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
					logger.Warn("{}", callbackData->message);
					break;
				case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
					logger.Error("{}", callbackData->message);
					break;
				default:
					logger.Error("{}", callbackData->message);
					break;
			}

			return XR_FALSE;
		}

		XrDebugUtilsMessengerEXT CreateDebugMessenger(XrInstance instance)
		{
			XrDebugUtilsMessengerEXT debugMessenger;

			XrDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo{};
			debugMessengerCreateInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugMessengerCreateInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugMessengerCreateInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
			debugMessengerCreateInfo.userCallback = HandleXRError;
			debugMessengerCreateInfo.userData = nullptr;

			auto xrCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_xrCreateDebugUtilsMessengerEXT>(GetXRFunction(instance, "xrCreateDebugUtilsMessengerEXT"));

			XrResult result = xrCreateDebugUtilsMessengerEXT(instance, &debugMessengerCreateInfo, &debugMessenger);

			if (result != XR_SUCCESS)
			{
				logger.Error("Failed to create OpenXR debug messenger");
				return XR_NULL_HANDLE;
			}

			return debugMessenger;
		}

		void DestroyDebugMessenger(XrInstance instance, XrDebugUtilsMessengerEXT debugMessenger)
		{
			auto xrDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_xrDestroyDebugUtilsMessengerEXT>(GetXRFunction(instance, "xrDestroyDebugUtilsMessengerEXT"));
			xrDestroyDebugUtilsMessengerEXT(debugMessenger);
		}
	}

	void OpenXRManager::Init()
	{
		bool enableValidationLayers = false;

		RID openXRSettings = Settings::Get<ProjectSettings, OpenXRSettings>();
		if (ResourceObject openXRSettingsObject = Resources::Read(openXRSettings))
		{
			if (!openXRSettingsObject.GetBool(OpenXRSettings::EnableOpenXR))
			{
				return;
			}
			enableValidationLayers = openXRSettingsObject.GetBool(OpenXRSettings::EnableValidationLayers);
		}

		static const char* const  applicationName = "Skore";
		static const unsigned int majorVersion = 0;
		static const unsigned int minorVersion = 1;
		static const unsigned int patchVersion = 0;
		static const char* const  layerNames[] = {"XR_APILAYER_LUNARG_core_validation"};

		Array<const char*> extensions;
#ifdef SK_VULKAN
		extensions.EmplaceBack("XR_KHR_vulkan_enable");
		extensions.EmplaceBack("XR_KHR_vulkan_enable2");
#endif

		if (enableValidationLayers)
		{
			extensions.EmplaceBack("XR_EXT_debug_utils");
		}

		XrInstanceCreateInfo instanceCreateInfo{};
		instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
		instanceCreateInfo.createFlags = 0;
		strcpy(instanceCreateInfo.applicationInfo.applicationName, applicationName);
		instanceCreateInfo.applicationInfo.applicationVersion = XR_MAKE_VERSION(majorVersion, minorVersion, patchVersion);
		strcpy(instanceCreateInfo.applicationInfo.engineName, applicationName);
		instanceCreateInfo.applicationInfo.engineVersion = XR_MAKE_VERSION(majorVersion, minorVersion, patchVersion);
		instanceCreateInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);

		if (enableValidationLayers)
		{
			instanceCreateInfo.enabledApiLayerCount = 1;
			instanceCreateInfo.enabledApiLayerNames = layerNames;
		}
		else
		{
			instanceCreateInfo.enabledApiLayerCount = 0;
			instanceCreateInfo.enabledApiLayerNames = nullptr;
		}

		instanceCreateInfo.enabledExtensionCount = extensions.Size();
		instanceCreateInfo.enabledExtensionNames = extensions.Data();

		XrResult result = xrCreateInstance(&instanceCreateInfo, &instance);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to create OpenXR instance");
		}

		if (enableValidationLayers)
		{
			debugMessenger = CreateDebugMessenger(instance);
		}

		XrSystemGetInfo systemGetInfo{};
		systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
		systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

		result = xrGetSystem(instance, &systemGetInfo, &systemID);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to get OpenXR system");
			return;
		}

		// std::signal(SIGINT, [](int event)
		// {
		// 	//TODO
		// });

		Event::Bind<OnShutdown, Shutdown>();

		logger.Info("OpenXR initialized successfully");

		openXREnabled = true;
	}

	bool OpenXRManager::IsEnabled()
	{
		return openXREnabled;
	}

	void OpenXRManager::Shutdown()
	{
		if (session) xrDestroySession(session);
		if (debugMessenger) DestroyDebugMessenger(instance, debugMessenger);
		if (instance) xrDestroyInstance(instance);
	}

	void OpenXRManagerCreateSession()
	{
		if (!OpenXRManager::IsEnabled()) return;

		XrSessionCreateInfo sessionCreateInfo{};
		sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;

#if SK_VULKAN
		XrGraphicsBindingVulkanKHR graphicsBinding{};
		if (Graphics::GetAPI() == GraphicsAPI::Vulkan)
		{
			VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(Graphics::GetDevice());
			graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
			graphicsBinding.instance = vulkanDevice->instance;
			graphicsBinding.physicalDevice = vulkanDevice->selectedAdapter->device;
			graphicsBinding.device = vulkanDevice->device;
			graphicsBinding.queueFamilyIndex = vulkanDevice->selectedAdapter->graphicsFamily;
			graphicsBinding.queueIndex = 0;
			sessionCreateInfo.next = &graphicsBinding;
		}
#endif

		sessionCreateInfo.createFlags = 0;
		sessionCreateInfo.systemId = systemID;

		if (XrResult result = xrCreateSession(instance, &sessionCreateInfo, &session); result != XR_SUCCESS)
		{
			logger.Error("Failed to create OpenXR session: {}", static_cast<i32>(result));
		}

		XrReferenceSpaceCreateInfo spaceCreateInfo{};
		spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace = { { 0, 0, 0, 1 }, { 0, 0, 0 } };

		XrResult result = xrCreateReferenceSpace(session, &spaceCreateInfo, &space);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to create space: {}", static_cast<i32>(result));
		}

	}

	GPUSwapchain* OpenXRManagerCreateSwapchain()
	{
		uint32_t configViewsCount = EyeCount;

		Array<XrViewConfigurationView> configViews(
			configViewsCount,
			{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW}
		);

		XrResult result = xrEnumerateViewConfigurationViews(
			instance,
			systemID,
			XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			configViewsCount,
			&configViewsCount,
			configViews.Data()
		);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to enumerate view configuration views: {}", static_cast<i32>(result));
			return nullptr;
		}

		uint32_t formatCount = 0;
		result = xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);

		Array<int64_t> formats(formatCount);
		result = xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.Data());
		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to enumerate swapchain formats: {}", static_cast<i32>(result));
			return nullptr;
		}

		int64_t chosenFormat = formats[0];
		for (int64_t format : formats)
		{
			if (format == VK_FORMAT_R8G8B8A8_SRGB) //TODO - get corret format
			{
				chosenFormat = format;
				break;
			}
		}

		XrSwapchain xrSwapchain;

		XrSwapchainCreateInfo swapchainCreateInfo{};
		swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
		swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainCreateInfo.format = chosenFormat;
		swapchainCreateInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
		swapchainCreateInfo.width = configViews[0].recommendedImageRectWidth;
		swapchainCreateInfo.height = configViews[0].recommendedImageRectHeight;
		swapchainCreateInfo.faceCount = 1;
		swapchainCreateInfo.arraySize = 1;
		swapchainCreateInfo.mipCount = 1;

		result = xrCreateSwapchain(session, &swapchainCreateInfo, &xrSwapchain);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to create swapchain: {}", static_cast<i32>(result));
			return nullptr;
		}

		uint32_t imageCount;
		result = xrEnumerateSwapchainImages(xrSwapchain, 0, &imageCount, nullptr);

		if (result != XR_SUCCESS)
		{
			//cerr << "Failed to enumerate swapchain images: " << result << endl;
			return {};
		}

		OpenXrSwapchain* openXRSwapchain = Alloc<OpenXrSwapchain>();
		openXRSwapchain->xrSwapchain = xrSwapchain;
		openXRSwapchain->extent = {configViews[0].recommendedImageRectWidth, configViews[0].recommendedImageRectHeight};
		openXRSwapchains.EmplaceBack(openXRSwapchain);

#if 0
#if SK_VULKAN
		if (Graphics::GetAPI() == GraphicsAPI::Vulkan)
		{
			VulkanDevice* vulkanDevice = static_cast<VulkanDevice*>(Graphics::GetDevice());

			Array<XrSwapchainImageVulkanKHR> images(imageCount, {.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
			result = xrEnumerateSwapchainImages(xrSwapchain, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)images.Data());

			if (result != XR_SUCCESS)
			{
				//cerr << "Failed to enumerate swapchain images: " << result << endl;
				return {};
			}

			openXRSwapchain->renderPasses.Resize(imageCount);

			for (uint32_t i = 0; i < imageCount; i++)
			{
				VulkanRenderPass* vulkanRenderPass = Alloc<VulkanRenderPass>();
				vulkanRenderPass->vulkanDevice = vulkanDevice;
				vulkanRenderPass->extent = {openXRSwapchain->extent.width, openXRSwapchain->extent.height};
				vulkanRenderPass->clearValues.Resize(1);
				vulkanRenderPass->formats.EmplaceBack(static_cast<VkFormat>(chosenFormat));

				VkImageViewCreateInfo imageViewCreateInfo{};
				imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				imageViewCreateInfo.image = images[i].image;
				imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				imageViewCreateInfo.format = static_cast<VkFormat>(chosenFormat);
				imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
				imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
				imageViewCreateInfo.subresourceRange.levelCount = 1;
				imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
				imageViewCreateInfo.subresourceRange.layerCount = 1;
				vkCreateImageView(vulkanDevice->device, &imageViewCreateInfo, nullptr, &vulkanRenderPass->imageView);

				VkAttachmentDescription attachmentDescription{};
				VkAttachmentReference   colorAttachmentReference{};

				attachmentDescription.format = static_cast<VkFormat>(chosenFormat);
				attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

				colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colorAttachmentReference.attachment = 0;

				VkSubpassDescription subPass = {};
				subPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subPass.colorAttachmentCount = 1;
				subPass.pColorAttachments = &colorAttachmentReference;

				VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
				renderPassInfo.attachmentCount = 1;
				renderPassInfo.pAttachments = &attachmentDescription;
				renderPassInfo.subpassCount = 1;
				renderPassInfo.pSubpasses = &subPass;
				renderPassInfo.dependencyCount = 0;
				vkCreateRenderPass(vulkanDevice->device, &renderPassInfo, nullptr, &vulkanRenderPass->renderPass);

				VkFramebufferCreateInfo framebufferCreateInfo{};
				framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferCreateInfo.renderPass = vulkanRenderPass->renderPass;
				framebufferCreateInfo.width = vulkanRenderPass->extent.width;
				framebufferCreateInfo.height = vulkanRenderPass->extent.height;
				framebufferCreateInfo.layers = 1u;
				framebufferCreateInfo.attachmentCount = 1;
				framebufferCreateInfo.pAttachments = &vulkanRenderPass->imageView;
				vkCreateFramebuffer(vulkanDevice->device, &framebufferCreateInfo, nullptr, &vulkanRenderPass->framebuffer);

				openXRSwapchain->renderPasses[i] = vulkanRenderPass;
			}
		}
		return openXRSwapchain;
#endif
#endif
		return {};
	}


	AppResult OpenXrManagerIterate()
	{
		if (!OpenXRManager::IsEnabled()) return AppResult::Continue;

		XrEventDataBuffer eventData{};
		eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
		XrResult result = xrPollEvent(instance, &eventData);

		if (result == XR_EVENT_UNAVAILABLE)
		{
			if (sessionRunning)
			{
				XrFrameWaitInfo frameWaitInfo{};
				frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

				XrFrameState frameState{};
				frameState.type = XR_TYPE_FRAME_STATE;

				XrResult result = xrWaitFrame(session, &frameWaitInfo, &frameState);
				if (result != XR_SUCCESS)
				{
					logger.Error("Failed to wait for frame: {}", static_cast<i32>(result));
					return AppResult::Failure;
				}

				XrFrameBeginInfo beginFrameInfo{};
				beginFrameInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
				result = xrBeginFrame(session, &beginFrameInfo);
				if (result != XR_SUCCESS)
				{
					logger.Error("Failed to begin frame: {}", static_cast<i32>(result));
					return AppResult::Failure;
				}

				XrViewLocateInfo viewLocateInfo{};
				viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
				viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				viewLocateInfo.displayTime = frameState.predictedDisplayTime;
				viewLocateInfo.space = space;

				XrViewState viewState{};
				viewState.type = XR_TYPE_VIEW_STATE;

				uint32_t viewCount = EyeCount;
				Array<XrView> views(viewCount, {.type = XR_TYPE_VIEW});

				xrLocateViews(session, &viewLocateInfo, &viewState, viewCount, &viewCount, views.Data());

				//TODO

				XrCompositionLayerProjectionView projectedViews[2]{};

				for (size_t i = 0; i < EyeCount; i++)
				{
					projectedViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
					projectedViews[i].pose = views[i].pose;
					projectedViews[i].fov = views[i].fov;
					projectedViews[i].subImage = {
						openXRSwapchains[i]->xrSwapchain,
						{
							{0, 0},
							{static_cast<i32>(openXRSwapchains[i]->extent.width), static_cast<i32>(openXRSwapchains[i]->extent.height)}
						},
						0
					};
				}

				XrCompositionLayerProjection layer{};
				layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
				layer.space = space;
				layer.viewCount = EyeCount;
				layer.views = projectedViews;
				auto pLayer = (const XrCompositionLayerBaseHeader*)&layer;

				XrFrameEndInfo endFrameInfo{};
				endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
				endFrameInfo.displayTime = frameState.predictedDisplayTime;
				endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
				endFrameInfo.layerCount = 1;
				endFrameInfo.layers = &pLayer;

				XrResult endFrameResult = xrEndFrame(session, &endFrameInfo);

				if (endFrameResult != XR_SUCCESS)
				{
					logger.Error("Failed to end frame: {}", static_cast<i32>(endFrameResult));
					//return AppResult::Failure;
				}
			}
		}
		else if (result != XR_SUCCESS)
		{
			logger.Error("Failed to poll events: {}", static_cast<i32>(result));
			return AppResult::Failure;
		}
		else
		{
			switch (eventData.type)
			{
				default:
					logger.Error("Unknown event type received: {}", static_cast<u64>(eventData.type));
					break;
				case XR_TYPE_EVENT_DATA_EVENTS_LOST:
					logger.Error("Event queue overflowed and events were lost.");
					break;
				case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
					logger.Debug("OpenXR instance is shutting down.");
					return AppResult::Success;
				case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
					logger.Debug("The interaction profile has changed.");
					break;
				case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
					logger.Debug("The reference space is changing.");
					break;
				case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
				{
					auto event = (XrEventDataSessionStateChanged*)&eventData;
					switch (event->state)
					{
						case XR_SESSION_STATE_UNKNOWN:
						case XR_SESSION_STATE_MAX_ENUM:
							logger.Error("Unknown session state entered: {}", static_cast<u64>(event->state));
							break;
						case XR_SESSION_STATE_IDLE:
							//return AppResult::Success;
							//sessionRunning = false;
							break;
						case XR_SESSION_STATE_READY:
						{
							XrSessionBeginInfo sessionBeginInfo{};
							sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
							sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

							result = xrBeginSession(session, &sessionBeginInfo);

							if (result != XR_SUCCESS)
							{
								logger.Error("Failed to begin session: {}", static_cast<u32>(result));
							}

							logger.Debug("OpenXR session is ready.");
							sessionRunning = true;
							break;
						}
						case XR_SESSION_STATE_SYNCHRONIZED:
						case XR_SESSION_STATE_VISIBLE:
						case XR_SESSION_STATE_FOCUSED:
							break;
						case XR_SESSION_STATE_STOPPING:
							result = xrEndSession(session);

							if (result != XR_SUCCESS)
							{
								logger.Error("Failed to end session: {}", static_cast<u32>(result));
							}

							logger.Debug("OpenXR session is stopping.");
							sessionRunning = false;
							break;
						case XR_SESSION_STATE_LOSS_PENDING:
							logger.Debug("OpenXR session is shutting down.");
							return AppResult::Success;
						case XR_SESSION_STATE_EXITING:
							logger.Debug("OpenXR runtime requested shutdown.");
							return AppResult::Success;
					}
					break;
				}
			}
		}

		return AppResult::Continue;
	}

#if SK_VULKAN

	struct VulkanGraphicsRequirements
	{
		u64 minApiVersionSupported;
		u64 maxApiVersionSupported;
	};

	VulkanGraphicsRequirements OpenXRManagerGetGraphicsRequirements()
	{
		if (!OpenXRManager::IsEnabled()) return {};

		auto xrGetVulkanGraphicsRequirementsKHR = (PFN_xrGetVulkanGraphicsRequirementsKHR)GetXRFunction(instance, "xrGetVulkanGraphicsRequirementsKHR");

		XrGraphicsRequirementsVulkanKHR graphicsRequirements{};
		graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
		XrResult result = xrGetVulkanGraphicsRequirementsKHR(instance, systemID, &graphicsRequirements);
		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to get OpenXR graphics requirements: {}", static_cast<i32>(result));
			return {};
		}
		return VulkanGraphicsRequirements{graphicsRequirements.minApiVersionSupported, graphicsRequirements.maxApiVersionSupported};
	}


	Array<String> OpenXRManagerGetInstanceExtensions()
	{
		if (!OpenXRManager::IsEnabled()) return {};

		Array<String> extensions;

		PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(GetXRFunction(instance, "xrGetVulkanInstanceExtensionsKHR"));
		uint32_t                             instanceExtensionsSize;

		xrGetVulkanInstanceExtensionsKHR(instance, systemID, 0, &instanceExtensionsSize, nullptr);
		char* instanceExtensionsData = new char[instanceExtensionsSize];
		xrGetVulkanInstanceExtensionsKHR(instance, systemID, instanceExtensionsSize, &instanceExtensionsSize, instanceExtensionsData);

		uint32_t last = 0;
		for (uint32_t i = 0; i <= instanceExtensionsSize; i++)
		{
			if (i == instanceExtensionsSize || instanceExtensionsData[i] == ' ')
			{
				extensions.EmplaceBack(String(instanceExtensionsData + last, i - last));
				last = i + 1;
			}
		}

		delete[] instanceExtensionsData;

		return extensions;
	}

	VkPhysicalDevice OpenXRManagerGetPhysicalDevice(VkInstance vulkanInstance)
	{
		if (!OpenXRManager::IsEnabled()) return {};

		auto xrGetVulkanGraphicsDeviceKHR = (PFN_xrGetVulkanGraphicsDeviceKHR)GetXRFunction(instance, "xrGetVulkanGraphicsDeviceKHR");

		VkPhysicalDevice physicalDevice;
		XrResult         result = xrGetVulkanGraphicsDeviceKHR(instance, systemID, vulkanInstance, &physicalDevice);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to get OpenXR physical device");
			return {};
		}

		return physicalDevice;
	}

	Array<String> OpenXRManagerGetDeviceExtensions()
	{
		if (!OpenXRManager::IsEnabled()) return {};

		auto xrGetVulkanDeviceExtensionsKHR = (PFN_xrGetVulkanDeviceExtensionsKHR)GetXRFunction(instance, "xrGetVulkanDeviceExtensionsKHR");

		uint32_t deviceExtensionsSize;

		XrResult result = xrGetVulkanDeviceExtensionsKHR(instance, systemID, 0, &deviceExtensionsSize, nullptr);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to get Vulkan device extensions: {}", static_cast<i32>(result));
			return {};
		}

		char* deviceExtensionsData = new char[deviceExtensionsSize];

		result = xrGetVulkanDeviceExtensionsKHR(instance, systemID, deviceExtensionsSize, &deviceExtensionsSize, deviceExtensionsData);

		if (result != XR_SUCCESS)
		{
			logger.Error("Failed to get Vulkan device extensions: {}", static_cast<i32>(result));
			return {};
		}


		Array<String> deviceExtensions;

		uint32_t last = 0;
		for (uint32_t i = 0; i <= deviceExtensionsSize; i++)
		{
			if (i == deviceExtensionsSize || deviceExtensionsData[i] == ' ')
			{
				deviceExtensions.EmplaceBack(String(deviceExtensionsData + last, i - last));
				last = i + 1;
			}
		}

		delete[] deviceExtensionsData;

		return deviceExtensions;
	}

#endif

	void RegisterOpenXRTypes()
	{
#ifdef SK_ENABLE_ALPHA_FEATURES
		Resources::Type<OpenXRSettings>()
			.Field<OpenXRSettings::EnableOpenXR>(ResourceFieldType::Bool)
			.Field<OpenXRSettings::EnableValidationLayers>(ResourceFieldType::Bool)
			.Attribute<EditableSettings>(EditableSettings{
				.path = "Engine/Open XR",
				.type = TypeInfo<ProjectSettings>::ID(),
				.order = 20
			})
			.Build();
#endif
	}
}