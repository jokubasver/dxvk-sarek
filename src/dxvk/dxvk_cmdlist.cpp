#include "dxvk_cmdlist.h"
#include "dxvk_device.h"

namespace dxvk {
    
  DxvkCommandList::DxvkCommandList(DxvkDevice* device)
  : m_device        (device),
    m_vkd           (device->vkd()),
    m_vki           (device->instance()->vki()),
    m_cmdBuffersUsed(0),
    m_descriptorPoolTracker(device) {
    const auto& graphicsQueue = m_device->queues().graphics;
    const auto& transferQueue = m_device->queues().transfer;

    VkFenceCreateInfo fenceInfo;
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = 0;
    
    if (m_vkd->vkCreateFence(m_vkd->device(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to create fence");
    
    VkCommandPoolCreateInfo poolInfo;
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.pNext            = nullptr;
    poolInfo.flags            = 0;
    poolInfo.queueFamilyIndex = graphicsQueue.queueFamily;
    
    if (m_vkd->vkCreateCommandPool(m_vkd->device(), &poolInfo, nullptr, &m_graphicsPool) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to create graphics command pool");
    
    if (m_device->hasDedicatedTransferQueue()) {
      poolInfo.queueFamilyIndex = transferQueue.queueFamily;

      if (m_vkd->vkCreateCommandPool(m_vkd->device(), &poolInfo, nullptr, &m_transferPool) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to create transfer command pool");
    }
    
    VkCommandBufferAllocateInfo cmdInfoGfx;
    cmdInfoGfx.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfoGfx.pNext             = nullptr;
    cmdInfoGfx.commandPool       = m_graphicsPool;
    cmdInfoGfx.level             = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfoGfx.commandBufferCount = 1;
    
    VkCommandBufferAllocateInfo cmdInfoDma;
    cmdInfoDma.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfoDma.pNext             = nullptr;
    cmdInfoDma.commandPool       = m_transferPool ? m_transferPool : m_graphicsPool;
    cmdInfoDma.level             = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfoDma.commandBufferCount = 1;
    
    if (m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoGfx, &m_execBuffer) != VK_SUCCESS
     || m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoGfx, &m_initBuffer) != VK_SUCCESS
     || m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoDma, &m_sdmaBuffer) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to allocate command buffer");
    
    if (m_device->hasDedicatedTransferQueue()) {
      VkSemaphoreCreateInfo semInfo;
      semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      semInfo.pNext = nullptr;
      semInfo.flags = 0;

      if (m_vkd->vkCreateSemaphore(m_vkd->device(), &semInfo, nullptr, &m_sdmaSemaphore) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to create semaphore");
    }
  }
  
  
  DxvkCommandList::~DxvkCommandList() {
    this->reset();

    m_vkd->vkDestroySemaphore(m_vkd->device(), m_sdmaSemaphore, nullptr);
    
    m_vkd->vkDestroyCommandPool(m_vkd->device(), m_graphicsPool, nullptr);
    m_vkd->vkDestroyCommandPool(m_vkd->device(), m_transferPool, nullptr);
    
    m_vkd->vkDestroyFence(m_vkd->device(), m_fence, nullptr);
  }
  
  
  VkResult DxvkCommandList::submit(
          VkSemaphore     waitSemaphore,
          VkSemaphore     wakeSemaphore) {
    const auto& graphics = m_device->queues().graphics;
    const auto& transfer = m_device->queues().transfer;

    m_submission.reset();

    if (m_cmdBuffersUsed.test(DxvkCmdBuffer::SdmaBuffer)) {
      m_submission.cmdBuffers.push_back(m_sdmaBuffer);

      if (m_device->hasDedicatedTransferQueue()) {
        m_submission.addWakeSemaphore(m_sdmaSemaphore, 0);
        VkResult status = submitToQueue(transfer.queueHandle, VK_NULL_HANDLE, m_submission);

        if (status != VK_SUCCESS)
          return status;

        m_submission.reset();
        m_submission.addWaitSemaphore(m_sdmaSemaphore, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
      }
    }

    if (m_cmdBuffersUsed.test(DxvkCmdBuffer::InitBuffer))
      m_submission.cmdBuffers.push_back(m_initBuffer);
    if (m_cmdBuffersUsed.test(DxvkCmdBuffer::ExecBuffer))
      m_submission.cmdBuffers.push_back(m_execBuffer);
    
    if (waitSemaphore)
      m_submission.addWaitSemaphore(waitSemaphore, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    if (wakeSemaphore)
      m_submission.addWakeSemaphore(wakeSemaphore, 0);
    
    for (const auto& entry : m_waitSemaphores) {
      m_submission.addWaitSemaphore(entry.fence->handle(), entry.value, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }

    for (const auto& entry : m_signalSemaphores) {
      m_submission.addWakeSemaphore(entry.fence->handle(), entry.value);
    }

    return submitToQueue(graphics.queueHandle, m_fence, m_submission);
  }
  
  
  VkResult DxvkCommandList::synchronize() {
    VkResult status = VK_TIMEOUT;
    
    while (status == VK_TIMEOUT) {
      status = m_vkd->vkWaitForFences(
        m_vkd->device(), 1, &m_fence, VK_FALSE,
        1'000'000'000ull);
    }
    
    return status;
  }
  
  
  void DxvkCommandList::beginRecording() {
    VkCommandBufferBeginInfo info;
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext            = nullptr;
    info.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    info.pInheritanceInfo = nullptr;
    
    if ((m_graphicsPool && m_vkd->vkResetCommandPool(m_vkd->device(), m_graphicsPool, 0) != VK_SUCCESS)
     || (m_transferPool && m_vkd->vkResetCommandPool(m_vkd->device(), m_transferPool, 0) != VK_SUCCESS))
      Logger::err("DxvkCommandList: Failed to reset command buffer");
    
    if (m_vkd->vkBeginCommandBuffer(m_execBuffer, &info) != VK_SUCCESS
     || m_vkd->vkBeginCommandBuffer(m_initBuffer, &info) != VK_SUCCESS
     || m_vkd->vkBeginCommandBuffer(m_sdmaBuffer, &info) != VK_SUCCESS)
      Logger::err("DxvkCommandList: Failed to begin command buffer");
    
    if (m_vkd->vkResetFences(m_vkd->device(), 1, &m_fence) != VK_SUCCESS)
      Logger::err("DxvkCommandList: Failed to reset fence");
    
    // Unconditionally mark the exec buffer as used. There
    // is virtually no use case where this isn't correct.
    m_cmdBuffersUsed = DxvkCmdBuffer::ExecBuffer;

    // The pool reset above also reset the render pass buffers
    m_passBuffersUsed = 0;
  }


  void DxvkCommandList::cmdBeginDeferredRenderPass(
    const VkRenderPassBeginInfo*  pRenderPassBegin) {
    if (m_passBuffersUsed == m_passBuffers.size()) {
      VkCommandBufferAllocateInfo allocInfo;
      allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocInfo.pNext              = nullptr;
      allocInfo.commandPool        = m_graphicsPool;
      allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
      allocInfo.commandBufferCount = 1;

      VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

      if (m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &allocInfo, &cmdBuffer) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to allocate render pass command buffer");

      m_passBuffers.push_back(cmdBuffer);
    }

    VkCommandBuffer passBuffer = m_passBuffers[m_passBuffersUsed++];

    VkCommandBufferInheritanceInfo inheritance;
    inheritance.sType                = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.pNext                = nullptr;
    inheritance.renderPass           = pRenderPassBegin->renderPass;
    inheritance.subpass              = 0;
    inheritance.framebuffer          = pRenderPassBegin->framebuffer;
    inheritance.occlusionQueryEnable = VK_FALSE;
    inheritance.queryFlags           = 0;
    inheritance.pipelineStatistics   = 0;

    VkCommandBufferBeginInfo info;
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext            = nullptr;
    info.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
                          | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    info.pInheritanceInfo = &inheritance;

    if (m_vkd->vkBeginCommandBuffer(passBuffer, &info) != VK_SUCCESS)
      Logger::err("DxvkCommandList: Failed to begin render pass command buffer");

    m_passBegin = *pRenderPassBegin;

    uint32_t clearCount = std::min<uint32_t>(m_passBegin.clearValueCount, m_passClearValues.size());

    for (uint32_t i = 0; i < clearCount; i++)
      m_passClearValues[i] = m_passBegin.pClearValues[i];

    m_passBegin.clearValueCount = clearCount;
    m_passBegin.pClearValues    = clearCount ? m_passClearValues.data() : nullptr;

    m_primaryBuffer = m_execBuffer;
    m_execBuffer    = passBuffer;
  }


  void DxvkCommandList::cmdEndDeferredRenderPass(
          VkRenderPass            renderPass) {
    VkCommandBuffer passBuffer = m_execBuffer;

    if (m_vkd->vkEndCommandBuffer(passBuffer) != VK_SUCCESS)
      Logger::err("DxvkCommandList: Failed to record render pass command buffer");

    m_execBuffer    = m_primaryBuffer;
    m_primaryBuffer = VK_NULL_HANDLE;

    m_passBegin.renderPass = renderPass;

    m_vkd->vkCmdBeginRenderPass(m_execBuffer, &m_passBegin,
      VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    m_vkd->vkCmdExecuteCommands(m_execBuffer, 1, &passBuffer);
    m_vkd->vkCmdEndRenderPass(m_execBuffer);
  }
  
  
  void DxvkCommandList::endRecording() {
    if (m_vkd->vkEndCommandBuffer(m_execBuffer) != VK_SUCCESS
     || m_vkd->vkEndCommandBuffer(m_initBuffer) != VK_SUCCESS
     || m_vkd->vkEndCommandBuffer(m_sdmaBuffer) != VK_SUCCESS)
      Logger::err("DxvkCommandList::endRecording: Failed to record command buffer");
  }
  
  
  void DxvkCommandList::reset() {
    // Free resources and other objects
    // that are no longer in use
    m_resources.reset();

    // Recycle heavy Vulkan objects
    m_descriptorPoolTracker.reset();

    // Return buffer memory slices
    m_bufferTracker.reset();

    // Return query and event handles
    m_gpuQueryTracker.reset();
    m_gpuEventTracker.reset();

    // Less important stuff
    m_signalTracker.reset();
    m_statCounters.reset();

    m_waitSemaphores.clear();
    m_signalSemaphores.clear();
  }


  VkResult DxvkCommandList::submitToQueue(
          VkQueue               queue,
          VkFence               fence,
    const DxvkQueueSubmission&  info) {
    VkTimelineSemaphoreSubmitInfoKHR timelineInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };
    timelineInfo.waitSemaphoreValueCount    = info.waitValues.size();
    timelineInfo.pWaitSemaphoreValues       = info.waitValues.data();
    timelineInfo.signalSemaphoreValueCount  = info.wakeValues.size();
    timelineInfo.pSignalSemaphoreValues     = info.wakeValues.data();

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount   = info.waitSync.size();
    submitInfo.pWaitSemaphores      = info.waitSync.data();
    submitInfo.pWaitDstStageMask    = info.waitMask.data();
    submitInfo.commandBufferCount   = info.cmdBuffers.size();
    submitInfo.pCommandBuffers      = info.cmdBuffers.data();
    submitInfo.signalSemaphoreCount = info.wakeSync.size();
    submitInfo.pSignalSemaphores    = info.wakeSync.data();

    if (m_device->features().khrTimelineSemaphore.timelineSemaphore)
      submitInfo.pNext = &timelineInfo;
    
    return m_vkd->vkQueueSubmit(queue, 1, &submitInfo, fence);
  }
  
  void DxvkCommandList::cmdBeginDebugUtilsLabel(VkDebugUtilsLabelEXT *pLabelInfo) {
    m_vki->vkCmdBeginDebugUtilsLabelEXT(m_execBuffer, pLabelInfo);
  }

  void DxvkCommandList::cmdEndDebugUtilsLabel() {
    m_vki->vkCmdEndDebugUtilsLabelEXT(m_execBuffer);
  }

  void DxvkCommandList::cmdInsertDebugUtilsLabel(VkDebugUtilsLabelEXT *pLabelInfo) {
    m_vki->vkCmdInsertDebugUtilsLabelEXT(m_execBuffer, pLabelInfo);
  }
}
