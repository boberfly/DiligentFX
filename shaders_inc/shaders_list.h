static const MemoryShaderSourceFileInfo g_Shaders[] =
{
    {
        "ComputeBlueNoiseTexture.fx",
        #include "ComputeBlueNoiseTexture.fx.h"
    },
    {
        "CopyTextureDepth.fx",
        #include "CopyTextureDepth.fx.h"
    },
    {
        "EnvMap.psh",
        #include "EnvMap.psh.h"
    },
    {
        "EnvMap.vsh",
        #include "EnvMap.vsh.h"
    },
    {
        "FullScreenTriangleVS.fx",
        #include "FullScreenTriangleVS.fx.h"
    },
    {
        "FullScreenTriangleVSOutput.fxh",
        #include "FullScreenTriangleVSOutput.fxh.h"
    },
    {
        "VectorField.vsh",
        #include "VectorField.vsh.h"
    },
    {
        "AtlasSampling.fxh",
        #include "AtlasSampling.fxh.h"
    },
    {
        "BasicStructures.fxh",
        #include "BasicStructures.fxh.h"
    },
    {
        "PBR_Common.fxh",
        #include "PBR_Common.fxh.h"
    },
    {
        "PostFX_Common.fxh",
        #include "PostFX_Common.fxh.h"
    },
    {
        "SRGBUtilities.fxh",
        #include "SRGBUtilities.fxh.h"
    },
    {
        "ShaderUtilities.fxh",
        #include "ShaderUtilities.fxh.h"
    },
    {
        "Shadows.fxh",
        #include "Shadows.fxh.h"
    },
    {
        "ComputeIrradianceMap.psh",
        #include "ComputeIrradianceMap.psh.h"
    },
    {
        "CubemapFace.vsh",
        #include "CubemapFace.vsh.h"
    },
    {
        "Iridescence.fxh",
        #include "Iridescence.fxh.h"
    },
    {
        "PBR_PrecomputeCommon.fxh",
        #include "PBR_PrecomputeCommon.fxh.h"
    },
    {
        "PBR_Textures.fxh",
        #include "PBR_Textures.fxh.h"
    },
    {
        "PrecomputeBRDF.psh",
        #include "PrecomputeBRDF.psh.h"
    },
    {
        "PrefilterEnvMap.psh",
        #include "PrefilterEnvMap.psh.h"
    },
    {
        "RenderPBR.psh",
        #include "RenderPBR.psh.h"
    },
    {
        "RenderPBR.vsh",
        #include "RenderPBR.vsh.h"
    },
    {
        "RenderPBR_Structures.fxh",
        #include "RenderPBR_Structures.fxh.h"
    },
    {
        "RenderUnshaded.psh",
        #include "RenderUnshaded.psh.h"
    },
    {
        "PBR_Shading.fxh",
        #include "PBR_Shading.fxh.h"
    },
    {
        "PBR_Structures.fxh",
        #include "PBR_Structures.fxh.h"
    },
    {
        "VertexProcessing.fxh",
        #include "VertexProcessing.fxh.h"
    },
    {
        "AtmosphereShadersCommon.fxh",
        #include "AtmosphereShadersCommon.fxh.h"
    },
    {
        "CoarseInsctr.fx",
        #include "CoarseInsctr.fx.h"
    },
    {
        "ComputeMinMaxShadowMapLevel.fx",
        #include "ComputeMinMaxShadowMapLevel.fx.h"
    },
    {
        "Extinction.fxh",
        #include "Extinction.fxh.h"
    },
    {
        "InitializeMinMaxShadowMap.fx",
        #include "InitializeMinMaxShadowMap.fx.h"
    },
    {
        "InterpolateIrradiance.fx",
        #include "InterpolateIrradiance.fx.h"
    },
    {
        "LookUpTables.fxh",
        #include "LookUpTables.fxh.h"
    },
    {
        "MarkRayMarchingSamples.fx",
        #include "MarkRayMarchingSamples.fx.h"
    },
    {
        "RayMarch.fx",
        #include "RayMarch.fx.h"
    },
    {
        "ReconstructCameraSpaceZ.fx",
        #include "ReconstructCameraSpaceZ.fx.h"
    },
    {
        "RefineSampleLocations.fx",
        #include "RefineSampleLocations.fx.h"
    },
    {
        "RenderCoordinateTexture.fx",
        #include "RenderCoordinateTexture.fx.h"
    },
    {
        "RenderSampling.fx",
        #include "RenderSampling.fx.h"
    },
    {
        "RenderSliceEndPoints.fx",
        #include "RenderSliceEndPoints.fx.h"
    },
    {
        "ScatteringIntegrals.fxh",
        #include "ScatteringIntegrals.fxh.h"
    },
    {
        "SliceUVDirection.fx",
        #include "SliceUVDirection.fx.h"
    },
    {
        "Sun.fx",
        #include "Sun.fx.h"
    },
    {
        "UnshadowedScattering.fxh",
        #include "UnshadowedScattering.fxh.h"
    },
    {
        "UnwarpEpipolarScattering.fx",
        #include "UnwarpEpipolarScattering.fx.h"
    },
    {
        "UpdateAverageLuminance.fx",
        #include "UpdateAverageLuminance.fx.h"
    },
    {
        "CombineScatteringOrders.fx",
        #include "CombineScatteringOrders.fx.h"
    },
    {
        "ComputeScatteringOrder.fx",
        #include "ComputeScatteringOrder.fx.h"
    },
    {
        "ComputeSctrRadiance.fx",
        #include "ComputeSctrRadiance.fx.h"
    },
    {
        "InitHighOrderScattering.fx",
        #include "InitHighOrderScattering.fx.h"
    },
    {
        "PrecomputeAmbientSkyLight.fx",
        #include "PrecomputeAmbientSkyLight.fx.h"
    },
    {
        "PrecomputeCommon.fxh",
        #include "PrecomputeCommon.fxh.h"
    },
    {
        "PrecomputeNetDensityToAtmTop.fx",
        #include "PrecomputeNetDensityToAtmTop.fx.h"
    },
    {
        "PrecomputeSingleScattering.fx",
        #include "PrecomputeSingleScattering.fx.h"
    },
    {
        "UpdateHighOrderScattering.fx",
        #include "UpdateHighOrderScattering.fx.h"
    },
    {
        "EpipolarLightScatteringFunctions.fxh",
        #include "EpipolarLightScatteringFunctions.fxh.h"
    },
    {
        "EpipolarLightScatteringStructures.fxh",
        #include "EpipolarLightScatteringStructures.fxh.h"
    },
    {
        "ComputeBilateralCleanup.fx",
        #include "ComputeBilateralCleanup.fx.h"
    },
    {
        "ComputeHierarchicalDepthBuffer.fx",
        #include "ComputeHierarchicalDepthBuffer.fx.h"
    },
    {
        "ComputeIntersection.fx",
        #include "ComputeIntersection.fx.h"
    },
    {
        "ComputeSpatialReconstruction.fx",
        #include "ComputeSpatialReconstruction.fx.h"
    },
    {
        "ComputeStencilMaskAndExtractRoughness.fx",
        #include "ComputeStencilMaskAndExtractRoughness.fx.h"
    },
    {
        "ComputeTemporalAccumulation.fx",
        #include "ComputeTemporalAccumulation.fx.h"
    },
    {
        "SSR_Common.fxh",
        #include "SSR_Common.fxh.h"
    },
    {
        "ScreenSpaceReflectionStructures.fxh",
        #include "ScreenSpaceReflectionStructures.fxh.h"
    },
    {
        "ToneMapping.fxh",
        #include "ToneMapping.fxh.h"
    },
    {
        "ToneMappingStructures.fxh",
        #include "ToneMappingStructures.fxh.h"
    },
    {
        "ShadowConversions.fx",
        #include "ShadowConversions.fx.h"
    },
};
