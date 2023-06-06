#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h> 
#include <memory.h>

#define SAMPLE_USE_D3D12 1

//Platform includes
#include "Platform/Core/EnvironmentVariables.h"
#if SAMPLE_USE_D3D12
#include "Platform/DirectX12/RenderPlatform.h"
#include "Platform/DirectX12/DeviceManager.h"
#endif


#include "Platform/CrossPlatform/PlatformStructuredBuffer.h"
#include "Platform/CrossPlatform/Effect.h"
#include "Platform/CrossPlatform/RenderDocLoader.h"
#include "Platform/CrossPlatform/HDRRenderer.h"
#include "Platform/CrossPlatform/SphericalHarmonics.h"
#include "Platform/CrossPlatform/View.h"
#include "Platform/CrossPlatform/Mesh.h"
#include "Platform/CrossPlatform/MeshRenderer.h"
#include "Platform/CrossPlatform/GpuProfiler.h"
#include "Platform/CrossPlatform/Camera.h"
#include "Platform/CrossPlatform/DeviceContext.h"
#include "Platform/Core/CommandLineParams.h"
#include "Platform/CrossPlatform/DisplaySurfaceManager.h"
#include "Platform/CrossPlatform/BaseFramebuffer.h"
#include "Platform/Shaders/Sl/camera_constants.sl"
#include "Platform/Math/Pi.h"

#include "Shaders/atmospheric_transmittance_constants.sl"

#ifdef _MSC_VER
#include "Platform/Windows/VisualStudioDebugOutput.h"
VisualStudioDebugOutput debug_buffer(true, NULL, 128);
#endif

using namespace simul;

//Other Windows Header Files
#include <SDKDDKVer.h>
#include <shellapi.h>
#include <random>

#define STRING_OF_MACRO1(x) #x
#define STRING_OF_MACRO(x) STRING_OF_MACRO1(x)

#if SAMPLE_USE_D3D12
dx12::DeviceManager dx12_deviceManager;
#endif


crossplatform::GraphicsDeviceInterface* graphicsDeviceInterface;
crossplatform::DisplaySurfaceManager displaySurfaceManager;

platform::core::CommandLineParams commandLineParams;

crossplatform::ConstantBuffer<cbAtmosphere>	atmosphereConstants;

crossplatform::Texture* transmittanceTexture;
crossplatform::Texture* directIrradianceTexture;
crossplatform::Texture* singleScatteringTexture;
crossplatform::Texture* multipleScatteringTexture;
crossplatform::Texture* scatteringDensityTexture;

bool texturesGenerated = false;

HWND hWnd = nullptr;
HINSTANCE hInst;
wchar_t wszWindowClass[] = L"AtmosphericScatteringTest";
int kOverrideWidth = 1440;
int kOverrideHeight = 900;

float mu_s = 0.5;
float height = 0.0;

enum class TestType
{
	UNKNOWN,
	CLEAR_COLOUR,
	QUAD_COLOUR,
	TEXT,
	CHECKERBOARD,
	FIBONACCI,
	TVTESTCARD,
	EXTERNAL
};

static float rayleigh_approx(float l)
{
	static double N = 2.545e-14;
	static double n = 1.000293;

	const double pn = 0.0035;

	double result = 0;

	result = pow(2.0 * SIMUL_PI_D, 3.0);
	result *= pow(n * n - 1.0, 2.0);
	result /= (3.0 * N * pow(l, 4.0));
	result *= (6.0 + 3.0 * pn);
	result /= (6.0 - 7.0 * pn);
	return float(result);
}

static float mie_approx(float l)
{
	double result = 0;

	double lambda = static_cast<double>(l) * 1e-3;  // micro-meters
	result = pow(lambda, -0.0);
	result *= 5.328e-3 / 1200.0;

	return float(result);
}

class PlatformRenderer : public crossplatform::PlatformRendererInterface
{
public:
	crossplatform::RenderPlatformType renderPlatformType = crossplatform::RenderPlatformType::Unknown;
	TestType testType;
	bool debug = false;
	const bool reverseDepth = true;
	int framenumber = 0;

	//Render Primitives
	crossplatform::RenderPlatform* renderPlatform = nullptr;
	crossplatform::Texture* depthTexture = nullptr;
	crossplatform::HdrRenderer* hdrRenderer = nullptr;
	crossplatform::BaseFramebuffer* hdrFramebuffer = nullptr;
	crossplatform::Effect* effect = nullptr;
	crossplatform::Effect* transmittanceEffect = nullptr;
	crossplatform::Effect* scatteringEffect = nullptr;
	crossplatform::ConstantBuffer<SceneConstants>	sceneConstants;
	crossplatform::ConstantBuffer<CameraConstants>	cameraConstants;

	//Scene Objects
	crossplatform::Camera							camera;

