// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.h: Scene rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RendererInterface.h"
#include "StaticBoundShaderState.h"
#include "ScenePrivateBase.h"
#include "LightSceneInfo.h"
#include "SceneRendering.h"
#include "DepthRendering.h"
#include "TranslucentRendering.h"
#include "ScreenSpaceDenoise.h"
#include "Lumen/LumenSceneCardCapture.h"
#include "Lumen/LumenTracingUtils.h"
#include "RayTracing/RayTracingLighting.h"
#include "IndirectLightRendering.h"
#include "ScreenSpaceRayTracing.h"
#include "RenderGraphUtils.h"
#include "SceneCulling/SceneCullingRenderer.h"

enum class ERayTracingPrimaryRaysFlag : uint32;

class FLumenCardUpdateContext;
class FSceneTextureParameters;
class FDistanceFieldCulledObjectBufferParameters;
class FTileIntersectionParameters;
class FDistanceFieldAOParameters;
class UStaticMeshComponent;
class FExponentialHeightFogSceneInfo;
class FRaytracingLightDataPacked;
class FLumenCardScatterContext;
class FRenderLightParameters;
class FRayTracingScene;
class FNaniteVisibility;
struct FNaniteVisibilityQuery;

struct FSceneWithoutWaterTextures;
struct FRayTracingReflectionOptions;
struct FHairStrandsTransmittanceMaskData;
struct FVolumetricFogLocalLightFunctionInfo;
struct FTranslucencyLightingVolumeTextures;
struct FLumenSceneFrameTemporaries;
struct FSingleLayerWaterPrePassResult;
struct FBuildHZBAsyncComputeParams;
struct FForwardBasePassTextures;
struct FTranslucentLightInjectionCollector;
struct FRayTracingPickingFeedback;
struct FDBufferTextures;
struct FILCUpdatePrimTaskData;
struct FLumenDirectLightingTaskData;

class IVisibilityTaskData;

#if RHI_RAYTRACING
struct FRayTracingRelevantPrimitiveTaskData;
#endif

/**
 * Encapsulates the resources and render targets used by global illumination plugins.
 */
class FGlobalIlluminationPluginResources : public FRenderResource

{
public:
	FRDGTextureRef GBufferA;
	FRDGTextureRef GBufferB;
	FRDGTextureRef GBufferC;
	FRDGTextureRef SceneDepthZ;
	FRDGTextureRef SceneColor;
	FRDGTextureRef LightingChannelsTexture;
};

/**
 * Delegate callback used by global illumination plugins
 */
class FGlobalIlluminationPluginDelegates
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FAnyRayTracingPassEnabled, bool& /*bAnyRayTracingPassEnabled*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FPrepareRayTracing, const FViewInfo& /*View*/, TArray<FRHIRayTracingShader*>& /*OutRayGenShaders*/);
	DECLARE_MULTICAST_DELEGATE_FourParams(FRenderDiffuseIndirectLight, const FScene& /*Scene*/, const FViewInfo& /*View*/, FRDGBuilder& /*GraphBuilder*/, FGlobalIlluminationPluginResources& /*Resources*/);

	static RENDERER_API FAnyRayTracingPassEnabled& AnyRayTracingPassEnabled();
	static RENDERER_API FPrepareRayTracing& PrepareRayTracing();
	static RENDERER_API FRenderDiffuseIndirectLight& RenderDiffuseIndirectLight();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	DECLARE_MULTICAST_DELEGATE_FourParams(FRenderDiffuseIndirectVisualizations, const FScene& /*Scene*/, const FViewInfo& /*View*/, FRDGBuilder& /*GraphBuilder*/, FGlobalIlluminationPluginResources& /*Resources*/);
	static RENDERER_API FRenderDiffuseIndirectVisualizations& RenderDiffuseIndirectVisualizations();
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
};

/**
 * Scene renderer that implements a deferred shading pipeline and associated features.
 */
class FDeferredShadingSceneRenderer : public FSceneRenderer
{
public:
	/** Defines which objects we want to render in the EarlyZPass. */
	FDepthPassInfo DepthPass;

