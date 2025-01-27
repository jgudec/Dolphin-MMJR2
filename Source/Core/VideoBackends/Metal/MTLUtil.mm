// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Metal/MTLUtil.h"

#include <fstream>
#include <string>

#include <TargetConditionals.h>
#include <spirv_msl.hpp>

#include "Common/MsgHandler.h"

#include "VideoCommon/Spirv.h"

Metal::DeviceFeatures Metal::g_features;

std::vector<MRCOwned<id<MTLDevice>>> Metal::Util::GetAdapterList()
{
  std::vector<MRCOwned<id<MTLDevice>>> list;
  id<MTLDevice> default_dev = MTLCreateSystemDefaultDevice();
  if (default_dev)
    list.push_back(MRCTransfer(default_dev));

#if TARGET_OS_OSX
  auto devices = MRCTransfer(MTLCopyAllDevices());
  for (id<MTLDevice> device in devices.Get())
  {
    if (device != default_dev)
      list.push_back(MRCRetain(device));
  }
#endif

  return list;
}

void Metal::Util::PopulateBackendInfo(VideoConfig* config)
{
  config->backend_info.api_type = APIType::Metal;
  config->backend_info.bUsesLowerLeftOrigin = false;
  config->backend_info.bSupportsExclusiveFullscreen = false;
  config->backend_info.bSupportsDualSourceBlend = true;
  config->backend_info.bSupportsPrimitiveRestart = true;
  config->backend_info.bSupportsGeometryShaders = false;
  config->backend_info.bSupportsComputeShaders = true;
  config->backend_info.bSupports3DVision = false;
  config->backend_info.bSupportsEarlyZ = true;
  config->backend_info.bSupportsBindingLayout = true;
  config->backend_info.bSupportsBBox = true;
  config->backend_info.bSupportsGSInstancing = false;
  config->backend_info.bSupportsPostProcessing = true;
  config->backend_info.bSupportsPaletteConversion = true;
  config->backend_info.bSupportsClipControl = true;
  config->backend_info.bSupportsSSAA = true;
  config->backend_info.bSupportsFragmentStoresAndAtomics = true;
  config->backend_info.bSupportsReversedDepthRange = false;
  config->backend_info.bSupportsLogicOp = false;
  config->backend_info.bSupportsMultithreading = false;
  config->backend_info.bSupportsGPUTextureDecoding = true;
  config->backend_info.bSupportsCopyToVram = true;
  config->backend_info.bSupportsBitfield = true;
  config->backend_info.bSupportsDynamicSamplerIndexing = true;
  config->backend_info.bSupportsFramebufferFetch = false;
  config->backend_info.bSupportsBackgroundCompiling = true;
  config->backend_info.bSupportsLargePoints = true;
  config->backend_info.bSupportsPartialDepthCopies = true;
  config->backend_info.bSupportsDepthReadback = true;
  config->backend_info.bSupportsShaderBinaries = false;
  config->backend_info.bSupportsPipelineCacheData = false;
  config->backend_info.bSupportsCoarseDerivatives = false;
  config->backend_info.bSupportsTextureQueryLevels = true;
  config->backend_info.bSupportsLodBiasInSampler = false;
  config->backend_info.bSupportsSettingObjectNames = true;
  // Metal requires multisample resolve to be done on a render pass
  config->backend_info.bSupportsPartialMultisampleResolve = false;
}

void Metal::Util::PopulateBackendInfoAdapters(VideoConfig* config,
                                              const std::vector<MRCOwned<id<MTLDevice>>>& adapters)
{
  config->backend_info.Adapters.clear();
  for (id<MTLDevice> adapter : adapters)
  {
    config->backend_info.Adapters.push_back([[adapter name] UTF8String]);
  }
}

