#include "gx_test_common.hpp"

#include "internal.hpp"
#include "dolphin/os/internal.hpp"
#include <dolphin/gd.h>
#include <dolphin/os.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

namespace aurora::gfx {
extern std::vector<u8> g_lastStorageUpload;
} // namespace aurora::gfx

namespace {
class GDArrayTest : public GXFifoTest {
protected:
  void SetUp() override {
    GXFifoTest::SetUp();
    savedMem1Size = aurora::g_config.mem1Size;
    aurora::g_config.mem1Size = sizeof(physicalMemory);
    MEM1Start = physicalMemory.data();
    MEM1End = physicalMemory.data() + physicalMemory.size();
    OSBaseAddress = reinterpret_cast<uintptr_t>(MEM1Start);
    aurora::gfx::g_lastStorageUpload.clear();
  }

  void TearDown() override {
    GXFifoTest::TearDown();
    GDSetCurrent(nullptr);
    MEM1Start = MEM1End = nullptr;
    OSBaseAddress = 0;
    aurora::g_config.mem1Size = savedMem1Size;
  }

  template <typename Writer>
  std::vector<u8> record(Writer writer, bool pad = false) {
    alignas(32) std::array<u8, 512> bytes{};
    GDLObj dl;
    GDInitGDLObj(&dl, bytes.data(), bytes.size());
    GDLObj* previous = __GDCurrentDL;
    GDSetCurrent(&dl);
    writer();
    if (pad) {
      GDPadCurr32();
    }
    const auto size = GDGetCurrOffset();
    GDSetCurrent(previous);
    return {bytes.begin(), bytes.begin() + size};
  }

  void replay(const std::vector<u8>& bytes) {
    GXCallDisplayList(bytes.data(), static_cast<u32>(bytes.size()));
    const auto fifo = capture_fifo();
    decode_fifo(fifo);
  }

  static void position_format() {
    const GXVtxDescList desc[] = {{GX_VA_POS, GX_INDEX8}, {GX_VA_NULL, GX_NONE}};
    const GXVtxAttrFmtList format[] = {
        {GX_VA_POS, GX_POS_XYZ, GX_F32, 0}, {GX_VA_NULL, GX_POS_XY, GX_U8, 0}};
    GDSetVtxDescv(desc);
    GDSetVtxAttrFmtv(GX_VTXFMT0, format);
  }

  static void position_draw(u8 index) {
    GDWrite_u8(GX_TRIANGLES);
    GDWrite_u16(1);
    GDWrite_u8(index);
  }

  alignas(32) std::array<u8, 512> physicalMemory{};
  u32 savedMem1Size{};
};

TEST_F(GDArrayTest, RetailWriterRetainsFullHostPointersForEveryArraySlot) {
  std::array<std::array<u8, 64>, 16> arrays{};
  if constexpr (sizeof(uintptr_t) > sizeof(u32)) {
    ASSERT_GT(reinterpret_cast<uintptr_t>(arrays.data()), std::numeric_limits<u32>::max());
  }
  for (u32 slot = 0; slot < arrays.size(); ++slot) {
    const GXAttr attr = static_cast<GXAttr>(GX_VA_POS + slot);
    const u8 stride = static_cast<u8>(slot + 5);
    const auto bytes = record([&] { GDSetArray(attr, arrays[slot].data(), stride); });
    ASSERT_EQ(bytes.size(), 22u);
    EXPECT_EQ(bytes[0], GX_AURORA);
    EXPECT_EQ(bytes[16], GX_LOAD_CP_REG);
    EXPECT_EQ(bytes[17], 0xB0 + slot);
    decode_fifo(bytes);
    const auto& decoded = gxState().arrays[attr];
    EXPECT_EQ(decoded.data, arrays[slot].data());
    EXPECT_EQ(decoded.stride, stride);
    EXPECT_FALSE(decoded.sizeKnown);
    EXPECT_EQ(decoded.size, 0u);
    EXPECT_TRUE(decoded.le);
  }
  std::array<float, 9> nbt{};
  decode_fifo(record([&] { GDSetArray(GX_VA_NBT, nbt.data(), sizeof(nbt)); }));
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].data, nbt.data());
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].stride, sizeof(nbt));
}

TEST_F(GDArrayTest, SizedWriterPreservesResourceEndiannessAndExtent) {
  std::array<u8, 60> resource{};
  for (bool littleEndian : {false, true}) {
    decode_fifo(record([&] { GDSetArraySized(GX_VA_TEX7, resource.data(), resource.size(), 6, littleEndian); }));
    const auto& decoded = gxState().arrays[GX_VA_TEX7];
    EXPECT_EQ(decoded.data, resource.data());
    EXPECT_EQ(decoded.size, resource.size());
    EXPECT_EQ(decoded.stride, 6u);
    EXPECT_TRUE(decoded.sizeKnown);
    EXPECT_EQ(decoded.le, littleEndian);
  }
}

