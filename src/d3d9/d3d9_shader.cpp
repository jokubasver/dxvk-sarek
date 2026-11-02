#include "d3d9_shader.h"

#include "d3d9_caps.h"
#include "d3d9_device.h"
#include "d3d9_util.h"

#include "../util/util_stall.h"

namespace dxvk {

  D3D9CommonShader::D3D9CommonShader() {}

  D3D9CommonShader::D3D9CommonShader(
            D3D9DeviceEx*         pDevice,
            VkShaderStageFlagBits ShaderStage,
      const DxvkShaderKey&        Key,
      const DxsoModuleInfo*       pDxsoModuleInfo,
      const void*                 pShaderBytecode,
      const DxsoAnalysisInfo&     AnalysisInfo,
            DxsoModule*           pModule) {
    const uint32_t bytecodeLength = AnalysisInfo.bytecodeByteLength;
    m_bytecode.resize(bytecodeLength);
    std::memcpy(m_bytecode.data(), pShaderBytecode, bytecodeLength);

    const std::string name = Key.toString();
    Logger::debug(str::format("Compiling shader ", name));
    
    // If requested by the user, dump both the raw DXBC
    // shader and the compiled SPIR-V module to a file.
    const std::string dumpPath = env::getEnvVar("DXVK_SHADER_DUMP_PATH");
    
    if (dumpPath.size() != 0) {
      DxsoReader reader(
        reinterpret_cast<const char*>(pShaderBytecode));

      reader.store(std::ofstream(str::topath(str::format(dumpPath, "/", name, ".dxso").c_str()).c_str(),
        std::ios_base::binary | std::ios_base::trunc), bytecodeLength);

      char comment[2048];
      Com<ID3DBlob> blob;
      HRESULT hr = DisassembleShader(
        pShaderBytecode,
        TRUE,
        comment, 
        &blob);
      
      if (SUCCEEDED(hr)) {
        std::ofstream disassembledOut(str::topath(str::format(dumpPath, "/", name, ".dxso.dis").c_str()).c_str(), std::ios_base::binary | std::ios_base::trunc);
        disassembledOut.write(
          reinterpret_cast<const char*>(blob->GetBufferPointer()),
          blob->GetBufferSize());
      }
    }
    
    // Decide whether we need to create a pass-through
    // geometry shader for vertex shader stream output

    const D3D9ConstantLayout& constantLayout = ShaderStage == VK_SHADER_STAGE_VERTEX_BIT
      ? pDevice->GetVertexConstantLayout()
      : pDevice->GetPixelConstantLayout();
    m_shaders      = pModule->compile(*pDxsoModuleInfo, name, AnalysisInfo, constantLayout);
    m_isgn         = pModule->isgn();
    m_usedSamplers = pModule->usedSamplers();

    // Shift up these sampler bits so we can just
    // do an or per-draw in the device.
    // We shift by 17 because 16 ps samplers + 1 dmap (tess)
    if (ShaderStage == VK_SHADER_STAGE_VERTEX_BIT)
      m_usedSamplers <<= caps::MaxTexturesPS + 1;

    m_usedRTs      = pModule->usedRTs();

    m_info      = pModule->info();
    m_meta      = pModule->meta();
    m_constants = pModule->constants();
    m_maxDefinedConst = pModule->maxDefinedConstant();

    m_shaders[0]->setShaderKey(Key);

    if (m_shaders[1] != nullptr) {
      // Lets lie about the shader key type for the state cache.
      m_shaders[1]->setShaderKey({ VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, Key.sha1() });
    }
    
    if (dumpPath.size() != 0) {
      std::ofstream dumpStream(
        str::topath(str::format(dumpPath, "/", name, ".spv").c_str()).c_str(),
        std::ios_base::binary | std::ios_base::trunc);
      
      m_shaders[0]->dump(dumpStream);
    }

    pDevice->GetDXVKDevice()->registerShader(m_shaders[0]);

    if (m_shaders[1] != nullptr)
      pDevice->GetDXVKDevice()->registerShader(m_shaders[1]);
  }


