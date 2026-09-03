#include "gx_test_common.hpp"

#include "gx/texture_memory.hpp"
#include "gfx/texture_convert.hpp"
#include <dolphin/gd.h>
#include <dolphin/os.h>

#include <algorithm>
#include <array>

namespace {
class GDTextureTest : public GXFifoTest {
protected:
  void SetUp() override {
    GXFifoTest::SetUp();
    savedMem1Size = aurora::g_config.mem1Size;
    aurora::g_config.mem1Size = memory.size();
    MEM1Start = memory.data();
    MEM1End = memory.data() + memory.size();
    OSBaseAddress = reinterpret_cast<uintptr_t>(MEM1Start);
    void* arena = OSInitAlloc(memory.data() + 256, MEM1End, 1);
    ASSERT_NE(arena, nullptr);
    heap = OSCreateHeap(arena, MEM1End);
    ASSERT_GE(heap, 0);
  }

  void TearDown() override {
    GXFifoTest::TearDown(); // Drain consumers before releasing source allocations.
    GDSetCurrent(nullptr);
    for (auto* allocation : allocations) {
      OSFreeToHeap(heap, allocation);
    }
    OSDestroyHeap(heap);
    MEM1Start = MEM1End = nullptr;
    OSBaseAddress = 0;
    aurora::g_config.mem1Size = savedMem1Size;
  }

  u8* allocate(u32 size) {
    auto* data = static_cast<u8*>(OSAllocFromHeap(heap, size));
    EXPECT_NE(data, nullptr);
    if (data != nullptr) {
      allocations.push_back(data);
      EXPECT_EQ(OSPhysicalToCached(OSCachedToPhysical(data)), data);
    }
    return data;
  }

  template <typename Writer>
  std::vector<u8> record(Writer writer) {
    alignas(32) std::array<u8, 512> bytes{};
    GDLObj dl;
    GDInitGDLObj(&dl, bytes.data(), bytes.size());
    GDLObj* previous = __GDCurrentDL;
    GDSetCurrent(&dl);
    writer();
    const auto size = GDGetCurrOffset();
    GDSetCurrent(previous);
    return {bytes.begin(), bytes.begin() + size};
  }

  void replay(const std::vector<u8>& bytes) {
    GXCallDisplayList(bytes.data(), static_cast<u32>(bytes.size()));
    decode_fifo(capture_fifo());
  }