TEST_F(GDArrayTest, RawWriterUsesActualPhysicalAddressMappingIncludingZero) {
  for (u32 physical : {0u, 32u, 160u}) {
    void* expected = physicalMemory.data() + physical;
    ASSERT_EQ(OSCachedToPhysical(expected), physical);
    decode_fifo(record([&] { GDSetArrayRaw(GX_VA_POS, physical, 12); }));
    const auto& decoded = gxState().arrays[GX_VA_POS];
    EXPECT_EQ(decoded.data, expected);
    EXPECT_EQ(decoded.stride, 12u);
    EXPECT_FALSE(decoded.sizeKnown);
    EXPECT_TRUE(decoded.le);
  }
}

TEST_F(GDArrayTest, PointerPatchPreservesSizedMetadataAndFollowingStride) {
  std::array<u8, 48> oldResource{}, newResource{};
  const auto bytes = record([&] {
    GDSetArraySized(GX_VA_NRM, oldResource.data(), oldResource.size(), 12, false);
    const u32 end = GDGetCurrOffset();
    GDSetCurrOffset(3);
    GDPatchArrayPtr(newResource.data());
    EXPECT_EQ(GDGetCurrOffset(), 11u);
    GDSetCurrOffset(end);
  });
  ASSERT_EQ(bytes.size(), 22u);
  decode_fifo(bytes);
  const auto& decoded = gxState().arrays[GX_VA_NRM];
  EXPECT_EQ(decoded.data, newResource.data());
  EXPECT_EQ(decoded.size, oldResource.size());
  EXPECT_EQ(decoded.stride, 12u);
  EXPECT_TRUE(decoded.sizeKnown);
  EXPECT_FALSE(decoded.le);
}

TEST_F(GDArrayTest, GXBaseOnlyWritePreservesEveryExistingStrideAndNativePointer) {
  std::array<std::array<u8, 64>, 16> before{}, after{};
  for (u32 slot = 0; slot < before.size(); ++slot) {
    const GXAttr attr = static_cast<GXAttr>(GX_VA_POS + slot);
    const u8 stride = static_cast<u8>(slot + 3);
    GXSetArraySized(attr, before[slot].data(), before[slot].size(), stride, false);
    decode_fifo(capture_fifo());
    GXSetArrayBase(attr, after[slot].data());
    const auto bytes = capture_fifo();
    EXPECT_EQ(bytes.size(), 16u);
    decode_fifo(bytes);
    const auto& decoded = gxState().arrays[attr];
    EXPECT_EQ(decoded.data, after[slot].data());
    EXPECT_EQ(decoded.stride, stride);
    EXPECT_FALSE(decoded.sizeKnown);
    EXPECT_TRUE(decoded.le);
  }
  GXSetArrayBase(GX_VA_NBT, before[0].data());
  decode_fifo(capture_fifo());
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].data, before[0].data());
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].stride, 4u);
}

TEST_F(GDArrayTest, NativeGDListReplaysIndexedDrawThroughActualDecoder) {
  const std::array<float, 9> positions{1, 2, 3, 11, 12, 13, 21, 22, 23};
  const auto bytes = record([&] {
    position_format();
    GDSetArray(GX_VA_POS, positions.data(), 12);
    position_draw(2);
  }, true);
  replay(bytes);
  const auto& decoded = gxState().arrays[GX_VA_POS];
  EXPECT_EQ(decoded.data, positions.data());
  EXPECT_EQ(decoded.requiredSize, sizeof(positions));
  ASSERT_EQ(aurora::gfx::g_lastStorageUpload.size(), sizeof(positions));
  EXPECT_EQ(0, std::memcmp(aurora::gfx::g_lastStorageUpload.data(), positions.data(), sizeof(positions)));
}

TEST_F(GDArrayTest, RawPhysicalArrayReplaysIndexedDrawFromItsResolvedOffset) {
  const std::array<float, 6> positions{5, 6, 7, -5, -6, -7};
  std::memcpy(physicalMemory.data() + 64, positions.data(), sizeof(positions));
  const auto bytes = record([&] {
    position_format();
    GDSetArrayRaw(GX_VA_POS, 64, 12);
    position_draw(1);
  }, true);
  replay(bytes);
  ASSERT_EQ(aurora::gfx::g_lastStorageUpload.size(), sizeof(positions));
  EXPECT_EQ(0, std::memcmp(aurora::gfx::g_lastStorageUpload.data(), positions.data(), sizeof(positions)));
}