	FSceneCullingRenderer SceneCullingRenderer;

#if RHI_RAYTRACING
	bool bShouldUpdateRayTracingScene =  false;

	void InitializeRayTracingFlags_RenderThread();
#endif

	enum class ERendererOutput
	{
		DepthPrepassOnly,	// Only render depth prepass and its related code paths
		FinalSceneColor		// Render the whole pipeline
	};

	FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer);

	/** Clears a view */
	void ClearView(FRHICommandListImmediate& RHICmdList);

	/**
	 * Renders the scene's prepass for a particular view
	 * @return true if anything was rendered
	 */
	void RenderPrePassView(FRHICommandList& RHICmdList, const FViewInfo& View);

	/**
	 * Renders the scene's prepass for a particular view in parallel
	 * @return true if the depth was cleared
	 */
	bool RenderPrePassViewParallel(const FViewInfo& View, FRHICommandListImmediate& ParentCmdList,TFunctionRef<void()> AfterTasksAreStarted, bool bDoPrePre);

	/** 
	 * Culls local lights and reflection probes to a grid in frustum space, builds one light list and grid per view in the current Views.  
	 * Needed for forward shading or translucency using the Surface lighting mode, and clustered deferred shading. 
	 */
	FComputeLightGridOutput GatherLightsAndComputeLightGrid(FRDGBuilder& GraphBuilder, bool bNeedLightGrid, FSortedLightSetSceneInfo &SortedLightSet);

	/** 
	 * Debug light grid content on screen.
	 */
	void DebugLightGrid(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTextures, bool bNeedLightGrid);

	void RenderBasePass(
		FRDGBuilder& GraphBuilder,
		FSceneTextures& SceneTextures,
		const FDBufferTextures& DBufferTextures,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		FRDGTextureRef ForwardShadowMaskTexture,
		FInstanceCullingManager& InstanceCullingManager,
		bool bNaniteEnabled,
		const TArrayView<Nanite::FRasterResults>& NaniteRasterResults);

	void RenderBasePassInternal(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FRenderTargetBindingSlots& BasePassRenderTargets,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		const FForwardBasePassTextures& ForwardBasePassTextures,
		const FDBufferTextures& DBufferTextures,
		bool bParallelBasePass,
		bool bRenderLightmapDensity,
		FInstanceCullingManager& InstanceCullingManager,
		bool bNaniteEnabled,
		const TArrayView<Nanite::FRasterResults>& NaniteRasterResults);

	void RenderAnisotropyPass(
		FRDGBuilder& GraphBuilder,
		FSceneTextures& SceneTextures,
		bool bDoParallelPass);
	/**
	 * Runs water pre-pass if enabled and returns an RDG-allocated object with intermediates, or null.
	 */
	FSingleLayerWaterPrePassResult* RenderSingleLayerWaterDepthPrepass(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures);

	void RenderSingleLayerWater(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult,
		bool bShouldRenderVolumetricCloud,
		FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
		FLumenSceneFrameTemporaries& LumenFrameTemporaries);

	void RenderSingleLayerWaterInner(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult);

	void RenderSingleLayerWaterReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult,
		FLumenSceneFrameTemporaries& LumenFrameTemporaries);

	void RenderOcclusion(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		bool bIsOcclusionTesting,
		const FBuildHZBAsyncComputeParams* BuildHZBAsyncComputeParams = nullptr);

	bool RenderHzb(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, const FBuildHZBAsyncComputeParams* AsyncComputeParams);

	/** Renders the view family. */
	virtual void Render(FRDGBuilder& GraphBuilder) override;

	/** Render the view family's hit proxies. */
	virtual void RenderHitProxies(FRDGBuilder& GraphBuilder) override;

	virtual bool ShouldRenderPrePass() const override;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void RenderVisualizeTexturePool(FRHICommandListImmediate& RHICmdList);
#endif

