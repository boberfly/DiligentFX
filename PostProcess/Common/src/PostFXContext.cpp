/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "PostFXContext.hpp"

#include "CommonlyUsedStates.h"
#include "GraphicsTypesX.hpp"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "RenderStateCache.hpp"
#include "ScopedDebugGroup.hpp"
#include "InternalResourceExtractor.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"

}

namespace NoiseBuffers
{

#include "SamplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"

}

PostFXContext::PostFXContext(IRenderDevice* pDevice)
{
    DEV_CHECK_ERR(pDevice != nullptr, "pDevice must not be null");

    RenderDeviceWithCache_N Device{pDevice};
    {
        TextureDesc Desc;
        Desc.Name      = "PostFXContext::SobolBuffer";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 256;
        Desc.Height    = 256;
        Desc.Format    = TEX_FORMAT_R32_UINT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE;

        TextureSubResData SubResData;
        SubResData.pData  = NoiseBuffers::Sobol_256spp_256d;
        SubResData.Stride = 4ull * Desc.Width;

        TextureData Data;
        Data.pContext        = nullptr;
        Data.NumSubresources = 1;
        Data.pSubResources   = &SubResData;

        m_Resources[RESOURCE_IDENTIFIER_SOBOL_BUFFER] = Device.CreateTexture(Desc, &Data);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "PostFXContext::ScramblingTileBuffer";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 128 * 4;
        Desc.Height    = 128 * 2;
        Desc.Format    = TEX_FORMAT_R32_UINT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE;

        TextureSubResData SubResData;
        SubResData.pData  = NoiseBuffers::ScramblingTile;
        SubResData.Stride = 4ull * Desc.Width;

        TextureData Data;
        Data.pContext        = nullptr;
        Data.NumSubresources = 1;
        Data.pSubResources   = &SubResData;

        m_Resources[RESOURCE_IDENTIFIER_SCRAMBLING_TILE_BUFFER] = Device.CreateTexture(Desc, &Data);
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_XY; TextureIdx <= RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_ZW; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "PostFXContext::BlueNoiseTexture";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 128;
        Desc.Height    = 128;
        Desc.Format    = TEX_FORMAT_RG8_UNORM;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[TextureIdx] = Device.CreateTexture(Desc, nullptr);
    }

    RefCntAutoPtr<IBuffer> pBuffer;
    CreateUniformBuffer(pDevice, 2 * sizeof(HLSL::CameraAttribs), "PostFXContext::IntermediateConstantBuffer", &pBuffer);
    m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE] = pBuffer;
}

PostFXContext::~PostFXContext() = default;

void PostFXContext::PrepareResources(const RenderAttributes& RenderAttribs)
{
    DEV_CHECK_ERR(RenderAttribs.pDevice != nullptr, "RenderAttribs.pDevice must not be null");
    DEV_CHECK_ERR(RenderAttribs.pDeviceContext != nullptr, "RenderAttribs.pDeviceContext must not be null");

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "PreparePostFX"};

    if (RenderAttribs.pCameraAttribsCB == nullptr)
    {
        DEV_CHECK_ERR(RenderAttribs.pCurrCamera != nullptr, "RenderAttribs.pCurrCamera must not be null");
        DEV_CHECK_ERR(RenderAttribs.pPrevCamera != nullptr, "RenderAttribs.pPrevCamera must not be null");

        if (!m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER])
        {
            RefCntAutoPtr<IBuffer> pBuffer;
            CreateUniformBuffer(RenderAttribs.pDevice, 2 * sizeof(HLSL::CameraAttribs), "PostFXContext::CameraAttibsConstantBuffer", &pBuffer);
            m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER] = pBuffer;
        }

        MapHelper<HLSL::CameraAttribs> CameraAttibs(RenderAttribs.pDeviceContext, reinterpret_cast<IBuffer*>(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER].RawPtr()), MAP_WRITE, MAP_FLAG_DISCARD);
        CameraAttibs[0] = *RenderAttribs.pCurrCamera;
        CameraAttibs[1] = *RenderAttribs.pPrevCamera;
    }
    else
    {
        m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER] = RenderAttribs.pCameraAttribsCB;
    }

    {
        MapHelper<Uint32> FrameAttibs(RenderAttribs.pDeviceContext, reinterpret_cast<IBuffer*>(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE].RawPtr()), MAP_WRITE, MAP_FLAG_DISCARD);
        *FrameAttibs        = RenderAttribs.FrameIndex;
        m_CurrentFrameIndex = RenderAttribs.FrameIndex;
    }

    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_BLUE_NOISE_TEXTURE];
    if (!RenderTech.IsInitialized())
    {
        PipelineResourceLayoutDescX ResourceLayout;
        ResourceLayout
            .AddVariable(SHADER_TYPE_PIXEL, "cbBlueNoiseAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_SobolBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_ScramblingTileBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        const auto VS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVS", SHADER_TYPE_VERTEX);
        const auto PS = PostFXRenderTechnique::CreateShader(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeBlueNoiseTexture.fx", "ComputeBlueNoiseTexturePS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "PreparePostFX::ComputeBlueNoiseTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_XY]),
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_ZW]),
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "cbBlueNoiseAttribs"}.Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE]);
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "g_SobolBuffer"}.Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_SOBOL_BUFFER]));
        ShaderResourceVariableX{RenderTech.PSO, SHADER_TYPE_PIXEL, "g_ScramblingTileBuffer"}.Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_SCRAMBLING_TILE_BUFFER]));
        RenderTech.InitializeSRB(true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBlueNoiseTexture"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_XY]),
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_ZW]),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

ITextureView* PostFXContext::Get2DBlueNoiseSRV(BLUE_NOISE_DIMENSION Dimension) const
{
    return StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_XY + Dimension])->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

IBuffer* PostFXContext::GetCameraAttribsCB() const
{
    return StaticCast<IBuffer*>(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
}

Uint32 PostFXContext::GetFrameIndex() const
{
    return m_CurrentFrameIndex;
}

} // namespace Diligent