TEST_F(GDArrayTest, PatchedNativePointerIsUsedByReplayWithoutTruncation) {
  const std::array<float, 3> original{1, 2, 3}, replacement{42, -7, 0.5f};
  const auto bytes = record([&] {
    position_format();
    const u32 arrayStart = GDGetCurrOffset();
    GDSetArray(GX_VA_POS, original.data(), 12);
    const u32 end = GDGetCurrOffset();
    GDSetCurrOffset(arrayStart + 3);
    GDPatchArrayPtr(replacement.data());
    GDSetCurrOffset(end);
    position_draw(0);
  }, true);
  replay(bytes);
  EXPECT_EQ(gxState().arrays[GX_VA_POS].data, replacement.data());
  ASSERT_EQ(aurora::gfx::g_lastStorageUpload.size(), sizeof(replacement));
  EXPECT_EQ(0, std::memcmp(aurora::gfx::g_lastStorageUpload.data(), replacement.data(), sizeof(replacement)));
}

TEST_F(GDArrayTest, BaseOnlyReplacementKeepsPaddedStrideForIndexedReplay) {
  struct Position {
    std::array<float, 3> value;
    float padding;
  };
  const std::array<Position, 2> before{}, after{{{{1, 2, 3}, 123}, {{4, 5, 6}, 456}}};
  const auto setup = record([&] {
    position_format();
    GDSetArray(GX_VA_POS, before.data(), sizeof(Position));
    position_draw(0);
  });
  replay(setup);
  GXSetArrayBase(GX_VA_POS, after.data());
  decode_fifo(capture_fifo());
  decode_fifo(std::vector<u8>{GX_TRIANGLES, 0, 1, 1});
  const auto& decoded = gxState().arrays[GX_VA_POS];
  EXPECT_EQ(decoded.stride, 16u);
  EXPECT_EQ(decoded.requiredSize, 28u); // second index + three float components
  ASSERT_EQ(aurora::gfx::g_lastStorageUpload.size(), 28u);
  EXPECT_EQ(0, std::memcmp(aurora::gfx::g_lastStorageUpload.data(), after.data(), 28));
}

TEST_F(GDArrayTest, IndexedMatrixArrayUsesNativeValuesAndPreservesStride) {
  std::array<float, 24> before{}, after{};
  for (u32 i = 0; i < 12; ++i) {
    after[12 + i] = static_cast<float>(i) * 1.25f - 7.0f;
  }
  decode_fifo(record([&] { GDSetArray(GX_POS_MTX_ARRAY, before.data(), 48); }));
  GXSetArrayBase(GX_POS_MTX_ARRAY, after.data());
  decode_fifo(capture_fifo());
  decode_fifo(record([&] { GDWriteXFIndxACmd(0, 12, 1); }));
  const float* actual = reinterpret_cast<const float*>(&gxState().pnMtx[0].pos);
  for (u32 i = 0; i < 12; ++i) {
    EXPECT_FLOAT_EQ(actual[i], after[12 + i]);
  }
  EXPECT_EQ(gxState().arrays[GX_POS_MTX_ARRAY].requiredSize, 96u);
}

TEST_F(GDArrayTest, TwelveArrayVcdVatListFits320ByteNativeBound) {
  const auto bytes = record([&] {
    const GXVtxDescList desc[] = {{GX_VA_POS, GX_INDEX16}, {GX_VA_NRM, GX_INDEX16},
                                {GX_VA_TEX0, GX_INDEX16}, {GX_VA_NULL, GX_NONE}};
    const GXVtxAttrFmtList format[] = {{GX_VA_POS, GX_POS_XYZ, GX_F32, 0},
                                     {GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0},
                                     {GX_VA_TEX0, GX_TEX_ST, GX_F32, 0},
                                     {GX_VA_NULL, GX_POS_XY, GX_U8, 0}};
    GDSetVtxDescv(desc);
    for (u32 slot = 0; slot < 12; ++slot) {
      GDSetArrayRaw(static_cast<GXAttr>(GX_VA_POS + slot), 0, 12);
    }
    GDSetVtxAttrFmtv(GX_VTXFMT0, format);
    EXPECT_EQ(GDGetCurrOffset(), 21u + 12u * 22u + 18u);
  }, true);
  EXPECT_EQ(bytes.size(), 320u);
  decode_fifo(bytes);
  for (u32 slot = 0; slot < 12; ++slot) {
    EXPECT_EQ(gxState().arrays[GX_VA_POS + slot].data, physicalMemory.data());
    EXPECT_EQ(gxState().arrays[GX_VA_POS + slot].stride, 12u);
  }
}
} // namespace
