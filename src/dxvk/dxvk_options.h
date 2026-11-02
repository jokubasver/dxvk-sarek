#pragma once

#include "../util/config/config.h"

#include "../vulkan/vulkan_loader.h"

namespace dxvk {

  struct DxvkOptions {
    DxvkOptions() { }
    DxvkOptions(const Config& config);

    /// Enable debug utils (alternative to DXVK_PERF_EVENTS=1)
    bool enableDebugUtils = false;

    /// Enable state cache
    bool enableStateCache = true;

    /// Number of compiler threads
    /// when using the state cache
    int32_t numCompilerThreads = 0;

    // Hides integrated GPUs if dedicated GPUs are
    // present. May be necessary for some games that
    // incorrectly assume monitor layouts.
    bool hideIntegratedGraphics = false;

    /// Device name
    std::string deviceFilter;

    // Tiler GPU tweaks. Currently biases host-visible
    // allocations toward cached memory on tilers; the
    // render-pass-op side is detected but not yet acted on.
    Tristate tilerMode = Tristate::Auto;

    /// Barriers after fragment work leave out the vertex
    /// stages, so the next render pass's vertex and tiling
    /// work can overlap it. Auto follows tilerMode.
    Tristate fragmentBarriers = Tristate::Auto;

    /// Record render passes into secondary command buffers and begin
    /// them when they end, so a pass can still learn that its depth
    /// contents won't be needed. Auto follows tilerMode.
    Tristate deferRenderPasses = Tristate::Auto;

    /// Reuse a descriptor set already written with the same descriptors
    /// instead of allocating and writing another. Auto = on.
    Tristate descriptorSetCache = Tristate::Auto;

    /// Storage buffer accesses skip the robustness bounds checks
    /// (VK_EXT_pipeline_robustness). Uniform buffers and vertex input keep
    /// them: D3D9 constants past the bound range must read zero. Auto = Arm.
    Tristate relaxedBufferRobustness = Tristate::Auto;

    /// D3D9 event queries complete with their command list instead of a
    /// vkCmdSetEvent, which has to end the render pass. Auto = tiler.
    Tristate eventQueryAtSubmit = Tristate::Auto;

    /// Prefer VK_PRESENT_MODE_FIFO_LATEST_READY_EXT over FIFO when the
    /// surface supports it. Auto = on.
    Tristate presentLatestReady = Tristate::Auto;

    /// Cull mode, front face, depth test, depth write, depth compare op and
    /// vertex strides are dynamic state (VK_EXT_extended_dynamic_state),
    /// so pipelines differing only in them are one. Auto = on if supported.
    Tristate dynamicPipelineState = Tristate::Auto;

    /// Depth bias for draws over existing depth into a render-to-single-
    /// sampled target whose pass was ended since its last depth clear,
    /// see DxvkContext::updateMsaaReloadBias. Auto = on.
    Tristate msaaReloadBias = Tristate::Auto;

    /// Measurement only: never enable alpha to coverage
    bool disableAlphaToCoverage = false;

    /// Hand the driver a pipeline cache and keep it on disk beside
    /// the state cache, for drivers without a disk cache of their own
    Tristate pipelineCache = Tristate::Auto;

    // Zero-initialize host-visible mapped memory on allocation.
    // Works around games that assume freshly mapped buffers are clean.
    bool zeroMappedMemory = false;

    /// Whether to use custom sin/cos approximation
    Tristate lowerSinCos = Tristate::Auto;

    /// Memory budget in bytes
    VkDeviceSize maxMemoryBudget = 0u;

    /// Shader compilation method. Supported values:
    /// "dyasync" (default), "async", "none".
    std::string shaderCompilationMethod;

    // Number of compiler threads used by the
    // active shader compilation method
    int32_t numShaderCompilerThreads;

    /// Frame pacing mode. Supported values: "", "low-latency", "min-latency".
    /// Empty (default) preserves Sarek existing behaviour unchanged.
    std::string framePace;

    /// Fine-tuning offset for low-latency frame pacing, in microseconds.
    /// Clamped to [-10000, 10000]. Defaults to 0.
    int32_t lowLatencyOffset = 0;

    /// Shader-related options
    Tristate useRawSsbo = Tristate::Auto;

    /// Workaround for NVIDIA driver bug 3114283
    Tristate shrinkNvidiaHvvHeap;

    /// HUD elements
    std::string hud;
  };

}
