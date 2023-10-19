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

#include "Tasks/HnRenderRprimsTask.hpp"

#include "pxr/imaging/hd/renderDelegate.h"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

HnRenderRprimsTask::HnRenderRprimsTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnRenderRprimsTask::~HnRenderRprimsTask()
{
}

void HnRenderRprimsTask::Sync(pxr::HdSceneDelegate* Delegate,
                              pxr::HdTaskContext*   TaskCtx,
                              pxr::HdDirtyBits*     DirtyBits)
{
    VERIFY_EXPR(Delegate != nullptr && TaskCtx != nullptr && DirtyBits != nullptr);

    if (*DirtyBits & pxr::HdChangeTracker::DirtyCollection)
    {
        pxr::VtValue CollectionVal = Delegate->Get(GetId(), pxr::HdTokens->collection);

        pxr::HdRprimCollection Collection = CollectionVal.Get<pxr::HdRprimCollection>();

        if (Collection.GetName().IsEmpty())
        {
            m_RenderPass.reset();
        }
        else
        {
            if (!m_RenderPass)
            {
                pxr::HdRenderIndex&    Index          = Delegate->GetRenderIndex();
                pxr::HdRenderDelegate* RenderDelegate = Index.GetRenderDelegate();
                m_RenderPass                          = RenderDelegate->CreateRenderPass(&Index, Collection);
            }
            else
            {
                m_RenderPass->SetRprimCollection(Collection);
            }
        }
    }

    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        // TODO
    }

    if (*DirtyBits & pxr::HdChangeTracker::DirtyRenderTags)
    {
        m_RenderTags = _GetTaskRenderTags(Delegate);
    }

    if (m_RenderPass)
    {
        m_RenderPass->Sync();
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

void HnRenderRprimsTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                 pxr::HdRenderIndex* RenderIndex)
{
}

void HnRenderRprimsTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    // TODO
    pxr::HdRenderPassStateSharedPtr RenderPassState;

    if (m_RenderPass)
    {
        m_RenderPass->Execute(RenderPassState, GetRenderTags());
    }
}

} // namespace USD

} // namespace Diligent