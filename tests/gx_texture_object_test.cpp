#include <dolphin/gx.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  static_assert(std::is_same_v<_GXTexObj, GXTexObj>);
  static_assert(std::is_same_v<_GXTlutObj, GXTlutObj>);
  try {
    alignas(32) std::array<std::uint8_t, 64> image{};
    GXTexObj object{};
    GXInitTexObj(&object, image.data(), 64, 32, GX_TF_RGB565, GX_REPEAT, GX_MIRROR, GX_TRUE);
    void* data = nullptr;
    u16 width = 0, height = 0;
    GXTexFmt format{};
    GXTexWrapMode wrapS{}, wrapT{};
    GXBool mipmap{};
    GXGetTexObjAll(&object, &data, &width, &height, &format, &wrapS, &wrapT, &mipmap);
    require(data == image.data() && width == 64 && height == 32 && format == GX_TF_RGB565 &&
                wrapS == GX_REPEAT && wrapT == GX_MIRROR && mipmap == GX_TRUE,
            "texture identity, dimensions, format, or wrapping changed");

    struct BiasCase { float input; float expected; };
    constexpr std::array biases{BiasCase{-5.f, -4.f}, BiasCase{-1.07f, -34.f / 32.f},
                               BiasCase{0.f, 0.f}, BiasCase{1.07f, 34.f / 32.f}, BiasCase{4.f, 127.f / 32.f}};
    for (u32 filter = GX_NEAR; filter <= GX_LIN_MIP_LIN; ++filter) {
      for (u32 mag = GX_NEAR; mag <= GX_LINEAR; ++mag) {
        for (const auto bias : biases) {
          GXInitTexObjLOD(&object, static_cast<GXTexFilter>(filter), static_cast<GXTexFilter>(mag),
                          1.1f, 12.f, bias.input, GX_TRUE, GX_FALSE, GX_ANISO_4);
          GXTexFilter minFilter{}, magFilter{};
          f32 minLod{}, maxLod{}, lodBias{};
          GXBool biasClamp{}, edgeLod{};
          GXAnisotropy aniso{};
          GXGetTexObjLODAll(&object, &minFilter, &magFilter, &minLod, &maxLod, &lodBias,
                            &biasClamp, &edgeLod, &aniso);
          require(minFilter == filter && magFilter == mag, "GX minification/magnification filter did not round trip");
          require(minLod == 17.f / 16.f && maxLod == 10.f && lodBias == bias.expected,
                  "LOD clamping or signed fixed-point quantization differs from GX");
          require(biasClamp == GX_TRUE && edgeLod == GX_FALSE && aniso == GX_ANISO_4,
                  "LOD flags changed");
          GXInitTexObjLODBias(&object, -1.07f);
          GXInitTexObjEdgeLOD(&object, GX_TRUE);
          GXInitTexObjBiasClamp(&object, GX_FALSE);
          GXGetTexObjLODAll(&object, &minFilter, &magFilter, &minLod, &maxLod, &lodBias,
                            &biasClamp, &edgeLod, &aniso);
          require(lodBias == -34.f / 32.f && edgeLod == GX_TRUE && biasClamp == GX_FALSE,
                  "individual sampler setters disagree with aggregate getter");
        }
      }
    }
    std::cout << "GX texture identity and 60 sampler combinations passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