private:
	static FGlobalDynamicIndexBuffer DynamicIndexBufferForInitViews;
	static FGlobalDynamicIndexBuffer DynamicIndexBufferForInitShadows;
	static FGlobalDynamicVertexBuffer DynamicVertexBufferForInitViews;
	static FGlobalDynamicVertexBuffer DynamicVertexBufferForInitShadows;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBufferForInitViews;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBufferForInitShadows;

	FSeparateTranslucencyDimensions SeparateTranslucencyDimensions;

	/** Creates a per object projected shadow for the given interaction. */
	void CreatePerObjectProjectedShadow(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		bool bCreateTranslucentObjectShadow,
		bool bCreateInsetObjectShadow,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows);

	struct FInitViewTaskDatas
	{
		FInitViewTaskDatas(IVisibilityTaskData* InVisibilityTaskData)
			: VisibilityTaskData(InVisibilityTaskData)
		{}

		IVisibilityTaskData* VisibilityTaskData;
		FILCUpdatePrimTaskData* ILCUpdatePrim = nullptr;
	#if RHI_RAYTRACING
		FRayTracingRelevantPrimitiveTaskData* RayTracingRelevantPrimitives = nullptr;
	#endif
		FDynamicShadowsTaskData* DynamicShadows = nullptr;
		FLumenDirectLightingTaskData* LumenDirectLighting = nullptr;
		FLumenSceneFrameTemporaries* LumenFrameTemporaries = nullptr;
	};

	void PreVisibilityFrameSetup(FRDGBuilder& GraphBuilder);

	void BeginInitDynamicShadows(FInitViewTaskDatas& TaskDatas);
	void FinishInitDynamicShadows(FRDGBuilder& GraphBuilder, FDynamicShadowsTaskData*& TaskData, FInstanceCullingManager& InstanceCullingManager, FRDGExternalAccessQueue& ExternalAccessQueue);

	void ComputeLightVisibility();

	/** Determines which primitives are visible for each view. */
	void BeginInitViews(
		FRDGBuilder& GraphBuilder,
		const FSceneTexturesConfig& SceneTexturesConfig,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		FInstanceCullingManager& InstanceCullingManager,
		FVirtualTextureUpdater* VirtualTextureUpdater,
		FInitViewTaskDatas& TaskDatas);

	void EndInitViews(
		FRDGBuilder& GraphBuilder,
		FLumenSceneFrameTemporaries& FrameTemporaries,
		FInstanceCullingManager& InstanceCullingManager,
		FRDGExternalAccessQueue& ExternalAccessQueue,
		FInitViewTaskDatas& TaskDatas);

	void CreateIndirectCapsuleShadows();

	void RenderPrePass(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, FInstanceCullingManager& InstanceCullingManager, FRDGTextureRef* FirstStageDepthBuffer);
	void RenderPrePassHMD(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture);

	void RenderFog(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef LightShaftOcclusionTexture);

	void RenderUnderWaterFog(
		FRDGBuilder& GraphBuilder,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);

	void RenderAtmosphere(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef LightShaftOcclusionTexture);

	/** Renders sky lighting and reflections that can be done in a deferred pass. */
	void RenderDeferredReflectionsAndSkyLighting(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FLumenSceneFrameTemporaries& LumenFrameTemporaries,
		FRDGTextureRef DynamicBentNormalAOTexture);

	void RenderDeferredReflectionsAndSkyLightingHair(FRDGBuilder& GraphBuilder);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Renders debug visualizations for global illumination plugins. */
	void RenderGlobalIlluminationPluginVisualizations(FRDGBuilder& GraphBuilder, FRDGTextureRef LightingChannelsTexture);