	//Test Object
	crossplatform::StructuredBuffer<uint>			rwSB;
	crossplatform::StructuredBuffer<vec4>			roSB;

public:
	PlatformRenderer(const crossplatform::RenderPlatformType& rpType, const TestType& tType, bool use_debug)
		:renderPlatformType(rpType), testType(tType), debug(use_debug)
	{
		//Inital RenderPlatform and RenderDoc
		//if (debug)
		crossplatform::RenderDocLoader::Load();

		graphicsDeviceInterface = &dx12_deviceManager;
		renderPlatform = new dx12::RenderPlatform();

		
		graphicsDeviceInterface->Initialize(debug, false, false);

		//RenderPlatforn Set up
		renderPlatform->SetShaderBuildMode(simul::crossplatform::ShaderBuildMode::BUILD_IF_CHANGED);
		renderPlatform->PushTexturePath("Textures");

		switch (renderPlatformType)
		{
		case crossplatform::RenderPlatformType::D3D11:
		{
			renderPlatform->PushShaderPath("Platform/DirectX11/HLSL/");
			renderPlatform->PushShaderPath("../../../../Platform/DirectX11/HLSL");
			renderPlatform->PushShaderPath("../../Platform/DirectX11/HLSL");
			break;
		}
		default:
		case crossplatform::RenderPlatformType::D3D12:
		{
			renderPlatform->PushShaderPath("Platform/DirectX12/HLSL/");
			renderPlatform->PushShaderPath("../../../../Platform/DirectX12/HLSL");
			renderPlatform->PushShaderPath("../../Platform/DirectX12/HLSL");
			//renderPlatform->PushShaderPath("C:/AtmosphericScatteringTesting/AtmosphericScatteringTesting/x64/Release/shaderbin/DirectX12");
			break;
		}
		case crossplatform::RenderPlatformType::Vulkan:
		{
			renderPlatform->PushShaderPath("Platform/Vulkan/GLSL/");
			renderPlatform->PushShaderPath("../../../../Platform/Vulkan/GLSL");
			renderPlatform->PushShaderPath("../../Platform/Vulan/GLSL");
			break;
		}
		case crossplatform::RenderPlatformType::OpenGL:
		{
			renderPlatform->PushShaderPath("Platform/OpenGL/GLSL/");
			renderPlatform->PushShaderPath("../../../../Platform/OpenGL/GLSL");
			renderPlatform->PushShaderPath("../../Platform/OpenGL/GLSL");
			break;
		}
		}

		renderPlatform->PushShaderPath("Platform/Shaders/SFX/");
		renderPlatform->PushShaderPath("../../../../Platform/CrossPlatform/SFX");
		renderPlatform->PushShaderPath("../../Platform/CrossPlatform/SFX");
		renderPlatform->PushShaderPath("../../../../Shaders/SFX/");
		renderPlatform->PushShaderPath("../../../../Shaders/SL/");
		renderPlatform->PushShaderPath("../../Shaders/SFX/");
		renderPlatform->PushShaderPath("../../Shaders/SL/");

		// Shader binaries: we want to use a shared common directory under Simul/Media. But if we're running from some other place, we'll just create a "shaderbin" directory.
		std::string cmake_binary_dir = STRING_OF_MACRO(CMAKE_BINARY_DIR);
		std::string cmake_source_dir = STRING_OF_MACRO(CMAKE_SOURCE_DIR);
		if (cmake_binary_dir.length())
		{
			renderPlatform->PushShaderPath(((std::string(STRING_OF_MACRO(PLATFORM_SOURCE_DIR)) + "/") + renderPlatform->GetPathName() + "/HLSL").c_str());
			renderPlatform->PushShaderPath(((std::string(STRING_OF_MACRO(PLATFORM_SOURCE_DIR)) + "/") + renderPlatform->GetPathName() + "/GLSL").c_str());
			renderPlatform->PushShaderBinaryPath(((cmake_binary_dir + "/") + renderPlatform->GetPathName() + "/shaderbin").c_str());
			std::string platform_build_path = ((cmake_binary_dir + "/Platform/") + renderPlatform->GetPathName());
			renderPlatform->PushShaderBinaryPath((platform_build_path + "/shaderbin").c_str());
			renderPlatform->PushTexturePath((cmake_source_dir + "/Resources/Textures").c_str());
		}
		renderPlatform->PushShaderBinaryPath((std::string("shaderbin/") + renderPlatform->GetPathName()).c_str());

		renderPlatform->PushShaderBinaryPath("C:/AtmosphericScatteringTesting/AtmosphericScatteringTesting/x64/Release/shaderbin/DirectX12");

		//Set up HdrRenderer and Depth texture
		depthTexture = renderPlatform->CreateTexture("Depth-Stencil"); //Calls new
		hdrRenderer = new crossplatform::HdrRenderer();

		//Set up BaseFramebuffer
		hdrFramebuffer = renderPlatform->CreateFramebuffer(); //Calls new
		hdrFramebuffer->SetFormat(crossplatform::RGBA_16_FLOAT);
		hdrFramebuffer->SetDepthFormat(crossplatform::D_32_FLOAT);
		hdrFramebuffer->SetAntialiasing(1);
		hdrFramebuffer->DefaultClearColour = vec4(0.0f, 0.0f, 0.0f, 1.0f);
		hdrFramebuffer->DefaultClearDepth = reverseDepth ? 0.0f : 1.0f;
		hdrFramebuffer->DefaultClearStencil = 0;

		vec3 look = { 0.0f, 1.0f, 0.0f }, up = { 0.0f, 0.0f, 1.0f };
		camera.LookInDirection(look, up);
		camera.SetHorizontalFieldOfViewDegrees(90.0f);
		camera.SetVerticalFieldOfViewDegrees(0.0f);// Automatic vertical fov - depends on window shape

		crossplatform::CameraViewStruct vs;
		vs.exposure = 1.0f;
		vs.gamma = 0.44f;
		vs.projection = reverseDepth ? crossplatform::DEPTH_REVERSE : crossplatform::FORWARD;
		vs.nearZ = 0.1f;
		vs.farZ = 300000.f;
		vs.InfiniteFarPlane = true;
		camera.SetCameraViewStruct(vs);
	}