void Metal::Util::PopulateBackendInfoFeatures(VideoConfig* config, id<MTLDevice> device)
{
#if TARGET_OS_OSX
  config->backend_info.bSupportsDepthClamp = true;
  config->backend_info.bSupportsST3CTextures = true;
  config->backend_info.bSupportsBPTCTextures = true;
#else
  bool supports_mac1 = false;
  bool supports_apple4 = false;
  if (@available(iOS 13, *))
  {
    supports_mac1 = [device supportsFamily:MTLGPUFamilyMac1];
    supports_apple4 = [device supportsFamily:MTLGPUFamilyApple4];
  }
  else
  {
    supports_apple4 = [device supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily4_v1];
  }
  config->backend_info.bSupportsDepthClamp = supports_mac1 || supports_apple4;
  config->backend_info.bSupportsST3CTextures = supports_mac1;
  config->backend_info.bSupportsBPTCTextures = supports_mac1;
  config->backend_info.bSupportsFramebufferFetch = true;
#endif

  config->backend_info.AAModes.clear();
  for (u32 i = 1; i <= 64; i <<= 1)
  {
    if ([device supportsTextureSampleCount:i])
      config->backend_info.AAModes.push_back(i);
  }

  if (char* env = getenv("MTL_UNIFIED_MEMORY"))
    g_features.unified_memory = env[0] == '1' || env[0] == 'y' || env[0] == 'Y';
  else if (@available(macOS 10.15, iOS 13.0, *))
    g_features.unified_memory = [device hasUnifiedMemory];
  else
    g_features.unified_memory = false;

  g_features.subgroup_ops = false;
  if (@available(macOS 10.15, iOS 13, *))
  {
    // Requires SIMD-scoped reduction operations
    g_features.subgroup_ops =
        [device supportsFamily:MTLGPUFamilyMac2] || [device supportsFamily:MTLGPUFamilyApple6];
    config->backend_info.bSupportsFramebufferFetch = [device supportsFamily:MTLGPUFamilyApple1];
  }
  if ([[device name] containsString:@"AMD"])
  {
    // Broken
    g_features.subgroup_ops = false;
  }
}

// clang-format off

AbstractTextureFormat Metal::Util::ToAbstract(MTLPixelFormat format)
{
  switch (format)
  {
  case MTLPixelFormatRGBA8Unorm:            return AbstractTextureFormat::RGBA8;
  case MTLPixelFormatBGRA8Unorm:            return AbstractTextureFormat::BGRA8;
#if TARGET_OS_OSX
  case MTLPixelFormatBC1_RGBA:              return AbstractTextureFormat::DXT1;
  case MTLPixelFormatBC2_RGBA:              return AbstractTextureFormat::DXT3;
  case MTLPixelFormatBC3_RGBA:              return AbstractTextureFormat::DXT5;
  case MTLPixelFormatBC7_RGBAUnorm:         return AbstractTextureFormat::BPTC;
#endif
  case MTLPixelFormatR16Unorm:              return AbstractTextureFormat::R16;
  case MTLPixelFormatDepth16Unorm:          return AbstractTextureFormat::D16;
#if TARGET_OS_OSX
  case MTLPixelFormatDepth24Unorm_Stencil8: return AbstractTextureFormat::D24_S8;
#endif
  case MTLPixelFormatR32Float:              return AbstractTextureFormat::R32F;
  case MTLPixelFormatDepth32Float:          return AbstractTextureFormat::D32F;
  case MTLPixelFormatDepth32Float_Stencil8: return AbstractTextureFormat::D32F_S8;
  default:                                  return AbstractTextureFormat::Undefined;
  }
}

MTLPixelFormat Metal::Util::FromAbstract(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::RGBA8:     return MTLPixelFormatRGBA8Unorm;
  case AbstractTextureFormat::BGRA8:     return MTLPixelFormatBGRA8Unorm;
#if TARGET_OS_OSX
  case AbstractTextureFormat::DXT1:      return MTLPixelFormatBC1_RGBA;
  case AbstractTextureFormat::DXT3:      return MTLPixelFormatBC2_RGBA;
  case AbstractTextureFormat::DXT5:      return MTLPixelFormatBC3_RGBA;
  case AbstractTextureFormat::BPTC:      return MTLPixelFormatBC7_RGBAUnorm;
#endif
  case AbstractTextureFormat::R16:       return MTLPixelFormatR16Unorm;
  case AbstractTextureFormat::D16:       return MTLPixelFormatDepth16Unorm;
#if TARGET_OS_OSX
  case AbstractTextureFormat::D24_S8:    return MTLPixelFormatDepth24Unorm_Stencil8;
#endif
  case AbstractTextureFormat::R32F:      return MTLPixelFormatR32Float;
  case AbstractTextureFormat::D32F:      return MTLPixelFormatDepth32Float;
  case AbstractTextureFormat::D32F_S8:   return MTLPixelFormatDepth32Float_Stencil8;
  default:                               return MTLPixelFormatInvalid;
  }
}