  alignas(32) std::array<u8, 128 * 1024> memory{};
  std::vector<void*> allocations;
  OSHeapHandle heap = -1;
  u32 savedMem1Size{};
};

TEST_F(GDTextureTest, AllMapsDecodeRetainedPhysicalAddressesAndOriginalTwentyByteLayout) {
  auto* image = allocate(4096);
  for (u32 map = 0; map < GX_MAX_TEXMAP; ++map) {
    const auto id = static_cast<GXTexMapID>(map);
    const auto bytes = record([&] {
      GDSetTexImgPtr(id, image + map * 32);
      GDSetTexImgAttr(id, 16, 8, GX_TF_RGBA8);
      GDSetTexLookupMode(id, GX_REPEAT, GX_MIRROR, GX_LIN_MIP_LIN, GX_LINEAR,
                         0.5f, 2.25f, -0.25f, true, true, GX_ANISO_2);
    });
    ASSERT_EQ(bytes.size(), 20u);
    replay(bytes);
    const auto& slot = gxState().loadedTextures[map];
    EXPECT_EQ(slot.data, image + map * 32);
    EXPECT_TRUE(slot.is_bp_texture());
    EXPECT_EQ(slot.width(), 16u);
    EXPECT_EQ(slot.height(), 8u);
    EXPECT_EQ(slot.format(), GX_TF_RGBA8);
    EXPECT_EQ(slot.wrap_s(), GX_REPEAT);
    EXPECT_EQ(slot.wrap_t(), GX_MIRROR);
    EXPECT_EQ(slot.min_filter(), GX_LIN_MIP_LIN);
    EXPECT_FLOAT_EQ(slot.max_lod(), 2.25f);
    EXPECT_EQ(slot.mip_count(), 4u); // ceil(max LOD), including base level
    EXPECT_EQ(slot.texObjId, 0u);
  }
}

TEST_F(GDTextureTest, PointerPatchSurvivesByteCopyAndPreservesFollowingCommands) {
  auto* source = allocate(256);
  const auto bytes = record([&] {
    GDSetTexImgPtr(GX_TEXMAP4, source);
    GDSetTexImgAttr(GX_TEXMAP4, 8, 4, GX_TF_I8);
    const auto end = GDGetCurrOffset();
    GDSetCurrOffset(2);
    GDPatchTexImgPtr(source + 64);
    EXPECT_EQ(GDGetCurrOffset(), 5u);
    GDSetCurrOffset(end);
  });
  ASSERT_EQ(bytes.size(), 10u);
  EXPECT_EQ(bytes[5], GX_LOAD_BP_REG);
  EXPECT_EQ(bytes[6], 0xA8u);
  auto copy = bytes;
  replay(copy);
  EXPECT_EQ(gxState().loadedTextures[4].data, source + 64);
  EXPECT_EQ(gxState().loadedTextures[4].width(), 8u);
}

TEST_F(GDTextureTest, RawAddressIsThirtyTwoByteUnitsAndZeroIsARealPhysicalAddress) {
  for (u32 address : {0u, 32u, 160u}) {
    const auto bytes = record([&] { GDSetTexImgPtrRaw(GX_TEXMAP0, address >> 5); });
    ASSERT_EQ(bytes.size(), 5u);
    replay(bytes);
    EXPECT_EQ(gxState().loadedTextures[0].data, memory.data() + address);
  }
}

TEST_F(GDTextureTest, PhysicalRangeChecksWholeExtentIncludingLastByte) {
  using aurora::gx::physical_texture_range;
  EXPECT_EQ(physical_texture_range(0, memory.size()), memory.data());
  EXPECT_EQ(physical_texture_range(memory.size() - 32, 32), memory.data() + memory.size() - 32);
  EXPECT_EQ(physical_texture_range(memory.size() - 32, 33), nullptr);
  EXPECT_EQ(physical_texture_range(memory.size(), 0), nullptr);
  EXPECT_EQ(physical_texture_range(UINT32_MAX, 32), nullptr);
}

TEST_F(GDTextureTest, PaletteLoadUsesThirtyByteCommandsAndSnapshotsSourceAtExecution) {
  auto* palette = allocate(32);
  std::fill_n(palette, 32, 0xAB);
  const auto load = record([&] { GDLoadTlut(palette, 0xF0000, GX_TLUT_16); });
  ASSERT_EQ(load.size(), 30u);
  const auto select = record([&] {
    GDSetTexImgAttr(GX_TEXMAP6, 8, 8, static_cast<GXTexFmt>(GX_TF_C4));
    GDSetTexTlut(GX_TEXMAP6, 0xF0000, GX_TL_RGB565);
  });
  ASSERT_EQ(select.size(), 10u);
  replay(select); // Selection may precede the load.
  replay(load);
  std::fill_n(palette, 32, 0xCD);
  auto tlut = aurora::gx::loaded_tlut(gxState().loadedTextures[6]);
  ASSERT_TRUE(tlut);
  EXPECT_EQ(tlut->numEntries, 16u);
  EXPECT_EQ(tlut->format, GX_TL_RGB565);
  EXPECT_EQ(static_cast<const u8*>(tlut->data)[0], 0xAB);
  EXPECT_EQ(static_cast<const u8*>(tlut->data)[31], 0xAB);
  EXPECT_NE(tlut->data, palette);
  replay(load); // Identical trigger bits must still perform another transfer.
  tlut = aurora::gx::loaded_tlut(gxState().loadedTextures[6]);
  ASSERT_TRUE(tlut);
  EXPECT_EQ(static_cast<const u8*>(tlut->data)[0], 0xCD);
  EXPECT_EQ(static_cast<const u8*>(tlut->data)[31], 0xCD);
}

TEST_F(GDTextureTest, OverlappingLoadsPreserveUnwrittenTmemAndBindingsSelectTheirOwnFormat) {
  auto* palette = allocate(1024);
  std::fill_n(palette, 1024, 0x11);
  auto* replacement = allocate(32);
  std::fill_n(replacement, 32, 0x22);
  replay(record([&] {
    GDLoadTlut(palette, 0xC0000, static_cast<GXTlutSize>(32));
    GDSetTexImgAttr(GX_TEXMAP0, 8, 4, static_cast<GXTexFmt>(GX_TF_C8));
    GDSetTexTlut(GX_TEXMAP0, 0xC0200, GX_TL_IA8);
    GDSetTexImgAttr(GX_TEXMAP7, 8, 4, static_cast<GXTexFmt>(GX_TF_C8));
    GDSetTexTlut(GX_TEXMAP7, 0xC0200, GX_TL_RGB5A3);
    GDLoadTlut(replacement, 0xC0200, GX_TLUT_16);
  }));
  for (u32 map : {0u, 7u}) {
    const auto tlut = aurora::gx::loaded_tlut(gxState().loadedTextures[map]);
    ASSERT_TRUE(tlut);
    EXPECT_EQ(tlut->numEntries, 256u);
    EXPECT_EQ(tlut->format, map == 0 ? GX_TL_IA8 : GX_TL_RGB5A3);
    const auto* data = static_cast<const u8*>(tlut->data);
    EXPECT_EQ(data[0], 0x22);
    EXPECT_EQ(data[31], 0x22);
    EXPECT_EQ(data[32], 0x11);
    EXPECT_EQ(data[511], 0x11);
  }
  EXPECT_EQ(gxState().textureMemory[0x40000], 0x11);
}

TEST_F(GDTextureTest, C14PaletteUsesElevenBitLineCountAndCompleteThirtyTwoKiBTransfer) {
  auto* palette = allocate(32768);
  std::fill_n(palette, 32768, 0x5A);
  palette[32767] = 0xE7;
  replay(record([&] {
    GDLoadTlut(palette, 0xF0000, GX_TLUT_16K);
    GDSetTexImgAttr(GX_TEXMAP2, 4, 4, static_cast<GXTexFmt>(GX_TF_C14X2));
    GDSetTexTlut(GX_TEXMAP2, 0xF0000, GX_TL_RGB5A3);
  }));
  const auto tlut = aurora::gx::loaded_tlut(gxState().loadedTextures[2]);
  ASSERT_TRUE(tlut);
  EXPECT_EQ(tlut->numEntries, 16384u);
  EXPECT_EQ(static_cast<const u8*>(tlut->data)[0], 0x5A);
  EXPECT_EQ(static_cast<const u8*>(tlut->data)[32767], 0xE7);
}

TEST_F(GDTextureTest, NativeMetadataAndRawDisplayListsRestoreIndependentSourcesAndCacheIdentity) {
  auto* physical = allocate(256);
  std::array<u8, 256> native{};
  GXTexObj object{};
  GXInitTexObj(&object, native.data(), 8, 8, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
  const auto raw = record([&] {
    GDSetTexImgPtr(GX_TEXMAP0, physical);
    GDSetTexImgAttr(GX_TEXMAP0, 16, 8, GX_TF_I4);
  });
  replay(raw);
  GXLoadTexObj(&object, GX_TEXMAP0);
  decode_fifo(capture_fifo());
  auto& slot = gxState().loadedTextures[0];
  EXPECT_EQ(slot.data, native.data());
  EXPECT_NE(slot.texObjId, 0u);
  EXPECT_FALSE(slot.is_bp_texture());
  replay(raw);
  EXPECT_EQ(slot.data, physical);
  EXPECT_EQ(slot.width(), 16u);
  EXPECT_EQ(slot.texObjId, 0u);
  EXPECT_TRUE(slot.is_bp_texture());
  GXLoadTexObj(&object, GX_TEXMAP0);
  decode_fifo(capture_fifo());
  EXPECT_EQ(slot.data, native.data());
  EXPECT_NE(slot.texObjId, 0u);
  EXPECT_EQ(slot.tlutRegion, UINT32_MAX);
  GXDestroyTexObj(&object);
}

TEST_F(GDTextureTest, OriginalPaletteCommandsProduceExpectedRgbaPixels) {
  auto* indices = allocate(32);
  std::fill_n(indices, 32, 0x01);
  auto* palette = allocate(32);
  std::fill_n(palette, 32, 0);
  palette[0] = 0xF8; // Big-endian RGB565 red.
  palette[2] = 0x07;
  palette[3] = 0xE0; // Big-endian RGB565 green.
  replay(record([&] {
    GDSetTexImgPtr(GX_TEXMAP0, indices);
    GDSetTexImgAttr(GX_TEXMAP0, 8, 8, static_cast<GXTexFmt>(GX_TF_C4));
    GDLoadTlut(palette, 0xF0000, GX_TLUT_16);
    GDSetTexTlut(GX_TEXMAP0, 0xF0000, GX_TL_RGB565);
  }));
  const auto& image = gxState().loadedTextures[0];
  const auto tlut = aurora::gx::loaded_tlut(image);
  ASSERT_TRUE(tlut);
  const auto pixels = aurora::gfx::convert_texture_palette(
      image.format(), image.width(), image.height(), 1,
      {static_cast<const u8*>(image.data), 32}, tlut->format, tlut->numEntries,
      {static_cast<const u8*>(tlut->data), 32});
  ASSERT_EQ(pixels.data.size(), 8u * 8u * 4u);
  const std::array<u8, 8> expected{255, 0, 0, 255, 0, 255, 0, 255};
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), pixels.data.data()));
}