#endif

	/** Computes DFAO, modulates it to scene color (which is assumed to contain diffuse indirect lighting), and stores the output bent normal for use occluding specular. */
	void RenderDFAOAsIndirectShadowing(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		FRDGTextureRef& DynamicBentNormalAO);

	bool ShouldRenderDistanceFieldLighting() const;

	/** Render Ambient Occlusion using mesh distance fields and the surface cache, which supports dynamic rigid meshes. */
	void RenderDistanceFieldLighting(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const class FDistanceFieldAOParameters& Parameters,
		FRDGTextureRef& OutDynamicBentNormalAO,
		bool bModulateToSceneColor,
		bool bVisualizeAmbientOcclusion);

	/** Render Ambient Occlusion using mesh distance fields on a screen based grid. */
	void RenderDistanceFieldAOScreenGrid(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FViewInfo& View,
		const FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
		FRDGBufferRef ObjectTilesIndirectArguments,
		const FTileIntersectionParameters& TileIntersectionParameters,
		const FDistanceFieldAOParameters& Parameters,
		FRDGTextureRef DistanceFieldNormal,
		FRDGTextureRef& OutDynamicBentNormalAO);

	void RenderMeshDistanceFieldVisualization(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FDistanceFieldAOParameters& Parameters);
	
	FFrontLayerTranslucencyData RenderFrontLayerTranslucency(
		FRDGBuilder& GraphBuilder,
		TArray<FViewInfo>& Views,
		const FSceneTextures& SceneTextures,
		bool bRenderOnlyForVSMPageMarking);

	bool IsLumenFrontLayerTranslucencyEnabled(const FViewInfo& View) const;

	void RenderLumenMiscVisualizations(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const FLumenSceneFrameTemporaries& FrameTemporaries);
	void RenderLumenRadianceCacheVisualization(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures);
	void RenderLumenRadiosityProbeVisualization(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const FLumenSceneFrameTemporaries& FrameTemporaries);
	void LumenScenePDIVisualization();
	
	/** 
	 * True if the 'r.UseClusteredDeferredShading' flag is 1 and sufficient feature level. 
	 */
	bool ShouldUseClusteredDeferredShading() const;

	/**
	 * Have the lights been injected into the light grid?
	 */
	bool AreLightsInLightGrid() const;


	/** Add a clustered deferred shading lighting render pass.	Note: in the future it should take the RenderGraph builder as argument */
	void AddClusteredDeferredShadingPass(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FSortedLightSetSceneInfo& SortedLightsSet,
		FRDGTextureRef ShadowMaskBits,
		FRDGTextureRef HairStrandsShadowMaskBits);

	/** Renders the scene's lighting. */
	void RenderLights(
		FRDGBuilder& GraphBuilder,
		FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		FRDGTextureRef LightingChannelsTexture,
		FSortedLightSetSceneInfo& SortedLightSet);

	/** Render stationary light overlap as complexity to scene color. */
	void RenderStationaryLightOverlap(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef LightingChannelsTexture);
	
	/** Renders the scene's translucency passes. */
	void RenderTranslucency(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		FTranslucencyPassResourcesMap* OutTranslucencyResourceMap,
		ETranslucencyView ViewsToRender,
		FInstanceCullingManager& InstanceCullingManager,
		bool bStandardTranslucentCanRenderSeparate);

	/** Renders the scene's translucency given a specific pass. */
	void RenderTranslucencyInner(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		FTranslucencyPassResourcesMap* OutTranslucencyResourceMap,
		FRDGTextureMSAA SharedDepthTexture,
		ETranslucencyView ViewsToRender,
		FRDGTextureRef SceneColorCopyTexture,
		ETranslucencyPass::Type TranslucencyPass,
		FInstanceCullingManager& InstanceCullingManager,
		bool bStandardTranslucentCanRenderSeparate);

	/** Renders the scene's light shafts */
	FRDGTextureRef RenderLightShaftOcclusion(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures);

	void RenderLightShaftBloom(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FTranslucencyPassResourcesMap& OutTranslucencyResourceMap);

	bool ShouldRenderDistortion() const;
	void RenderDistortion(FRDGBuilder& GraphBuilder, 
		FRDGTextureRef SceneColorTexture, 
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef SceneVelocityTexture,
		FTranslucencyPassResourcesMap& TranslucencyResourceMap);

	void CollectLightForTranslucencyLightingVolumeInjection(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		const FLightSceneInfo* LightSceneInfo,
		bool bSupportShadowMaps,
		FTranslucentLightInjectionCollector& Collector);

	/** Renders capsule shadows for all per-object shadows using it for the given light. */
	bool RenderCapsuleDirectShadows(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo& LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		TArrayView<const FProjectedShadowInfo* const> CapsuleShadows,
		bool bProjectingForForwardShading) const;

	/** Renders indirect shadows from capsules modulated onto scene color. */
	void RenderIndirectCapsuleShadows(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures) const;

	void RenderVirtualShadowMapProjections(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		const FLightSceneInfo* LightSceneInfo);

	/** Renders capsule shadows for movable skylights, using the cone of visibility (bent normal) from DFAO. */
	void RenderCapsuleShadowsForMovableSkylight(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef& BentNormalOutput) const;

	void RenderDeferredShadowProjections(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture);

	void RenderForwardShadowProjections(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef& ForwardScreenSpaceShadowMask,
		FRDGTextureRef& ForwardScreenSpaceShadowMaskSubPixel);

	/** Used by RenderLights to render a light function to the attenuation buffer. */
	bool RenderLightFunction(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		bool bLightAttenuationCleared,
		bool bProjectingForForwardShading, 
		bool bUseHairStrands);

	/** Renders a light function indicating that whole scene shadowing being displayed is for previewing only, and will go away in game. */
	bool RenderPreviewShadowsIndicator(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		bool bLightAttenuationCleared,
		bool bUseHairStrands);

	/** Renders a light function with the given material. */
	bool RenderLightFunctionForMaterial(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		const FMaterialRenderProxy* MaterialProxy,
		bool bLightAttenuationCleared,
		bool bProjectingForForwardShading,
		bool bRenderingPreviewShadowsIndicator, 
		bool bUseHairStrands);

	void RenderLightsForHair(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FSortedLightSetSceneInfo& SortedLightSet,
		FRDGTextureRef InScreenShadowMaskSubPixelTexture,
		FRDGTextureRef LightingChannelsTexture);

	/** Specialized version of RenderLight for hair (run lighting evaluation on at sub-pixel rate, without depth bound) */
	void RenderLightForHair(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		const FMinimalSceneTextures& SceneTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		FRDGTextureRef LightingChannelsTexture,
		const FHairStrandsTransmittanceMaskData& InTransmittanceMaskData,
		const bool bForwardRendering);

	/** Renders an array of simple lights using standard deferred shading. */
	void RenderSimpleLightsStandardDeferred(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FSimpleLightArray& SimpleLights);

	void RenderHeterogeneousVolumes(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures);
	void CompositeHeterogeneousVolumes(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures);

	void VisualizeVolumetricLightmap(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures);

	/** Render image based reflections (SSR, Env, SkyLight) without compute shaders */
	void RenderStandardDeferredImageBasedReflections(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, bool bReflectionEnv, const TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO, TRefCountPtr<IPooledRenderTarget>& VelocityRT);

	void RenderDeferredPlanarReflections(FRDGBuilder& GraphBuilder, const FSceneTextureParameters& SceneTextures, const FViewInfo& View, FRDGTextureRef& ReflectionsOutput);


	bool IsNaniteEnabled() const;


	void SetupImaginaryReflectionTextureParameters(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FSceneTextureParameters* OutTextures);

	void RenderRayTracingReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FViewInfo& View,
		int DenoiserMode,
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderRayTracingDeferredReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		int DenoiserMode,
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderDitheredLODFadingOutMask(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneDepthTexture);

	void RenderRayTracingShadows(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		const FLightSceneInfo& LightSceneInfo,
		const IScreenSpaceDenoiser::FShadowRayTracingConfig& RayTracingConfig,
		const IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements,
		FRDGTextureRef LightingChannelsTexture,
		FRDGTextureUAV* OutShadowMaskUAV,
		FRDGTextureUAV* OutRayHitDistanceUAV,
		FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV);
	void CompositeRayTracingSkyLight(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef SkyLightRT,
		FRDGTextureRef HitDistanceRT);
	
	bool RenderRayTracingGlobalIllumination(
		FRDGBuilder& GraphBuilder, 
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig* RayTracingConfig,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs) override;
	
	void RenderRayTracingGlobalIlluminationBruteForce(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
		int32 UpscaleFactor,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);

	void RayTracingGlobalIlluminationCreateGatherPoints(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		int32 UpscaleFactor,
		int32 SampleIndex,
		FRDGBufferRef& GatherPointsBuffer,
		FIntVector& GatherPointsResolution);

	void RenderRayTracingGlobalIlluminationFinalGather(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
		int32 UpscaleFactor,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);
	
	void RenderRayTracingAmbientOcclusion(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		const FSceneTextureParameters& SceneTextures,
		FRDGTextureRef* OutAmbientOcclusionTexture) override;
	
	ERendererOutput GetRendererOutput() const;

