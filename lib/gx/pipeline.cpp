#include "pipeline.hpp"

#include "../gfx/encoding.hpp"
#include "../gfx/resources.hpp"
#include "../gfx/pipeline_cache.hpp"
#include "../gfx/resource_cache.hpp"

#include "gx_fmt.hpp"
#include "shader_info.hpp"

#include <tracy/Tracy.hpp>

namespace aurora::gx {

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  const auto shader = build_shader(config.shaderConfig);
  const auto label =
      fmt::format("GX Pipeline {:x} shader {:x}", xxh3_hash(config, static_cast<HashType>(gfx::ShaderType::GX)),
                  xxh3_hash(config.shaderConfig));
  return build_pipeline(config, {}, shader, label.c_str());
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass) {
  if (!gfx::bind_pipeline(data.pipeline, pass)) {
    return;
  }

  const auto& resources = gfx::detail::resources();
  pass.SetImmediates(0, &data.immediateData, sizeof(data.immediateData));
  const std::array offsets{data.uniformRange.offset};
  pass.SetBindGroup(1, resources.uniformBindGroup, offsets.size(), offsets.data());
  if (data.bindGroups.textureBindGroup) {
    pass.SetBindGroup(2, gfx::find_bind_group(data.bindGroups.textureBindGroup));
  }
  const bool disabledPolygonClipping = data.immediateData.triangleIndexStart != UINT32_MAX;
  if (disabledPolygonClipping) {
    const auto& size = data.clippingViewport.targetSize;
    pass.SetViewport(0, 0, size.x, size.y, 0.f, 1.f);
  } else if (data.geometry.indexCount != 0) {
    const auto& indices = data.geometry.idxRange;
    pass.SetIndexBuffer(resources.indexBuffer, wgpu::IndexFormat::Uint16, indices.offset, indices.size);
  }
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  if (disabledPolygonClipping) {
    pass.Draw(3, data.instanceCount);
    const auto& viewport = data.clippingViewport;
    pass.SetViewport(viewport.left, viewport.top, viewport.width, viewport.height, 0.f, 1.f);
  } else if (data.geometry.indexCount == 0) {
    pass.Draw(data.geometry.vtxCount, data.instanceCount);
  } else {
    pass.DrawIndexed(data.geometry.indexCount, data.instanceCount);
  }
}

} // namespace aurora::gx
