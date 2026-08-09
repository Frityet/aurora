#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <RVLFaceLib.h>

#include <aurora/rfl/ResourceArchive.hpp>

namespace aurora::rfl {

enum class CharacterModelError {
  None,
  InvalidDefaultIndex,
  InvalidResolution,
  InvalidExpressionFlags,
  GpuFrameInactive,
  GpuCopyFailed,
  ResourceMissing,
  ResourceMalformed,
  DisplayListOverflow,
};

struct CharacterModelStats {
  std::array<std::size_t, 9> shape_vertex_counts{};
  std::array<std::size_t, 9> shape_primitive_counts{};
  std::size_t expression_count = 0;
  std::size_t mask_texture_bytes = 0;
  std::size_t display_list_bytes = 0;
};

class CharacterModel final {
public:
  CharacterModel(const CharacterModel&) = delete;
  CharacterModel& operator=(const CharacterModel&) = delete;
  CharacterModel(CharacterModel&&) = delete;
  CharacterModel& operator=(CharacterModel&&) = delete;
  ~CharacterModel();

  [[nodiscard]] static std::unique_ptr<CharacterModel> create_default(const ResourceArchive& archive,
                                                                      std::uint16_t index, RFLResolution resolution,
                                                                      std::uint32_t expression_flags,
                                                                      CharacterModelError& error);

  void set_matrix(const Mtx matrix);
  [[nodiscard]] bool set_expression(RFLExpression expression);
  [[nodiscard]] RFLExpression expression() const;
  [[nodiscard]] bool mask_ready(RFLExpression expression) const;
  [[nodiscard]] const CharacterModelStats& stats() const;

  void draw_opaque(const RFLDrawCoreSetting& setting) const;
  void draw_translucent(const RFLDrawCoreSetting& setting) const;

private:
  struct Impl;

  explicit CharacterModel(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> m_impl;
};

void load_vertex_setting(const RFLDrawCoreSetting& setting);
void load_material_setting(const RFLDrawCoreSetting& setting);

} // namespace aurora::rfl