	~PlatformRenderer()
	{
		delete hdrFramebuffer;
		delete hdrRenderer;
		delete depthTexture;
		delete renderPlatform;
		graphicsDeviceInterface->Shutdown();
	}

	void OnCreateDevice()
	{
#ifdef SAMPLE_USE_D3D12
		if (renderPlatformType == crossplatform::RenderPlatformType::D3D12)
		{
			// We will provide a command list so initialization of following resource can take place
			((dx12::RenderPlatform*)renderPlatform)->SetImmediateContext((dx12::ImmediateContext*)dx12_deviceManager.GetImmediateContext());
		}
#endif
		void* device = graphicsDeviceInterface->GetDevice();
		renderPlatform->RestoreDeviceObjects(device);

		hdrRenderer->RestoreDeviceObjects(renderPlatform);
		hdrFramebuffer->RestoreDeviceObjects(renderPlatform);
		effect = renderPlatform->CreateEffect();
		effect->Load(renderPlatform, "solid");
		transmittanceEffect = renderPlatform->CreateEffect("atmospheric_transmittance");
		scatteringEffect = renderPlatform->CreateEffect("atmospheric_scattering");
		sceneConstants.RestoreDeviceObjects(renderPlatform);
		sceneConstants.LinkToEffect(effect, "SolidConstants");
		cameraConstants.RestoreDeviceObjects(renderPlatform);

		atmosphereConstants.RestoreDeviceObjects(renderPlatform);	

#ifdef SAMPLE_USE_D3D12
		if (renderPlatformType == crossplatform::RenderPlatformType::D3D12)
		{
			dx12_deviceManager.FlushImmediateCommandList();
		}
#endif
	}

	void OnLostDevice()
	{
		if (effect)
		{
			effect->InvalidateDeviceObjects();
			delete effect;
			effect = nullptr;
		}
		if (transmittanceEffect)
		{
			transmittanceEffect->InvalidateDeviceObjects();
			delete transmittanceEffect;
			transmittanceEffect = nullptr;
		}
		if (scatteringEffect)
		{
			scatteringEffect->InvalidateDeviceObjects();
			delete scatteringEffect;
			scatteringEffect = nullptr;
		}
		sceneConstants.InvalidateDeviceObjects();
		cameraConstants.InvalidateDeviceObjects();
		atmosphereConstants.InvalidateDeviceObjects();
		hdrRenderer->InvalidateDeviceObjects();
		hdrFramebuffer->InvalidateDeviceObjects();
		renderPlatform->InvalidateDeviceObjects();
	}

	void OnDestroyDevice()
	{
		OnLostDevice();
	}

	bool OnDeviceRemoved()
	{
		OnLostDevice();
		return true;
	}

	int AddView() override
	{
		static int last_view_id = 0;
		return last_view_id++;
	};
	void RemoveView(int id) override {};
	void ResizeView(int view_id, int W, int H) override {};