#if RHI_RAYTRACING
	template <int TextureImportanceSampling>
	void RenderRayTracingRectLightInternal(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const TArray<FViewInfo>& Views,
		const FLightSceneInfo& RectLightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef RayDistanceTexture);

	void VisualizeRectLightMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FRWBuffer& RectLightMipTree, const FIntVector& RectLightMipTreeDimensions);

	void VisualizeSkyLightMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const TRefCountPtr<IPooledRenderTarget>& SceneColor, FRWBuffer& SkyLightMipTreePosX, FRWBuffer& SkyLightMipTreePosY, FRWBuffer& SkyLightMipTreePosZ, FRWBuffer& SkyLightMipTreeNegX, FRWBuffer& SkyLightMipTreeNegY, FRWBuffer& SkyLightMipTreeNegZ, const FIntVector& SkyLightMipDimensions);

	void RenderRayTracingSkyLight(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef& OutSkyLightTexture,
		FRDGTextureRef& OutHitDistanceTexture);

	void RenderRayTracingPrimaryRaysView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef* InOutColorTexture,
		FRDGTextureRef* InOutRayHitDistanceTexture,
		int32 SamplePerPixel,
		int32 HeightFog,
		float ResolutionFraction,
		ERayTracingPrimaryRaysFlag Flags);

	void RenderRayTracingTranslucency(FRDGBuilder& GraphBuilder, FRDGTextureMSAA SceneColorTexture);

	void RenderRayTracingTranslucencyView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef* OutColorTexture,
		FRDGTextureRef* OutRayHitDistanceTexture,
		int32 SamplePerPixel,
		int32 HeightFog,
		float ResolutionFraction);

	/** Setup the default miss shader (required for any raytracing pipeline) */
	void SetupRayTracingDefaultMissShader(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void SetupPathTracingDefaultMissShader(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Lighting Evaluation shader setup (used by ray traced reflections and translucency) */
	void SetupRayTracingLightingMissShader(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Path tracing functions. */
	void RenderPathTracing(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneColorOutputTexture,
		FRDGTextureRef SceneDepthOutputTexture,
		struct FPathTracingResources& PathTracingResources);

	void ComputePathCompaction(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHITexture* RadianceTexture, FRHITexture* SampleCountTexture, FRHITexture* PixelPositionTexture,
		FRHIUnorderedAccessView* RadianceSortedRedUAV, FRHIUnorderedAccessView* RadianceSortedGreenUAV, FRHIUnorderedAccessView* RadianceSortedBlueUAV, FRHIUnorderedAccessView* RadianceSortedAlphaUAV, FRHIUnorderedAccessView* SampleCountSortedUAV);

	void WaitForRayTracingScene(FRDGBuilder& GraphBuilder, FRDGBufferRef DynamicGeometryScratchBuffer);

	/** Debug ray tracing functions. */
	void RenderRayTracingDebug(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorOutputTexture, FRayTracingPickingFeedback& PickingFeedback);
	void RenderRayTracingBarycentrics(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorOutputTexture, bool bVisualizeProceduralPrimitives);
	void RayTracingDisplayPicking(const FRayTracingPickingFeedback& PickingFeedback, FScreenMessageWriter& Writer);

	/** Fills RayTracingScene instance list for the given View and adds relevant ray tracing data to the view. Does not reset previous scene contents. */
	bool GatherRayTracingWorldInstancesForView(FRDGBuilder& GraphBuilder, FViewInfo& View, FRayTracingScene& RayTracingScene, FRayTracingRelevantPrimitiveTaskData* RayTracingRelevantPrimitiveTaskData);

	bool SetupRayTracingPipelineStates(FRDGBuilder& GraphBuilder);
	void SetupRayTracingLightDataForViews(FRDGBuilder& GraphBuilder);
	bool DispatchRayTracingWorldUpdates(FRDGBuilder& GraphBuilder, FRDGBufferRef& OutDynamicGeometryScratchBuffer);

	/** Functions to create ray tracing pipeline state objects for various effects */
	FRayTracingPipelineState* CreateRayTracingMaterialPipeline(FRDGBuilder& GraphBuilder, FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable);
	FRayTracingPipelineState* CreateRayTracingDeferredMaterialGatherPipeline(FRHICommandList& RHICmdList, const FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable);
	FRayTracingPipelineState* CreateLumenHardwareRayTracingMaterialPipeline(FRHICommandList& RHICmdList, const FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable);

	/** Functions to bind parameters to the ray tracing scene (fill the shader binding tables, etc.) */
	void BindRayTracingMaterialPipeline(FRHICommandListImmediate& RHICmdList, FViewInfo& View, FRayTracingPipelineState* PipelineState);
	void BindRayTracingDeferredMaterialGatherPipeline(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRayTracingPipelineState* PipelineState);
	void BindLumenHardwareRayTracingMaterialPipeline(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHIUniformBuffer* SceneUniformBuffer, FRayTracingPipelineState* PipelineState);

	void BuildLumenHardwareRayTracingHitGroupData(FRHICommandListBase& RHICmdList, FRayTracingScene& RayTracingScene, const FViewInfo& View, FRDGBufferRef OutHitGroupDataBuffer);
	FRayTracingLocalShaderBindings* BuildLumenHardwareRayTracingMaterialBindings(FRHICommandList& RHICmdList, const FViewInfo& View, FRHIUniformBuffer* SceneUniformBuffer);
	void SetupLumenHardwareRayTracingHitGroupBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View);
	void SetupLumenHardwareRayTracingUniformBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View);
	// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. Register each effect at startup and just loop over them automatically
	static void PrepareRayTracingReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDeferredReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareSingleLayerWaterRayTracingReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingShadows(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingAmbientOcclusion(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingSkyLight(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIllumination(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIlluminationPlugin(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingTranslucency(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingVolumetricFogShadows(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDebug(const FSceneViewFamily& ViewFamily, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PreparePathTracing(const FSceneViewFamily& ViewFamily, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingScreenProbeGather(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingShortRangeAO(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingScreenProbeGatherDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingRadianceCache(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingRadianceCacheDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	void PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	void PrepareLumenHardwareRayTracingVisualize(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingVisualizeDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);

	// Versions for setting up the deferred material pipeline
	static void PrepareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDeferredReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIlluminationDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);

	// Versions for setting up the lumen material pipeline
	static void PrepareLumenHardwareRayTracingTranslucencyVolumeLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	void PrepareLumenHardwareRayTracingVisualizeLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	void PrepareLumenHardwareRayTracingReflectionsLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingScreenProbeGatherLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingRadiosityLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
#endif // RHI_RAYTRACING


	struct FNaniteBasePassVisibility
	{
		FNaniteVisibilityQuery* Query = nullptr;
		FNaniteVisibility* Visibility = nullptr;

	} NaniteBasePassVisibility;

	/** Set to true if lights were injected into the light grid (this controlled by somewhat complex logic, this flag is used to cross-check). */
	bool bAreLightsInLightGrid;
};

DECLARE_CYCLE_STAT_EXTERN(TEXT("PrePass"), STAT_CLM_PrePass, STATGROUP_CommandListMarkers, );
