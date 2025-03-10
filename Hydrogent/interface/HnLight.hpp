/*
 *  Copyright 2023 Diligent Graphics LLC
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

#pragma once

#include "pxr/imaging/hd/light.h"

#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

namespace USD
{

/// Light implementation in Hydrogent.
class HnLight final : public pxr::HdLight
{
public:
    static HnLight* Create(const pxr::SdfPath& Id);

    ~HnLight();

    // Synchronizes state from the delegate to this object.
    virtual void Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    // Returns the minimal set of dirty bits to place in the
    // change tracker for use in the first sync of this prim.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    const float3& GetDirection() const { return m_Direction; }
    const float4& GetIntensity() const { return m_Intensity; }

    void SetDirection(const float3& Direction) { m_Direction = Direction; }
    void SetIntensity(const float4& Intensity) { m_Intensity = Intensity; }

private:
    HnLight(const pxr::SdfPath& Id);

private:
    float3 m_Direction;
    float4 m_Intensity;
};

} // namespace USD

} // namespace Diligent