	void Render(int view_id, void* context, void* colorBuffer, int w, int h, long long frame, void* context_allocator = nullptr) override
	{
		if (w * h == 0) //FramebufferGL can't deal with a viewport of {0,0,0,0}!
			return;

		// Device context structure
		simul::crossplatform::GraphicsDeviceContext	deviceContext;

		// Store back buffer, depth buffer and viewport information
		deviceContext.defaultTargetsAndViewport.num = 1;
		deviceContext.defaultTargetsAndViewport.m_rt[0] = colorBuffer;
		deviceContext.defaultTargetsAndViewport.rtFormats[0] = crossplatform::UNKNOWN; //To be later defined in the pipeline
		deviceContext.defaultTargetsAndViewport.m_dt = nullptr;
		deviceContext.defaultTargetsAndViewport.depthFormat = crossplatform::UNKNOWN;
		deviceContext.defaultTargetsAndViewport.viewport = { 0, 0, w, h };
		deviceContext.frame_number = framenumber;
		deviceContext.platform_context = context;
		deviceContext.renderPlatform = renderPlatform;
		deviceContext.viewStruct.view_id = view_id;
		deviceContext.viewStruct.depthTextureStyle = crossplatform::PROJECTION;
		{
			deviceContext.viewStruct.view = camera.MakeViewMatrix();
			float aspect = (float)kOverrideWidth / (float)kOverrideHeight;
			if (reverseDepth)
			{
				deviceContext.viewStruct.proj = camera.MakeDepthReversedProjectionMatrix(aspect);
			}
			else
			{
				deviceContext.viewStruct.proj = camera.MakeProjectionMatrix(aspect);
			}
			deviceContext.viewStruct.Init();
		}

		//Begin frame
		renderPlatform->BeginFrame(deviceContext);
		hdrFramebuffer->SetWidthAndHeight(w, h);
		hdrFramebuffer->Activate(deviceContext);

		switch (testType)
		{
		default:
		case TestType::EXTERNAL:
		{
			Test_External(deviceContext, w, h);
			break;
		}
		}

		hdrFramebuffer->Deactivate(deviceContext);
		hdrRenderer->Render(deviceContext, hdrFramebuffer->GetTexture(), 1.0f, 0.44f);

		framenumber++;
	}


