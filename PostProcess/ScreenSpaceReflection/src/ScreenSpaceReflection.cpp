/*
 *  Copyright 2023-2024 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "ScreenSpaceReflection.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"
#include "ShaderMacroHelper.hpp"
#include "GraphicsTypesX.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"
}

static DILIGENT_CONSTEXPR DepthStencilStateDesc DSS_StencilWrite{
    False,                // DepthEnable
    False,                // DepthWriteEnable
    COMPARISON_FUNC_LESS, // DepthFunc
    True,                 // StencilEnable
    0xFF,                 // StencilReadMask
    0xFF,                 // StencilWriteMask
    {
        STENCIL_OP_KEEP,       // StencilFailOp
        STENCIL_OP_KEEP,       // StencilDepthFailOp
        STENCIL_OP_REPLACE,    // StencilPassOp
        COMPARISON_FUNC_ALWAYS // StencilFunc
    },
};

static DILIGENT_CONSTEXPR DepthStencilStateDesc DSS_StencilReadComparisonEqual{
    False,                // DepthEnable
    False,                // DepthWriteEnable
    COMPARISON_FUNC_LESS, // DepthFunc
    True,                 // StencilEnable
    0xFF,                 // StencilReadMask
    0xFF,                 // StencilWriteMask
    {
        STENCIL_OP_KEEP,      // StencilFailOp
        STENCIL_OP_KEEP,      // StencilDepthFailOp
        STENCIL_OP_KEEP,      // StencilPassOp
        COMPARISON_FUNC_EQUAL // StencilFunc
    },
};

ScreenSpaceReflection::ScreenSpaceReflection(IRenderDevice* pDevice)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    const auto& DeviceInfo = pDevice->GetDeviceInfo();

    m_SupportedFeatures.TransitionSubresources  = DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D12 || DeviceInfo.Type == RENDER_DEVICE_TYPE_VULKAN;
    m_SupportedFeatures.TextureSubresourceViews = DeviceInfo.Features.TextureSubresourceViews;
    m_SupportedFeatures.CopyDepthToColor        = DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D12 || DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D11;

    {
        RefCntAutoPtr<IBuffer> pBuffer;
        CreateUniformBuffer(pDevice, sizeof(HLSL::ScreenSpaceReflectionAttribs), "ScreenSpaceReflection::ConstantBuffer", &pBuffer);
        m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER, pBuffer);
    }

    if (!m_SupportedFeatures.TextureSubresourceViews)
    {
        RefCntAutoPtr<IBuffer> pBuffer;
        CreateUniformBuffer(pDevice, sizeof(Uint32), "ScreenSpaceReflection::IntermediateConstantBuffer", &pBuffer);
        m_Resources.Insert(RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE, pBuffer);
    }
}

ScreenSpaceReflection::~ScreenSpaceReflection() = default;

void ScreenSpaceReflection::SetBackBufferSize(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, Uint32 BackBufferWidth, Uint32 BackBufferHeight)
{
    DEV_CHECK_ERR(pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");

    if (m_BackBufferWidth == BackBufferWidth && m_BackBufferHeight == BackBufferHeight)
        return;

    m_BackBufferWidth  = BackBufferWidth;
    m_BackBufferHeight = BackBufferHeight;

    RenderDeviceWithCache_N Device{pDevice};

    constexpr Uint32 DepthHierarchyMipCount = SSR_DEPTH_HIERARCHY_MAX_MIP + 1;
    {
        m_HierarchicalDepthMipMapRTV.clear();
        m_HierarchicalDepthMipMapSRV.clear();

        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthHierarchy";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(BackBufferWidth, BackBufferHeight), DepthHierarchyMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_HIERARCHY, Device.CreateTexture(Desc));

        m_HierarchicalDepthMipMapSRV.resize(Desc.MipLevels);
        m_HierarchicalDepthMipMapRTV.resize(Desc.MipLevels);

        for (Uint32 MipLevel = 0; MipLevel < Desc.MipLevels; MipLevel++)
        {
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_RENDER_TARGET;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].AsTexture()->CreateView(ViewDesc, &m_HierarchicalDepthMipMapRTV[MipLevel]);
            }

            if (m_SupportedFeatures.TextureSubresourceViews)
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_SHADER_RESOURCE;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].AsTexture()->CreateView(ViewDesc, &m_HierarchicalDepthMipMapSRV[MipLevel]);
            }
        }
    }

    if (!m_SupportedFeatures.TextureSubresourceViews)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthHierarchyIntermediate";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(BackBufferWidth, BackBufferHeight), DepthHierarchyMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::Roughness";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R8_UNORM;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_ROUGHNESS, Device.CreateTexture(Desc));
    }

    {
        m_DepthStencilMaskDSVReadOnly.Release();

        TEXTURE_FORMAT DepthStencilFormat = TEX_FORMAT_D32_FLOAT_S8X24_UINT;

        TextureFormatInfoExt FormatInfo = pDevice->GetTextureFormatInfoExt(TEX_FORMAT_D24_UNORM_S8_UINT);
        if (FormatInfo.Supported && FormatInfo.BindFlags & BIND_DEPTH_STENCIL)
            DepthStencilFormat = TEX_FORMAT_D24_UNORM_S8_UINT;

        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthStencilMask";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = DepthStencilFormat;
        Desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK, Device.CreateTexture(Desc));

        TextureViewDesc ViewDesc;
        ViewDesc.ViewType = TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
        m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].AsTexture()->CreateView(ViewDesc, &m_DepthStencilMaskDSVReadOnly);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::Radiance";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_RADIANCE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::RayDirectionPDF";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ResolvedRadiance";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_RESOLVED_RADIANCE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ResolvedVariance";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_RESOLVED_VARIANCE, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ResolvedDepth";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_RESOLVED_DEPTH, Device.CreateTexture(Desc));
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_RADIANCE_HISTORY0; TextureIdx <= RESOURCE_IDENTIFIER_RADIANCE_HISTORY1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::RadianceHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_VARIANCE_HISTORY0; TextureIdx <= RESOURCE_IDENTIFIER_VARIANCE_HISTORY1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::VarianceHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(TextureIdx, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_DEPTH_HISTORY, Device.CreateTexture(Desc));
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::Output";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        m_Resources.Insert(RESOURCE_IDENTIFIER_OUTPUT, Device.CreateTexture(Desc));
    }
}

void ScreenSpaceReflection::Execute(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");
    DEV_CHECK_ERR(RenderAttribs.pPostFXContext != nullptr, "RenderAttribs.pPostFXContext must not be null");

    DEV_CHECK_ERR(RenderAttribs.pColorBufferSRV != nullptr, "RenderAttribs.pColorBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDepthBufferSRV != nullptr, "RenderAttribs.pDepthBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pNormalBufferSRV != nullptr, "RenderAttribs.pNormalBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pMaterialBufferSRV != nullptr, "RenderAttribs.pMaterialBufferSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pMotionVectorsSRV != nullptr, "RenderAttribs.pMotionVectorsSRV must not be null");
    DEV_CHECK_ERR(RenderAttribs.pSSRAttribs != nullptr, "RenderAttribs.pSSRAttribs must not be null");

    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_COLOR, RenderAttribs.pColorBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_DEPTH, RenderAttribs.pDepthBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_NORMAL, RenderAttribs.pNormalBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_MATERIAL_PARAMETERS, RenderAttribs.pMaterialBufferSRV->GetTexture());
    m_Resources.Insert(RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS, RenderAttribs.pMotionVectorsSRV->GetTexture());

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "ScreenSpaceReflection"};
    {
        MapHelper<HLSL::ScreenSpaceReflectionAttribs> SSRAttibs{RenderAttribs.pDeviceContext, m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER], MAP_WRITE, MAP_FLAG_DISCARD};
        *SSRAttibs = *RenderAttribs.pSSRAttribs;
    }

    ComputeHierarchicalDepthBuffer(RenderAttribs);
    ComputeStencilMaskAndExtractRoughness(RenderAttribs);
    ComputeIntersection(RenderAttribs);
    ComputeSpatialReconstruction(RenderAttribs);
    ComputeTemporalAccumulation(RenderAttribs);
    ComputeBilateralCleanup(RenderAttribs);
}

ITextureView* ScreenSpaceReflection::GetSSRRadianceSRV() const
{
    return m_Resources[RESOURCE_IDENTIFIER_OUTPUT].GetTextureSRV();
}

void ScreenSpaceReflection::CopyTextureDepth(const RenderAttributes& RenderAttribs, ITextureView* pSRV, ITextureView* pRTV)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COPY_DEPTH];
    if (!RenderTech.IsInitialized())
    {
        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "CopyTextureDepth.fx", "CopyDepthPS", SHADER_TYPE_PIXEL);

        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 nullptr, "ScreenSpaceReflection::CopyDepth",
                                 VS, PS, ResourceLayout,
                                 {
                                     pRTV->GetTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
        RenderTech.InitializeSRB(false);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(pSRV);

    RenderAttribs.pDeviceContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}


void ScreenSpaceReflection::ComputeHierarchicalDepthBuffer(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_HIERARCHICAL_DEPTH_BUFFER];
    if (!RenderTech.IsInitialized())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SUPPORTED_SHADER_SRV", m_SupportedFeatures.TextureSubresourceViews ? 1 : 0);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeHierarchicalDepthBuffer.fx", "ComputeHierarchicalDepthBufferPS", SHADER_TYPE_PIXEL, Macros);

        PipelineResourceLayoutDescX ResourceLayout;
        if (m_SupportedFeatures.TextureSubresourceViews)
        {
            ResourceLayout.AddVariable(SHADER_TYPE_PIXEL, "g_TextureLastMip", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
        }
        else
        {
            ResourceLayout
                .AddVariable(SHADER_TYPE_PIXEL, "cbTextureMipAtrrib", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMips", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                // Immutable samplers are required for WebGL to work properly
                .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureMips", Sam_PointWrap);
        }

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeHierarchicalDepthBuffer",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].AsTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
        RenderTech.InitializeSRB(false);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeHierarchicalDepthBuffer"};

    if (m_SupportedFeatures.CopyDepthToColor)
    {
        CopyTextureAttribs CopyAttribs;
        CopyAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH];
        CopyAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY];
        CopyAttribs.SrcMipLevel              = 0;
        CopyAttribs.DstMipLevel              = 0;
        CopyAttribs.SrcSlice                 = 0;
        CopyAttribs.DstSlice                 = 0;
        CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribs);
    }
    else
    {
        CopyTextureDepth(RenderAttribs, m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV(), m_HierarchicalDepthMipMapRTV[0]);

        if (!m_SupportedFeatures.TextureSubresourceViews)
        {
            CopyTextureAttribs CopyMipAttribs;
            CopyMipAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY];
            CopyMipAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE];
            CopyMipAttribs.SrcMipLevel              = 0;
            CopyMipAttribs.DstMipLevel              = 0;
            CopyMipAttribs.SrcSlice                 = 0;
            CopyMipAttribs.DstSlice                 = 0;
            CopyMipAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            CopyMipAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            RenderAttribs.pDeviceContext->CopyTexture(CopyMipAttribs);
        }
    }


    if (m_SupportedFeatures.TransitionSubresources)
    {
        StateTransitionDesc TransitionDescW2W[] = {
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].AsTexture(),
                                RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET,
                                STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2W), TransitionDescW2W);

        ShaderResourceVariableX TextureLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureLastMip"};
        for (Uint32 MipLevel = 1; MipLevel < m_HierarchicalDepthMipMapRTV.size(); MipLevel++)
        {
            StateTransitionDesc TranslationW2R[] = {
                StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].AsTexture(),
                                    RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                    MipLevel - 1, 1, 0, REMAINING_ARRAY_SLICES,
                                    STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_NONE},
            };

            TextureLastMipSV.Set(m_HierarchicalDepthMipMapSRV[MipLevel - 1]);
            RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TranslationW2R), TranslationW2R);
            RenderAttribs.pDeviceContext->SetRenderTargets(1, &m_HierarchicalDepthMipMapRTV[MipLevel], nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
            RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
        }

        StateTransitionDesc TransitionDescW2R[] = {
            StateTransitionDesc{m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].AsTexture(),
                                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                static_cast<Uint32>(m_HierarchicalDepthMipMapRTV.size() - 1), 1, 0, REMAINING_ARRAY_SLICES,
                                STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2R), TransitionDescW2R);
    }
    else
    {
        if (m_SupportedFeatures.TextureSubresourceViews)
        {
            ShaderResourceVariableX TextureLastMipSV{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureLastMip"};
            for (Uint32 MipLevel = 1; MipLevel < m_HierarchicalDepthMipMapRTV.size(); MipLevel++)
            {
                TextureLastMipSV.Set(m_HierarchicalDepthMipMapSRV[MipLevel - 1]);
                RenderAttribs.pDeviceContext->SetRenderTargets(1, &m_HierarchicalDepthMipMapRTV[MipLevel], nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
            }
        }
        else
        {
            ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "cbTextureMipAtrrib"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE]);
            ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMips"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE]);

            for (Uint32 MipLevel = 1; MipLevel < m_HierarchicalDepthMipMapRTV.size(); MipLevel++)
            {
                {
                    MapHelper<Uint32> TextureMipAttribs{RenderAttribs.pDeviceContext, m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE], MAP_WRITE, MAP_FLAG_DISCARD};
                    *TextureMipAttribs = MipLevel - 1;
                }

                RenderAttribs.pDeviceContext->SetRenderTargets(1, &m_HierarchicalDepthMipMapRTV[MipLevel], nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
                RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                CopyTextureAttribs CopyMipAttribs;
                CopyMipAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY];
                CopyMipAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE];
                CopyMipAttribs.SrcMipLevel              = MipLevel;
                CopyMipAttribs.DstMipLevel              = MipLevel;
                CopyMipAttribs.SrcSlice                 = 0;
                CopyMipAttribs.DstSlice                 = 0;
                CopyMipAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                CopyMipAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                RenderAttribs.pDeviceContext->CopyTexture(CopyMipAttribs);
            }
        }
    }
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeStencilMaskAndExtractRoughness(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_STENCIL_MASK_AND_EXTRACT_ROUGHNESS];
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMaterialParameters", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeStencilMaskAndExtractRoughness.fx", "ComputeStencilMaskAndExtractRoughnessPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeStencilMaskAndExtractRoughness",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS].AsTexture()->GetDesc().Format,
                                 },
                                 m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].AsTexture()->GetDesc().Format,
                                 DSS_StencilWrite, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMaterialParameters"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_MATERIAL_PARAMETERS].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeStencilMaskAndExtractRoughness"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS].GetTextureRTV(),
    };

    ITextureView* pDSV = m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].GetTextureDSV();

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearDepthStencil(pDSV, CLEAR_STENCIL_FLAG, 1.0, 0x00, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeIntersection(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_INTERSECTION];
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureBlueNoise", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepthHierarchy", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        if (!m_SupportedFeatures.TextureSubresourceViews)
        {
            // Immutable sampler is required for WebGL to work properly
            ResourceLayout.AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TextureDepthHierarchy", Sam_PointClamp);
        }

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeIntersection.fx", "ComputeIntersectionPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeIntersection",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_RADIANCE].AsTexture()->GetDesc().Format,
                                     m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF].AsTexture()->GetDesc().Format,
                                 },
                                 m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].AsTexture()->GetDesc().Format,
                                 DSS_StencilReadComparisonEqual, BS_Default, true);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRadiance"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRoughness"}.Set(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureBlueNoise"}.Set(RenderAttribs.pPostFXContext->Get2DBlueNoiseSRV(PostFXContext::BLUE_NOISE_DIMENSION_XY));
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepthHierarchy"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeIntersection"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_RADIANCE].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF].GetTextureRTV(),
    };

    constexpr float4 RTVClearColor = float4(0.0, 0.0, 0.0, 0.0);

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[1], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeSpatialReconstruction(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_SPATIAL_RECONSTRUCTION];
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRayDirectionPDF", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureIntersectSpecular", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRayLength", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeSpatialReconstruction.fx", "ComputeSpatialReconstructionPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeSpatialReconstruction",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE].AsTexture()->GetDesc().Format,
                                     m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE].AsTexture()->GetDesc().Format,
                                     m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH].AsTexture()->GetDesc().Format,
                                 },
                                 m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].AsTexture()->GetDesc().Format,
                                 DSS_StencilReadComparisonEqual, BS_Default, true);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRoughness"}.Set(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRayDirectionPDF"}.Set(m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureIntersectSpecular"}.Set(m_Resources[RESOURCE_IDENTIFIER_RADIANCE].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "SpatialReconstruction"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION];
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureCurrVariance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TexturePrevVariance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureHitDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrevDepth", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrevRadiance", Sam_LinearClamp)
            .AddImmutableSampler(SHADER_TYPE_PIXEL, "g_TexturePrevVariance", Sam_LinearClamp);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeTemporalAccumulation.fx", "ComputeTemporalAccumulationPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0].AsTexture()->GetDesc().Format,
                                     m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0].AsTexture()->GetDesc().Format,
                                 },
                                 m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].AsTexture()->GetDesc().Format,
                                 DSS_StencilReadComparisonEqual, BS_Default, true);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    const Uint32 CurrFrameIdx = RenderAttribs.pPostFXContext->GetFrameIndex() & 1;
    const Uint32 PrevFrameIdx = (RenderAttribs.pPostFXContext->GetFrameIndex() - 1) & 1;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureMotion"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRoughness"}.Set(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureHitDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrRadiance"}.Set(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureCurrVariance"}.Set(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevRadiance"}.Set(m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0 + PrevFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TexturePrevVariance"}.Set(m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0 + PrevFrameIdx].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeTemporalAccumulation"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0 + CurrFrameIdx].GetTextureRTV(),
        m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0 + CurrFrameIdx].GetTextureRTV(),
    };

    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);

    if (m_SupportedFeatures.CopyDepthToColor)
    {
        CopyTextureAttribs CopyAttribs;
        CopyAttribs.pSrcTexture              = m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH];
        CopyAttribs.pDstTexture              = m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY];
        CopyAttribs.SrcMipLevel              = 0;
        CopyAttribs.DstMipLevel              = 0;
        CopyAttribs.SrcSlice                 = 0;
        CopyAttribs.DstSlice                 = 0;
        CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribs);
    }
    else
    {
        CopyTextureDepth(RenderAttribs, m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV(), m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY].GetTextureRTV());
    }
}

void ScreenSpaceReflection::ComputeBilateralCleanup(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_BILATERAL_CLEANUP];

    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbCameraAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_TextureVariance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeBilateralCleanup.fx", "ComputeBilateralCleanupPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeBilateralCleanup",
                                 VS, PS, ResourceLayout,
                                 {
                                     m_Resources[RESOURCE_IDENTIFIER_OUTPUT].AsTexture()->GetDesc().Format,
                                 },
                                 m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK].AsTexture()->GetDesc().Format,
                                 DSS_StencilReadComparisonEqual, BS_Default, true);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbCameraAttribs"}.Set(RenderAttribs.pPostFXContext->GetCameraAttribsCB());
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.InitializeSRB(true);
    }

    const Uint32 CurrFrameIdx = RenderAttribs.pPostFXContext->GetFrameIndex() & 1;

    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRadiance"}.Set(m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0 + CurrFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureVariance"}.Set(m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0 + CurrFrameIdx].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureRoughness"}.Set(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureNormal"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL].GetTextureSRV());
    ShaderResourceVariableX{RenderTech.SRB, SHADER_TYPE_PIXEL, "g_TextureDepth"}.Set(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH].GetTextureSRV());

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBilateralCleanup"};

    ITextureView* pRTVs[] = {
        m_Resources[RESOURCE_IDENTIFIER_OUTPUT].GetTextureRTV(),
    };

    constexpr float4 RTVClearColor = float4(0.0, 0.0, 0.0, 0.0);

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

} // namespace Diligent