TEST_F(GDTextureTest, NativePaletteMetadataDoesNotPerformAFalsePhysicalMemoryTransfer) {
  std::array<u16, 16> native{};
  GXTlutObj palette{};
  GXInitTlutObj(&palette, native.data(), GX_TL_RGB565, native.size());
  GXLoadTlut(&palette, GX_TLUT3);
  decode_fifo(capture_fifo());
  EXPECT_TRUE(gxState().textureMemory.empty());
  EXPECT_EQ(gxState().loadedTluts[3].data, native.data());
  EXPECT_EQ(gxState().loadedTluts[3].numEntries, native.size());
  GXDestroyTlutObj(&palette);
}

TEST_F(GDTextureTest, RawMipCountRoundsFractionalLodUpAndClampsAtOneTexel) {
  replay(record([&] {
    GDSetTexImgPtrRaw(GX_TEXMAP0, 0);
    GDSetTexImgAttr(GX_TEXMAP0, 8, 4, GX_TF_I4);
    GDSetTexLookupMode(GX_TEXMAP0, GX_CLAMP, GX_CLAMP, GX_NEAR_MIP_NEAR,
                       GX_NEAR, 0, 10, 0, false, false, GX_ANISO_1);
  }));
  EXPECT_EQ(gxState().loadedTextures[0].mip_count(), 4u);
  replay(record([&] {
    GDSetTexLookupMode(GX_TEXMAP0, GX_CLAMP, GX_CLAMP, GX_LINEAR,
                       GX_LINEAR, 0, 10, 0, false, false, GX_ANISO_1);
  }));
  EXPECT_EQ(gxState().loadedTextures[0].mip_count(), 1u);
}
} // namespace