// clang-format on

// MARK: Shader Translation

static const std::string_view SHADER_HEADER = R"(
// Target GLSL 4.5.
#version 450 core
// Always available on Metal
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#define ATTRIBUTE_LOCATION(x) layout(location = x)
#define FRAGMENT_OUTPUT_LOCATION(x) layout(location = x)
#define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y) layout(location = x, index = y)
#define UBO_BINDING(packing, x) layout(packing, set = 0, binding = (x - 1))
#define SAMPLER_BINDING(x) layout(set = 1, binding = x)
#define TEXEL_BUFFER_BINDING(x) layout(set = 1, binding = (x + 8))
#define SSBO_BINDING(x) layout(std430, set = 2, binding = x)
#define INPUT_ATTACHMENT_BINDING(x, y, z) layout(set = x, binding = y, input_attachment_index = z)
#define VARYING_LOCATION(x) layout(location = x)
#define FORCE_EARLY_Z layout(early_fragment_tests) in

// Metal framebuffer fetch helpers.
#define FB_FETCH_VALUE subpassLoad(in_ocol0)

// hlsl to glsl function translation
#define API_METAL 1
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define frac fract
#define lerp mix

// These were changed in Vulkan
#define gl_VertexID gl_VertexIndex
#define gl_InstanceID gl_InstanceIndex
)";
static const std::string_view COMPUTE_SHADER_HEADER = R"(
// Target GLSL 4.5.
#version 450 core
// Always available on Metal
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

// All resources are packed into one descriptor set for compute.
#define UBO_BINDING(packing, x) layout(packing, set = 0, binding = (x - 1))
#define SSBO_BINDING(x) layout(std430, set = 2, binding = x)
#define IMAGE_BINDING(format, x) layout(format, set = 1, binding = x)

// hlsl to glsl function translation
#define API_METAL 1
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define frac fract
#define lerp mix
)";
static const std::string_view SUBGROUP_HELPER_HEADER = R"(
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_KHR_shader_subgroup_ballot : enable

#define SUPPORTS_SUBGROUP_REDUCTION 1
#define CAN_USE_SUBGROUP_REDUCTION true
#define IS_HELPER_INVOCATION gl_HelperInvocation
#define IS_FIRST_ACTIVE_INVOCATION (subgroupElect())
#define SUBGROUP_MIN(value) value = subgroupMin(value)
#define SUBGROUP_MAX(value) value = subgroupMax(value)
)";

static const std::string_view MSL_HEADER =
    // We know our shader generator leaves unused variables.
    "#pragma clang diagnostic ignored \"-Wunused-variable\"\n"
    // These are usually when the compiler doesn't think a switch is exhaustive
    "#pragma clang diagnostic ignored \"-Wreturn-type\"\n";

static constexpr spirv_cross::MSLResourceBinding
MakeResourceBinding(spv::ExecutionModel stage, u32 set, u32 binding,  //
                    u32 msl_buffer, u32 msl_texture, u32 msl_sampler)
{
  spirv_cross::MSLResourceBinding resource;
  resource.stage = stage;
  resource.desc_set = set;
  resource.binding = binding;
  resource.msl_buffer = msl_buffer;
  resource.msl_texture = msl_texture;
  resource.msl_sampler = msl_sampler;
  return resource;
}

