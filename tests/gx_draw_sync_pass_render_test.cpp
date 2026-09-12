#include <aurora/aurora.h>
#include <aurora/gfx.hpp>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include "../lib/gfx/recording.hpp"
#include "../lib/webgpu/map_future.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr u16 Width = 256;
constexpr u16 Height = 192;
constexpr u32 Stride = Width * 4;
u32 callbackDepth = GX_MAX_Z24;
u16 callbackToken = 0;
size_t callbackCount = 0;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int length) {
  if (level < LOG_WARNING) return;
  std::cerr << "[aurora:" << (module ? module : "unknown") << "] "
            << std::string_view(message ? message : "", length) << '\n';
}

struct AuroraLifetime {
  ~AuroraLifetime() { aurora_shutdown(); }
};

void draw_sync_callback(u16 token) {
  callbackToken = token;
  GXPeekZ(Width / 2, Height / 2, &callbackDepth);
  ++callbackCount;
}

void configure_draw_state() {
  constexpr float projection[4][4] = {
      {1.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, -1.0F, -1.0F},
      {0.0F, 0.0F, 0.0F, 1.0F},
  };
  constexpr float identity[3][4] = {
      {1.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, 1.0F, 0.0F},
  };

  GXSetProjection(projection, GX_ORTHOGRAPHIC);
  GXLoadPosMtxImm(identity, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);
  GXSetViewport(0.0F, 0.0F, static_cast<float>(Width), static_cast<float>(Height), 0.0F, 1.0F);
  GXSetScissor(0, 0, Width, Height);
  GXSetCullMode(GX_CULL_NONE);
  GXSetClipMode(GX_CLIP_ENABLE);
  GXSetCoPlanar(GX_DISABLE);
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetZCompLoc(GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetDither(GX_FALSE);
  GXSetDstAlpha(GX_FALSE, 0);
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetNumTexGens(0);
  GXSetNumIndStages(0);
  GXSetNumTevStages(1);
  GXSetTevDirect(GX_TEVSTAGE0);
  GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

void draw_fullscreen(float z, GXColor color) {
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition3f32(-1.0F, 1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(1.0F, 1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(1.0F, -1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(-1.0F, -1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXEnd();
}


std::vector<u8> read_color(const wgpu::Texture& texture) {
  const auto device = aurora::gfx::device();
  const wgpu::BufferDescriptor bufferDesc{
      .label = "Split pass readback",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead,
      .size = Stride * Height,
  };
  const auto buffer = device.CreateBuffer(&bufferDesc);
  const auto encoder = device.CreateCommandEncoder();
  const wgpu::TexelCopyTextureInfo source{.texture = texture};
  const wgpu::TexelCopyBufferInfo destination{
      .layout = {.offset = 0, .bytesPerRow = Stride, .rowsPerImage = Height},
      .buffer = buffer,
  };
  const wgpu::Extent3D extent{Width, Height, 1};
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  const auto commands = encoder.Finish();
  aurora::gfx::queue().Submit(1, &commands);
  bool mapped = false;
  auto future = buffer.MapAsync(wgpu::MapMode::Read, 0, Stride * Height, wgpu::CallbackMode::WaitAnyOnly,
                               [&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                 mapped = status == wgpu::MapAsyncStatus::Success;
                               });
  aurora::webgpu::complete_future(future, true);
  require(mapped, "split-pass color readback must complete");
  std::vector<u8> result(Stride * Height);
  std::memcpy(result.data(), buffer.GetConstMappedRange(0, result.size()), result.size());
  buffer.Unmap();
  return result;
}

wgpu::RenderPipeline make_pipeline(const wgpu::Device& device, const wgpu::ShaderModule& module,
                                   const char* vertexEntry, const char* fragmentEntry, bool writeStencil) {
  const wgpu::ColorTargetState color{.format = wgpu::TextureFormat::RGBA8Unorm};
  const wgpu::FragmentState fragment{.module = module, .entryPoint = fragmentEntry,
                                     .targetCount = 1, .targets = &color};
  const wgpu::StencilFaceState stencil{
      .compare = writeStencil ? wgpu::CompareFunction::Always : wgpu::CompareFunction::Equal,
      .failOp = wgpu::StencilOperation::Keep,
      .depthFailOp = wgpu::StencilOperation::Keep,
      .passOp = writeStencil ? wgpu::StencilOperation::Replace : wgpu::StencilOperation::Keep,
  };
  const wgpu::DepthStencilState depth{
      .format = wgpu::TextureFormat::Depth24PlusStencil8,
      .depthWriteEnabled = true,
      .depthCompare = wgpu::CompareFunction::Less,
      .stencilFront = stencil,
      .stencilBack = stencil,
      .stencilReadMask = 0xff,
      .stencilWriteMask = writeStencil ? 0xffu : 0u,
  };
  const wgpu::PipelineLayoutDescriptor layoutDesc{};
  const auto layout = device.CreatePipelineLayout(&layoutDesc);
  const wgpu::RenderPipelineDescriptor descriptor{
      .label = fragmentEntry,
      .layout = layout,
      .vertex = {.module = module, .entryPoint = vertexEntry},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .depthStencil = &depth,
      .fragment = &fragment,
  };
  return device.CreateRenderPipeline(&descriptor);
}

struct SplitPassDraws {
  std::array<wgpu::RenderPipeline, 3> pipelines;
};

void encode_split_draw(const aurora::gfx::DrawContext&, const wgpu::RenderPassEncoder& pass,
                       const void* payload, size_t payloadSize, void* userdata) {
  if (payloadSize != sizeof(u32)) std::abort();
  u32 index;
  std::memcpy(&index, payload, sizeof(index));
  auto& draws = *static_cast<SplitPassDraws*>(userdata);
  if (index >= draws.pipelines.size()) std::abort();
  pass.SetPipeline(draws.pipelines[index]);
  pass.SetStencilReference(7);
  // Green only covers the right half. The untouched left half independently
  // proves that prefix color contents survive the continuation's Load.
  pass.SetScissorRect(index == 2 ? Width / 2 : 0, 0, index == 2 ? Width / 2 : Width, Height);
  pass.Draw(3);
}

void prove_discarded_attachment_continuation() {
  namespace gfx = aurora::gfx;
  const auto device = gfx::device();
  const wgpu::TextureDescriptor colorDesc{
      .label = "Split pass color",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
      .size = {Width, Height, 1},
      .format = wgpu::TextureFormat::RGBA8Unorm,
  };
  const auto color = device.CreateTexture(&colorDesc);
  const wgpu::TextureDescriptor depthDesc{
      .label = "Split pass depth and stencil",
      .usage = wgpu::TextureUsage::RenderAttachment,
      .size = {Width, Height, 1},
      .format = wgpu::TextureFormat::Depth24PlusStencil8,
  };
  const auto depth = device.CreateTexture(&depthDesc);
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = R"(
fn position(index: u32, z: f32) -> vec4f {
  let vertices = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
  return vec4f(vertices[index], z, 1);
}
@vertex fn prefix(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return position(i, 0.25); }
@vertex fn behind(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return position(i, 0.75); }
@vertex fn ahead(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f { return position(i, 0.1); }
@fragment fn red() -> @location(0) vec4f { return vec4f(1, 0, 0, 1); }
@fragment fn blue() -> @location(0) vec4f { return vec4f(0, 0, 1, 1); }
@fragment fn green() -> @location(0) vec4f { return vec4f(0, 1, 0, 1); }
)";
  const wgpu::ShaderModuleDescriptor shaderDesc{.nextInChain = &wgsl, .label = "Split pass proof"};
  const auto shader = device.CreateShaderModule(&shaderDesc);
  SplitPassDraws draws{{make_pipeline(device, shader, "prefix", "red", true),
                       make_pipeline(device, shader, "behind", "blue", false),
                       make_pipeline(device, shader, "ahead", "green", false)}};
  const auto drawType = gfx::register_draw_type({.label = "Split pass proof", .draw = encode_split_draw, .userdata = &draws});
  const gfx::ColorPassDescriptor descriptor{
      .label = "Final-discard pass interrupted by GX tokens",
      .colorView = color.CreateView(),
      .colorFormat = colorDesc.format,
      .depthStencilView = depth.CreateView(),
      .depthStencilFormat = depthDesc.format,
      .targetSize = colorDesc.size,
      .colorLoadOp = wgpu::LoadOp::Clear,
      .colorStoreOp = wgpu::StoreOp::Discard,
      .clearColor = {0, 0, 0, 1},
      .hasDepth = true,
      .depthLoadOp = wgpu::LoadOp::Clear,
      .depthStoreOp = wgpu::StoreOp::Discard,
      .depthClearValue = 1,
      .hasStencil = true,
      .stencilLoadOp = wgpu::LoadOp::Clear,
      .stencilStoreOp = wgpu::StoreOp::Discard,
      .stencilClearValue = 0,
  };
  gfx::begin_color_pass(descriptor);
  u32 index = 0;
  require(gfx::push_custom_draw(drawType, &index, sizeof(index)), "prefix draw must be recorded");
  GXSetDrawSync(0x3010);
  GXDrawDone();
  require(GXReadDrawSync() == 0x3010, "prefix token must complete before the continuation");

  index = 1;
  require(gfx::push_custom_draw(drawType, &index, sizeof(index)), "occluded draw must be recorded");
  index = 2;
  require(gfx::push_custom_draw(drawType, &index, sizeof(index)), "stencil-tested draw must be recorded");
  GXSetDrawSync(0x3011);
  GXDrawDone();
  require(GXReadDrawSync() == 0x3011, "continuation token must complete before readback");
  const auto pixels = read_color(color);
  for (u32 y = Height / 4; y < Height * 3 / 4; ++y) {
    for (u32 x = Width / 4; x < Width * 3 / 4; ++x) {
      const auto offset = y * Stride + x * 4;
      const std::array<u8, 4> expected = x < Width / 2 ? std::array<u8, 4>{255, 0, 0, 255}
                                                      : std::array<u8, 4>{0, 255, 0, 255};
      require(std::memcmp(pixels.data() + offset, expected.data(), 4) == 0,
              "token split must preserve color, reject farther depth, and retain stencil reference 7");
    }
  }
  // Read before the logical pass ends: its requested final Discard remains
  // valid, while every intermediate token must temporarily Store attachments.
  gfx::end_color_pass();
  gfx::complete_draw();
  gfx::unregister_draw_type(drawType);
}

void prove_offscreen_token_order() {
  configure_draw_state();
  draw_fullscreen(-0.375F, GXColor{232, 24, 24, 255});
  // Opening this pass must preserve the earlier EFB work in command order,
  // even though the EFB has not yet acquired an explicit resolve consumer.
  require(aurora::gfx::create_pass(Width, Height), "offscreen pass must open");
  draw_fullscreen(-0.125F, GXColor{24, 232, 24, 255});
  GXSetDrawSyncCallback(draw_sync_callback);
  GXSetDrawSync(0x3001);
  GXDrawDone();
  require(callbackCount == 1 && callbackToken == 0x3001, "offscreen token must invoke its callback");
  const u32 offscreenTokenEfbDepth = callbackDepth;
  require(offscreenTokenEfbDepth > GX_MAX_Z24 / 3 && offscreenTokenEfbDepth < GX_MAX_Z24 / 2,
          "offscreen token must observe the preceding EFB draw, not clear depth or offscreen depth");
  aurora::gfx::ResolvedTargets unused;
  require(aurora::gfx::resolve_pass({.color = false, .depth = false}, unused), "offscreen pass must close");
  GXSetDrawSync(0x3002);
  GXDrawDone();
  require(callbackCount == 2 && callbackToken == 0x3002 && callbackDepth == offscreenTokenEfbDepth,
          "restoring EFB must retain the same pre-offscreen contents");
  GXSetDrawSyncCallback(nullptr);
}

void prove_gpu_continuation() {
  AuroraConfig config{};
  config.appName = "Aurora draw-sync pass continuation proof";
  config.cachePath = std::getenv("AURORA_TEST_CACHE_PATH");
#if defined(__APPLE__)
  config.desiredBackend = BACKEND_METAL;
#else
  config.desiredBackend = BACKEND_VULKAN;
#endif
  config.allowCpuAdapter = true;
  config.windowWidth = Width;
  config.windowHeight = Height;
  config.vsync = false;
  config.pauseOnFocusLost = false;
  config.logCallback = log_callback;
  config.logLevel = LOG_WARNING;
  const auto init = aurora_initialize(0, nullptr, &config);
  const AuroraLifetime lifetime;
  require(init.backend == config.desiredBackend, "the proof requires the requested actual GPU backend");
  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  GXInit(nullptr, 0);
  VISetFrameBufferScale(1);
  GXRenderModeObj renderMode{};
  renderMode.viTVmode = VI_TVMODE_NTSC_PROG;
  renderMode.fbWidth = renderMode.viWidth = Width;
  renderMode.efbHeight = renderMode.xfbHeight = renderMode.viHeight = Height;
  renderMode.xFBmode = VI_XFBMODE_SF;
  VIConfigure(&renderMode);
  aurora_update();
  require(aurora_begin_frame(), "Aurora must acquire a GPU frame");
  GXSetCopyClear(GXColor{16, 16, 16, 255}, GX_MAX_Z24);
  GXSetDispCopySrc(0, 0, Width, Height);
  GXSetDispCopyDst(Width, Height);
  GXSetDispCopyYScale(1);
  prove_offscreen_token_order();
  GXCopyDisp(nullptr, GX_TRUE);
  prove_discarded_attachment_continuation();
  aurora_end_frame();
  aurora::gfx::synchronize();
}
} // namespace

int main() {
  try {
    prove_gpu_continuation();
    std::cout << "[ok] offscreen draw-sync callback observes preceding EFB GPU depth\n";
    std::cout << "[ok] GPU completion preserves color, depth and stencil across final-discard pass splits\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[fail] draw-sync pass continuation: " << exception.what() << '\n';
    return 1;
  }
}