  Rc<DxvkShader> D3D9CommonShader::GetSrgbBlendShader(
          D3D9DeviceEx*         pDevice,
          uint32_t              Key,
          D3D9ShaderPermutation Permutation) const {
    std::lock_guard<dxvk::mutex> lock(m_srgbBlend->mutex);

    auto entry = m_srgbBlend->shaders.find(Key);

    if (entry == m_srgbBlend->shaders.end()) {
      double t0 = stall::enabled() ? stall::now() : 0.0;

      DxsoReader reader(reinterpret_cast<const char*>(m_bytecode.data()));
      DxsoModule module(reader);
      DxsoAnalysisInfo analysis = module.analyze();

      DxsoModuleInfo moduleInfo;
      moduleInfo.options = pDevice->GetDxsoOptions();
      moduleInfo.options.srgbBlendKey = Key;

      // No shader key: the state cache can't recreate these
      std::string name = str::format(GetName(), "_srgbblend_", std::hex, Key);
      entry = m_srgbBlend->shaders.insert({ Key,
        module.compile(moduleInfo, name, analysis, pDevice->GetPixelConstantLayout()) }).first;

      if (stall::enabled())
        Logger::info(str::format("STALL ", uint32_t(t0), " ms: shader variant ", name,
          " translated in ", uint32_t(stall::now() - t0), " ms"));
    }

    return entry->second[Permutation];
  }


  Rc<DxvkShader> D3D9CommonShader::GetPropInstanceShader(
          D3D9DeviceEx*         pDevice,
          uint32_t              Mode) const {
    std::lock_guard<dxvk::mutex> lock(m_srgbBlend->mutex);

    DxsoPermutations& variant = m_srgbBlend->propInstancing.at(Mode);

    if (variant[D3D9ShaderPermutations::None] == nullptr) {
      double t0 = stall::enabled() ? stall::now() : 0.0;

      DxsoReader reader(reinterpret_cast<const char*>(m_bytecode.data()));
      DxsoModule module(reader);
      DxsoAnalysisInfo analysis = module.analyze();

      DxsoModuleInfo moduleInfo;
      moduleInfo.options = pDevice->GetDxsoOptions();
      moduleInfo.options.propInstancing = Mode;

      // No shader key: the state cache can't recreate these
      std::string name = str::format(GetName(), "_propinst_", Mode);
      variant = module.compile(moduleInfo, name, analysis, pDevice->GetVertexConstantLayout());

      if (stall::enabled())
        Logger::info(str::format("STALL ", uint32_t(t0), " ms: shader variant ", name,
          " translated in ", uint32_t(stall::now() - t0), " ms"));
    }

    return variant[D3D9ShaderPermutations::None];
  }


  void D3D9ShaderModuleSet::GetShaderModule(
            D3D9DeviceEx*         pDevice,
            D3D9CommonShader*     pShaderModule,
            VkShaderStageFlagBits ShaderStage,
      const DxsoModuleInfo*       pDxbcModuleInfo,
      const void*                 pShaderBytecode) {
    DxsoReader reader(
      reinterpret_cast<const char*>(pShaderBytecode));

    DxsoModule module(reader);

    if (module.info().shaderStage() != ShaderStage)
      throw DxvkError("GetShaderModule: Bytecode does not match shader stage");

    DxsoAnalysisInfo info = module.analyze();

    DxvkShaderKey lookupKey = DxvkShaderKey(
      ShaderStage,
      Sha1Hash::compute(pShaderBytecode, info.bytecodeByteLength));

    // Use the shader's unique key for the lookup
    { std::unique_lock<dxvk::mutex> lock(m_mutex);
      
      auto entry = m_modules.find(lookupKey);
      if (entry != m_modules.end()) {
        *pShaderModule = entry->second;
        return;
      }
    }
    
    // This shader has not been compiled yet, so we have to create a
    // new module. This takes a while, so we won't lock the structure.
    *pShaderModule = D3D9CommonShader(
      pDevice, ShaderStage, lookupKey,
      pDxbcModuleInfo, pShaderBytecode,
      info, &module);
    
    // Insert the new module into the lookup table. If another thread
    // has compiled the same shader in the meantime, we should return
    // that object instead and discard the newly created module.
    { std::unique_lock<dxvk::mutex> lock(m_mutex);
      
      auto status = m_modules.insert({ lookupKey, *pShaderModule });
      if (!status.second) {
        *pShaderModule = status.first->second;
        return;
      }
    }
  }

}