std::optional<std::string> Metal::Util::TranslateShaderToMSL(ShaderStage stage,
                                                             std::string_view source)
{
  std::string full_source;

  std::string_view header = stage == ShaderStage::Compute ? COMPUTE_SHADER_HEADER : SHADER_HEADER;
  full_source.reserve(header.size() + SUBGROUP_HELPER_HEADER.size() + source.size());

  full_source.append(header);
  if (Metal::g_features.subgroup_ops)
    full_source.append(SUBGROUP_HELPER_HEADER);
  full_source.append(source);

  std::optional<SPIRV::CodeVector> code;
  switch (stage)
  {
  case ShaderStage::Vertex:
    code = SPIRV::CompileVertexShader(full_source, APIType::Metal, glslang::EShTargetSpv_1_3);
    break;
  case ShaderStage::Geometry:
    PanicAlertFmt("Tried to compile geometry shader for Metal, but Metal doesn't support them!");
    break;
  case ShaderStage::Pixel:
    code = SPIRV::CompileFragmentShader(full_source, APIType::Metal, glslang::EShTargetSpv_1_3);
    break;
  case ShaderStage::Compute:
    code = SPIRV::CompileComputeShader(full_source, APIType::Metal, glslang::EShTargetSpv_1_3);
    break;
  }
  if (!code.has_value())
    return std::nullopt;

  // clang-format off
  static const spirv_cross::MSLResourceBinding resource_bindings[] = {
      MakeResourceBinding(spv::ExecutionModelVertex,    0, 0, 1, 0, 0), // vs/ubo
      MakeResourceBinding(spv::ExecutionModelVertex,    0, 1, 1, 0, 0), // vs/ubo
      MakeResourceBinding(spv::ExecutionModelFragment,  0, 0, 0, 0, 0), // vs/ubo
      MakeResourceBinding(spv::ExecutionModelFragment,  0, 1, 1, 0, 0), // vs/ubo
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 0, 0, 0, 0), // ps/samp0
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 1, 0, 1, 1), // ps/samp1
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 2, 0, 2, 2), // ps/samp2
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 3, 0, 3, 3), // ps/samp3
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 4, 0, 4, 4), // ps/samp4
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 5, 0, 5, 5), // ps/samp5
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 6, 0, 6, 6), // ps/samp6
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 7, 0, 7, 7), // ps/samp7
      MakeResourceBinding(spv::ExecutionModelFragment,  1, 8, 0, 8, 8), // ps/samp8
      MakeResourceBinding(spv::ExecutionModelFragment,  2, 0, 2, 0, 0), // ps/ssbo
      MakeResourceBinding(spv::ExecutionModelGLCompute, 0, 1, 0, 0, 0), // cs/ubo
      MakeResourceBinding(spv::ExecutionModelGLCompute, 1, 0, 0, 0, 0), // cs/output_image
      MakeResourceBinding(spv::ExecutionModelGLCompute, 2, 0, 2, 0, 0), // cs/ssbo
      MakeResourceBinding(spv::ExecutionModelGLCompute, 2, 1, 3, 0, 0), // cs/ssbo
  };

  spirv_cross::CompilerMSL::Options options;
#if TARGET_OS_OSX
  options.platform = spirv_cross::CompilerMSL::Options::macOS;
#elif TARGET_OS_IOS
  options.platform = spirv_cross::CompilerMSL::Options::iOS;
#else
  #error What platform is this?
#endif
  // clang-format on

  spirv_cross::CompilerMSL compiler(std::move(*code));

  if (@available(macOS 11, iOS 14, *))
    options.set_msl_version(2, 3);
  else if (@available(macOS 10.15, iOS 13, *))
    options.set_msl_version(2, 2);
  else if (@available(macOS 10.14, iOS 12, *))
    options.set_msl_version(2, 1);
  else
    options.set_msl_version(2, 0);
  options.use_framebuffer_fetch_subpasses = true;
  compiler.set_msl_options(options);

  for (auto& binding : resource_bindings)
    compiler.add_msl_resource_binding(binding);

  std::string msl(MSL_HEADER);
  msl += compiler.compile();
  return msl;
}