	void Test_External(crossplatform::GraphicsDeviceContext deviceContext, int w, int h)
	{
		hdrFramebuffer->Clear(deviceContext, 0.00f, 0.31f, 0.57f, 1.00f, reverseDepth ? 0.0f : 1.0f);

		if (!texturesGenerated)
		{
			transmittanceTexture = renderPlatform->CreateTexture();
			directIrradianceTexture = renderPlatform->CreateTexture();
			singleScatteringTexture = renderPlatform->CreateTexture();
			multipleScatteringTexture = renderPlatform->CreateTexture();
			scatteringDensityTexture = renderPlatform->CreateTexture();
			transmittanceTexture->ensureTexture2DSizeAndFormat(renderPlatform, 256, 256, 1, crossplatform::PixelFormat::RGBA_32_FLOAT, false, true, false, 1, 0, false, vec4(0.0, 0.0, 0.0, 0.0));
			directIrradianceTexture->ensureTexture2DSizeAndFormat(renderPlatform, 256, 256, 1, crossplatform::PixelFormat::RGBA_32_FLOAT, false, true, false, 1, 0, false, vec4(0.0, 0.0, 0.0, 0.0));
			singleScatteringTexture->ensureTexture3DSizeAndFormat(renderPlatform, 256, 128, 32, crossplatform::PixelFormat::RGBA_32_FLOAT, true, 1, false);
			renderPlatform->ClearTexture(deviceContext, singleScatteringTexture, vec4(1.0, 0.0, 1.0, 0.0));
			multipleScatteringTexture->ensureTexture3DSizeAndFormat(renderPlatform, 256, 128, 32, crossplatform::PixelFormat::RGBA_32_FLOAT, true, 1, false);
			renderPlatform->ClearTexture(deviceContext, multipleScatteringTexture, vec4(0.0, 0.0, 0.0, 0.0));
			scatteringDensityTexture->ensureTexture3DSizeAndFormat(renderPlatform, 256, 128, 32, crossplatform::PixelFormat::RGBA_32_FLOAT, true, 1, false);
			renderPlatform->ClearTexture(deviceContext, scatteringDensityTexture, vec4(0.0, 0.0, 0.0, 0.0));

			//crossplatform::Effect* transmittance = renderPlatform->CreateEffect("atmospheric_transmittance");
			crossplatform::EffectTechnique* precompute_transmittance = transmittanceEffect->GetTechniqueByName("precompute_transmittance");
			crossplatform::EffectTechnique* precompute_direct_irradiance = scatteringEffect->GetTechniqueByName("precompute_direct_irradiance");
			crossplatform::EffectTechnique* precompute_single_scattering = scatteringEffect->GetTechniqueByName("precompute_single_scattering");
			crossplatform::EffectTechnique* precompute_scattering_density_texture = scatteringEffect->GetTechniqueByName("precompute_scattering_density_texture");
			crossplatform::EffectTechnique* precompute_multiple_scattering_texture = scatteringEffect->GetTechniqueByName("precompute_multiple_scattering_texture");

			atmosphereConstants.LinkToEffect(transmittanceEffect, "cbAtmosphere");
			atmosphereConstants.LinkToEffect(scatteringEffect, "cbAtmosphere");

			atmosphereConstants.g_topRadius = 6420000.0;
			atmosphereConstants.g_bottomRadius = 6360000.0;
			atmosphereConstants.g_mu_s_min = -0.2;

			atmosphereConstants.g_rayleighExpTerm = 1.f;
			atmosphereConstants.g_rayleighExpScale = -1.f / 8000.f;
			atmosphereConstants.g_rayleighLinearTerm = 0.f;
			atmosphereConstants.g_rayleighConstantTerm = 0.f;

			atmosphereConstants.g_rayleighScattering = vec3(rayleigh_approx(630), rayleigh_approx(550), rayleigh_approx(440)) * 0.001f;
			atmosphereConstants.g_rayleighDensity;

			atmosphereConstants.g_mieExpTerm = 1.f;
			atmosphereConstants.g_mieExpScale = -1.f / 1200.f;
			atmosphereConstants.g_mieLinearTerm = 0.f;
			atmosphereConstants.g_mieConstantTerm = 0.f;

			double haze = 1.f;
			double nu = 4.0;
			double T = (1.0 + haze);
			double c = (0.6544 * T - 0.6510) * 1e-16;
			if (haze > 1.0f)
				c /= haze;
			if (c < 0.0)
				c = 0.0;
			vec3 Mie;
			Mie.x = (float)(0.434 * c * SIMUL_PI_D * pow(2.0 * SIMUL_PI_D / (680.f * 1e-9), nu - 2) * 0.68455);
			Mie.y = (float)(0.434 * c * SIMUL_PI_D * pow(2.0 * SIMUL_PI_D / (550.f * 1e-9), nu - 2) * 0.673323);
			Mie.z = (float)(0.434 * c * SIMUL_PI_D * pow(2.0 * SIMUL_PI_D / (440.f * 1e-9), nu - 2) * 0.6691485);

			atmosphereConstants.g_mieScattering = vec3(mie_approx(630), mie_approx(550), mie_approx(440));
			atmosphereConstants.g_miePhaseFunction = 0.8f;

			atmosphereConstants.g_mieExtinction = Mie * 0.001f;

			atmosphereConstants.g_absorptionExpTerm = 0.f;
			atmosphereConstants.g_absorptionExpScale = 0.f;
			atmosphereConstants.g_absorptionLinearTerm = 1.f / 15000.f;
			atmosphereConstants.g_absorptionConstantTerm = -2.f / 3.f;

			//atmosphereConstants.g_absorptionExtinction = (300.0 * 2.687e20 / 15000.0) * vec3(1.582000e-26, 3.500000e-25, 1.209000e-25);

			atmosphereConstants.g_solarIrradiance = 1.5;
			atmosphereConstants.g_groundAlbedo = 0.1;
			atmosphereConstants.g_scatteringOrder = 2;

			atmosphereConstants.g_mu_s = mu_s;
			atmosphereConstants.g_height = height;

			effect->SetConstantBuffer(deviceContext, &atmosphereConstants);

			transmittanceEffect->Apply(deviceContext, precompute_transmittance, 0);

			transmittanceTexture->activateRenderTarget(deviceContext);
			renderPlatform->DrawQuad(deviceContext);
			transmittanceTexture->deactivateRenderTarget(deviceContext);

			transmittanceEffect->Unapply(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_direct_irradiance, 0);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			directIrradianceTexture->activateRenderTarget(deviceContext);
			renderPlatform->DrawQuad(deviceContext);
			directIrradianceTexture->deactivateRenderTarget(deviceContext);

			scatteringEffect->Unapply(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_single_scattering, 0);
			scatteringEffect->SetUnorderedAccessView(deviceContext, "singleScatteringOutput", singleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			renderPlatform->DispatchCompute(deviceContext, 256, 128, 32);
			scatteringEffect->Unapply(deviceContext);
			scatteringEffect->UnbindTextures(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_scattering_density_texture, 0);
			scatteringEffect->SetUnorderedAccessView(deviceContext, "scatteringDensityOutput", scatteringDensityTexture);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			scatteringEffect->SetTexture(deviceContext, "g_DirectIrradiance", directIrradianceTexture);
			scatteringEffect->SetTexture(deviceContext, "g_singleScattering", singleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_multipleScattering", multipleScatteringTexture);
			renderPlatform->DispatchCompute(deviceContext, 256, 128, 32);
			scatteringEffect->Unapply(deviceContext);
			scatteringEffect->UnbindTextures(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_multiple_scattering_texture, 0);
			scatteringEffect->SetUnorderedAccessView(deviceContext, "multipleScatteringOutput", multipleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			scatteringEffect->SetTexture(deviceContext, "g_singleScattering", singleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_scatteringDensityTexture", scatteringDensityTexture);
			renderPlatform->DispatchCompute(deviceContext, 256, 128, 32);
			scatteringEffect->Unapply(deviceContext);
			scatteringEffect->UnbindTextures(deviceContext);

			texturesGenerated = true;
		}
		if (mu_s > 1.0)
			mu_s = 1.0;
		if (mu_s < 0.0)
			mu_s = 0.0;


		atmosphereConstants.g_height = height;
		atmosphereConstants.g_mu_s = mu_s;
		
		effect->SetConstantBuffer(deviceContext, &atmosphereConstants);		/*

		if (!texturesGenerated)
		{
			transmittanceTexture = renderPlatform->CreateTexture();
			directIrradianceTexture = renderPlatform->CreateTexture();
			singleScatteringTexture = renderPlatform->CreateTexture();
			multipleScatteringTexture = renderPlatform->CreateTexture();
			scatteringDensityTexture = renderPlatform->CreateTexture();
			transmittanceTexture->ensureTexture2DSizeAndFormat(renderPlatform, 256, 256, 1, crossplatform::PixelFormat::RGBA_32_FLOAT, false, true, false, 1, 0, false, vec4(0.0, 0.0, 0.0, 0.0));
			directIrradianceTexture->ensureTexture2DSizeAndFormat(renderPlatform, 256, 256, 1, crossplatform::PixelFormat::RGBA_32_FLOAT, false, true, false, 1, 0, false, vec4(0.0, 0.0, 0.0, 0.0));
			singleScatteringTexture->ensureTexture3DSizeAndFormat(renderPlatform, 256, 128, 32, crossplatform::PixelFormat::RGBA_32_FLOAT, true, 1, false);
			renderPlatform->ClearTexture(deviceContext, singleScatteringTexture, vec4(1.0, 0.0, 1.0, 0.0));
			multipleScatteringTexture->ensureTexture3DSizeAndFormat(renderPlatform, 256, 128, 32, crossplatform::PixelFormat::RGBA_32_FLOAT, true, 1, false);
			renderPlatform->ClearTexture(deviceContext, multipleScatteringTexture, vec4(0.0, 0.0, 0.0, 0.0));
			scatteringDensityTexture->ensureTexture3DSizeAndFormat(renderPlatform, 256, 128, 32, crossplatform::PixelFormat::RGBA_32_FLOAT, true, 1, false);
			renderPlatform->ClearTexture(deviceContext, scatteringDensityTexture, vec4(0.0, 0.0, 0.0, 0.0));

			//crossplatform::Effect* transmittance = renderPlatform->CreateEffect("atmospheric_transmittance");
			crossplatform::EffectTechnique* precompute_transmittance = transmittanceEffect->GetTechniqueByName("precompute_transmittance");
			crossplatform::EffectTechnique* precompute_direct_irradiance = scatteringEffect->GetTechniqueByName("precompute_direct_irradiance");
			crossplatform::EffectTechnique* precompute_single_scattering = scatteringEffect->GetTechniqueByName("precompute_single_scattering");
			crossplatform::EffectTechnique* precompute_scattering_density_texture = scatteringEffect->GetTechniqueByName("precompute_scattering_density_texture");
			crossplatform::EffectTechnique* precompute_multiple_scattering_texture = scatteringEffect->GetTechniqueByName("precompute_multiple_scattering_texture");

			atmosphereConstants.LinkToEffect(transmittanceEffect, "cbAtmosphere");
			atmosphereConstants.LinkToEffect(scatteringEffect, "cbAtmosphere");

			atmosphereConstants.g_topRadius = 6420000.0;
			atmosphereConstants.g_bottomRadius = 6360000.0;
			atmosphereConstants.g_mu_s_min = -0.2;

			atmosphereConstants.g_rayleighExpTerm = 1.f;
			atmosphereConstants.g_rayleighExpScale = -1.f / 8000.f;
			atmosphereConstants.g_rayleighLinearTerm = 0.f;
			atmosphereConstants.g_rayleighConstantTerm = 0.f;

			atmosphereConstants.g_rayleighScattering = vec3(rayleigh_approx(630), rayleigh_approx(550), rayleigh_approx(440)) * 0.001f;
			atmosphereConstants.g_rayleighDensity;

			atmosphereConstants.g_mieExpTerm = 1.f;
			atmosphereConstants.g_mieExpScale = -1.f / 1200.f;
			atmosphereConstants.g_mieLinearTerm = 0.f;
			atmosphereConstants.g_mieConstantTerm = 0.f;

			double haze = 1.f;
			double nu = 4.0;
			double T = (1.0 + haze);
			double c = (0.6544 * T - 0.6510) * 1e-16;
			if (haze > 1.0f)
				c /= haze;
			if (c < 0.0)
				c = 0.0;
			vec3 Mie;
			Mie.x = (float)(0.434 * c * SIMUL_PI_D * pow(2.0 * SIMUL_PI_D / (680.f * 1e-9), nu - 2) * 0.68455);
			Mie.y = (float)(0.434 * c * SIMUL_PI_D * pow(2.0 * SIMUL_PI_D / (550.f * 1e-9), nu - 2) * 0.673323);
			Mie.z = (float)(0.434 * c * SIMUL_PI_D * pow(2.0 * SIMUL_PI_D / (440.f * 1e-9), nu - 2) * 0.6691485);

			atmosphereConstants.g_mieScattering = vec3(mie_approx(630), mie_approx(550), mie_approx(440));
			atmosphereConstants.g_miePhaseFunction = 0.8f;

			atmosphereConstants.g_mieExtinction = Mie * 0.001f;

			atmosphereConstants.g_absorptionExpTerm = 0.f;
			atmosphereConstants.g_absorptionExpScale = 0.f;
			atmosphereConstants.g_absorptionLinearTerm = 1.f / 15000.f;
			atmosphereConstants.g_absorptionConstantTerm = -2.f / 3.f;

			atmosphereConstants.g_absorptionExtinction = (300.0 * 2.687e20 / 15000.0) * vec3(1.582000e-26, 3.500000e-25, 1.209000e-25);

			atmosphereConstants.g_solarIrradiance = 1.5;
			atmosphereConstants.g_groundAlbedo = 0.1;
			atmosphereConstants.g_scatteringOrder = 2;

			atmosphereConstants.g_mu_s = mu_s;
			atmosphereConstants.g_height = height;

			effect->SetConstantBuffer(deviceContext, &atmosphereConstants);

			transmittanceEffect->Apply(deviceContext, precompute_transmittance, 0);

			transmittanceTexture->activateRenderTarget(deviceContext);
			renderPlatform->DrawQuad(deviceContext);
			transmittanceTexture->deactivateRenderTarget(deviceContext);

			transmittanceEffect->Unapply(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_direct_irradiance, 0);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			directIrradianceTexture->activateRenderTarget(deviceContext);
			renderPlatform->DrawQuad(deviceContext);
			directIrradianceTexture->deactivateRenderTarget(deviceContext);

			scatteringEffect->Unapply(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_single_scattering, 0);
			scatteringEffect->SetUnorderedAccessView(deviceContext, "singleScatteringOutput", singleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			renderPlatform->DispatchCompute(deviceContext, 256, 128, 32);
			scatteringEffect->Unapply(deviceContext);
			scatteringEffect->UnbindTextures(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_scattering_density_texture, 0);
			scatteringEffect->SetUnorderedAccessView(deviceContext, "scatteringDensityOutput", scatteringDensityTexture);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			scatteringEffect->SetTexture(deviceContext, "g_DirectIrradiance", directIrradianceTexture);
			scatteringEffect->SetTexture(deviceContext, "g_singleScattering", singleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_multipleScattering", multipleScatteringTexture);
			renderPlatform->DispatchCompute(deviceContext, 256, 128, 32);
			scatteringEffect->Unapply(deviceContext);
			scatteringEffect->UnbindTextures(deviceContext);

			scatteringEffect->Apply(deviceContext, precompute_multiple_scattering_texture, 0);
			scatteringEffect->SetUnorderedAccessView(deviceContext, "multipleScatteringOutput", multipleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
			scatteringEffect->SetTexture(deviceContext, "g_singleScattering", singleScatteringTexture);
			scatteringEffect->SetTexture(deviceContext, "g_scatteringDensityTexture", scatteringDensityTexture);
			renderPlatform->DispatchCompute(deviceContext, 256, 128, 32);
			scatteringEffect->Unapply(deviceContext);
			scatteringEffect->UnbindTextures(deviceContext);

			texturesGenerated = true;
		}
		if (mu_s > 1.0)
			mu_s = 1.0;
		if (mu_s < 0.0)
			mu_s = 0.0;


		atmosphereConstants.g_height = height;
		atmosphereConstants.g_mu_s = mu_s;
		
		effect->SetConstantBuffer(deviceContext, &atmosphereConstants);		/*

		crossplatform::EffectTechnique* test_single_scattering = scatteringEffect->GetTechniqueByName("test_single_scattering_skybox");
		scatteringEffect->Apply(deviceContext, test_single_scattering, 0);
		scatteringEffect->SetTexture(deviceContext, "g_singleScattering", singleScatteringTexture);
		renderPlatform->Draw(deviceContext, 4, 0);
		scatteringEffect->Unapply(deviceContext);

		crossplatform::EffectTechnique* test_transmittance = transmittanceEffect->GetTechniqueByName("visualise_transmittance");
		transmittanceEffect->SetConstantBuffer(deviceContext, &atmosphereConstants);
		transmittanceEffect->Apply(deviceContext, test_transmittance, 0);
		transmittanceEffect->SetTexture(deviceContext, "g_Transmittance", transmittanceTexture);
		renderPlatform->Draw(deviceContext, 4, 0);
		transmittanceEffect->Unapply(deviceContext);
		

		crossplatform::EffectTechnique* test_multiple_scattering = scatteringEffect->GetTechniqueByName("test_multiple_scattering");
		scatteringEffect->Apply(deviceContext, test_multiple_scattering, 0);
		scatteringEffect->SetTexture(deviceContext, "g_multipleScattering", multipleScatteringTexture);
		renderPlatform->Draw(deviceContext, 4, 0);
		scatteringEffect->Unapply(deviceContext);
		*/
		renderPlatform->DrawTexture(deviceContext, 0, 0, w, h,directIrradianceTexture, 1.0f, false, 0.45f);

		//delete transmittanceTexture;
		//delete singleScatteringTexture;
	}
};
PlatformRenderer* platformRenderer;


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		/*case WM_MOUSEWHEEL:
			if (renderer)
			{
				int xPos = GET_X_LPARAM(lParam);
				int yPos = GET_Y_LPARAM(lParam);
				short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
				renderer->OnMouse((wParam & MK_LBUTTON) != 0
					, (wParam & MK_RBUTTON) != 0
					, (wParam & MK_MBUTTON) != 0
					, 0, xPos, yPos);
			}
			break;
		case WM_MOUSEMOVE:
			if (renderer)
			{
				int xPos = GET_X_LPARAM(lParam);
				int yPos = GET_Y_LPARAM(lParam);
				renderer->OnMouse((wParam & MK_LBUTTON) != 0
					, (wParam & MK_RBUTTON) != 0
					, (wParam & MK_MBUTTON) != 0
					, 0, xPos, yPos);
			}
			break;*/
		case WM_KEYDOWN:
			if (wParam == VK_DOWN)
			{
				mu_s -= 0.01;
				height -= 500;
			}
			else if (wParam == VK_UP)
			{
				mu_s += 0.01;
				height += 500;
			}
			break;
		/*case WM_COMMAND:
		{
	int wmId, wmEvent;
			wmId = LOWORD(wParam);
			wmEvent = HIWORD(wParam);
			// Parse the menu selections:
			//switch (wmId)
			return DefWindowProc(hWnd, message, wParam, lParam);
			break;
			}
		case WM_SIZE:
			if (renderer)
			{
				INT Width = LOWORD(lParam);
				INT Height = HIWORD(lParam);
				if (Width > 8192 || Height > 8192 || Width < 0 || Height < 0)
					break;
				displaySurfaceManager.ResizeSwapChain(hWnd);
			}
			break;*/
	case WM_PAINT:
		if (platformRenderer)
		{
			/*double fTime = 0.0;
			float time_step = 0.01f;
			renderer->OnFrameMove(fTime, time_step);*/
			displaySurfaceManager.Render(hWnd);
			displaySurfaceManager.EndFrame();
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}


int APIENTRY WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR    lpCmdLine,
	int       nCmdShow)
{
	wchar_t** szArgList;
	int argCount;
	szArgList = CommandLineToArgvW(GetCommandLineW(), &argCount);

#ifdef _MSC_VER
	// The following disables error dialogues in the case of a crash, this is so automated testing will not hang. See http://blogs.msdn.com/b/oldnewthing/archive/2004/07/27/198410.aspx
	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);
	// But that doesn't work sometimes, so:
	_set_abort_behavior(0, _WRITE_ABORT_MSG);
	_set_abort_behavior(0, _CALL_REPORTFAULT);
	// And still we might get "debug assertion failed" boxes. So we do this as well:
	_set_error_mode(_OUT_TO_STDERR);
#endif

	GetCommandLineParams(commandLineParams, argCount, (const wchar_t**)szArgList);
	if (commandLineParams.logfile_utf8.length())
		debug_buffer.setLogFile(commandLineParams.logfile_utf8.c_str());
	// Initialize the Window class:
	{
		WNDCLASSEXW wcex;
		wcex.cbSize = sizeof(WNDCLASSEXW);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		wcex.hIcon = 0;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = NULL;
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = wszWindowClass;
		wcex.hIconSm = 0;
		RegisterClassExW(&wcex);
	}
	// Create the window:
	{
		hInst = hInstance; // Store instance handle in our global variable
		hWnd = CreateWindowW(wszWindowClass, wszWindowClass, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, kOverrideWidth/*commandLineParams.win_w*/, kOverrideHeight/*commandLineParams.win_h*/, NULL, NULL, hInstance, NULL);
		if (!hWnd)
			return 0;
		ShowWindow(hWnd, nCmdShow);
		UpdateWindow(hWnd);
	}

	platformRenderer = new PlatformRenderer(crossplatform::RenderPlatformType::D3D12, TestType::EXTERNAL, commandLineParams("debug"));
	platformRenderer->OnCreateDevice();
	displaySurfaceManager.Initialize(platformRenderer->renderPlatform);
	displaySurfaceManager.SetRenderer(hWnd, platformRenderer, -1);

	// Main message loop:
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		InvalidateRect(hWnd, NULL, TRUE);
		//if (commandLineParams.quitafterframe > 0 && renderer->framenumber >= commandLineParams.quitafterframe)
			//break;
	}
	displaySurfaceManager.RemoveWindow(hWnd);
	displaySurfaceManager.Shutdown();
	platformRenderer->OnDeviceRemoved();
	delete platformRenderer;
	return 0;
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR    lpCmdLine,
	int       nCmdShow)
{
	return WinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}