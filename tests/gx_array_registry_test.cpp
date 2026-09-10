#include "gx_test_common.hpp"

#include <aurora/gx_array.hpp>
#include <dolphin/gd.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aurora::gfx {
extern std::vector<u8> g_lastStorageUpload;
extern uint32_t g_testStorageUploadCount;
} // namespace aurora::gfx

namespace {
using aurora::gx::ArrayRegistration;
using aurora::gx::find_registered_array;

TEST(GXArrayRegistryOwnership, BoundsAndEndianFollowActualSubarray) {
  std::array<u8, 96> bytes{};
  ArrayRegistration registration(bytes.data() + 16, 48, false);
  EXPECT_FALSE(find_registered_array(nullptr));
  EXPECT_FALSE(find_registered_array(bytes.data() + 15));
  ASSERT_TRUE(find_registered_array(bytes.data() + 16));
  EXPECT_EQ(find_registered_array(bytes.data() + 16)->size, 48u);
  EXPECT_FALSE(find_registered_array(bytes.data() + 16)->littleEndian);
  EXPECT_EQ(find_registered_array(bytes.data() + 28)->size, 36u);
  EXPECT_EQ(find_registered_array(bytes.data() + 63)->size, 1u);
  EXPECT_FALSE(find_registered_array(bytes.data() + 64));
  registration.reset();
  EXPECT_FALSE(find_registered_array(bytes.data() + 16));
}

TEST(GXArrayRegistryOwnership, MoveAndSharedRegistrationRetireOnlyLastOwner) {
  std::array<u8, 64> first{}, second{};
  ArrayRegistration owner(first.data(), first.size(), true);
  ArrayRegistration duplicate(first.data(), first.size(), true);
  ArrayRegistration moved(std::move(owner));
  owner.reset();
  duplicate.reset();
  ASSERT_TRUE(find_registered_array(first.data()));
  ArrayRegistration destination(second.data(), second.size(), false);
  destination = std::move(moved);
  EXPECT_FALSE(find_registered_array(second.data()));
  EXPECT_TRUE(find_registered_array(first.data()));
  destination.reset();
  EXPECT_FALSE(find_registered_array(first.data()));
}

TEST(GXArrayRegistryOwnership, InvalidOrConflictingExtentsAreRejected) {
  std::array<u8, 128> bytes{};
  EXPECT_THROW(ArrayRegistration(nullptr, 1, true), std::invalid_argument);
  EXPECT_THROW(ArrayRegistration(bytes.data(), 0, true), std::invalid_argument);
  EXPECT_THROW(ArrayRegistration(bytes.data(), uint64_t{1} << 32, true), std::invalid_argument);
  EXPECT_THROW(ArrayRegistration(reinterpret_cast<const void*>(std::numeric_limits<uintptr_t>::max() - 3), 8, true),
               std::invalid_argument);
  ArrayRegistration owner(bytes.data() + 32, 32, true);
  EXPECT_THROW(ArrayRegistration(bytes.data() + 32, 32, false), std::invalid_argument);
  EXPECT_THROW(ArrayRegistration(bytes.data() + 31, 2, true), std::invalid_argument);
  EXPECT_THROW(ArrayRegistration(bytes.data() + 63, 2, true), std::invalid_argument);
  ArrayRegistration preceding(bytes.data(), 32, true);
  ArrayRegistration following(bytes.data() + 64, 32, false);
  EXPECT_EQ(find_registered_array(bytes.data() + 63)->size, 1u);
  EXPECT_FALSE(find_registered_array(bytes.data() + 64)->littleEndian);
}

class GXArrayRegistryTest : public GXFifoTest {
protected:
  template <typename Writer>
  std::vector<u8> record_gd(Writer writer) {
    alignas(32) std::array<u8, 128> buffer{};
    GDLObj list;
    GDInitGDLObj(&list, buffer.data(), buffer.size());
    auto* previous = __GDCurrentDL;
    GDSetCurrent(&list);
    writer();
    const auto size = GDGetCurrOffset();
    GDSetCurrent(previous);
    return {buffer.begin(), buffer.begin() + size};
  }

  void position_format() {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    decode_fifo(flush_and_capture());
  }

  void draw_position(u8 index) { decode_fifo({GX_TRIANGLES, 0, 1, index}); }
};

TEST_F(GXArrayRegistryTest, UnsizedGXAndBaseOnlyBindingsResolveLiveMetadata) {
  std::array<u8, 96> bytes{};
  ArrayRegistration owner(bytes.data(), bytes.size(), false);
  GXSetArray(GX_VA_NRM, bytes.data(), 6);
  decode_fifo(capture_fifo());
  EXPECT_TRUE(gxState().arrays[GX_VA_NRM].sizeKnown);
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].size, 96u);
  EXPECT_FALSE(gxState().arrays[GX_VA_NRM].le);
  GXSetArrayBase(GX_VA_NRM, bytes.data() + 12);
  decode_fifo(capture_fifo());
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].size, 84u);
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].stride, 6u);
  owner.reset();
  GXSetArrayBase(GX_VA_NRM, bytes.data() + 12);
  decode_fifo(capture_fifo());
  EXPECT_FALSE(gxState().arrays[GX_VA_NRM].sizeKnown);
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].size, 0u);
}

TEST_F(GXArrayRegistryTest, RecordedGDPointerPatchResolvesReplacementAtReplay) {
  std::array<u8, 48> original{};
  std::array<u8, 96> replacement{};
  ArrayRegistration originalOwner(original.data(), original.size(), true);
  const auto bytes = record_gd([&] {
    GDSetArray(GX_VA_NRM, original.data(), 6);
    const auto end = GDGetCurrOffset();
    GDSetCurrOffset(3);
    GDPatchArrayPtr(replacement.data() + 12);
    GDSetCurrOffset(end);
  });
  // Neither the replacement's address nor its extent was registered when the
  // unsized command was written and patched.
  ArrayRegistration replacementOwner(replacement.data(), replacement.size(), false);
  decode_fifo(bytes);
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].data, replacement.data() + 12);
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].size, 84u);
  EXPECT_TRUE(gxState().arrays[GX_VA_NRM].sizeKnown);
  EXPECT_FALSE(gxState().arrays[GX_VA_NRM].le);
  EXPECT_EQ(gxState().arrays[GX_VA_NRM].stride, 6u);
}

TEST_F(GXArrayRegistryTest, ExplicitSizedBindingsKeepTheirCapAndByteOrder) {
  std::array<u8, 96> bytes{};
  ArrayRegistration owner(bytes.data(), bytes.size(), false);
  GXSetArraySized(GX_VA_POS, bytes.data(), 24, 12, true);
  decode_fifo(capture_fifo());
  EXPECT_EQ(gxState().arrays[GX_VA_POS].size, 24u);
  EXPECT_TRUE(gxState().arrays[GX_VA_POS].le);
  decode_fifo(record_gd([&] { GDSetArraySized(GX_VA_POS, bytes.data(), 36, 12, true); }));
  EXPECT_EQ(gxState().arrays[GX_VA_POS].size, 36u);
  EXPECT_TRUE(gxState().arrays[GX_VA_POS].le);
}

TEST_F(GXArrayRegistryTest, GrowingIndicesUploadRegisteredSourceOnce) {
  std::array<float, 48> positions{};
  ArrayRegistration owner(positions.data(), sizeof(positions), true);
  position_format();
  GXSetArray(GX_VA_POS, positions.data(), 12);
  decode_fifo(capture_fifo());
  aurora::gfx::g_testStorageUploadCount = 0;
  for (u8 index = 0; index < 16; ++index) draw_position(index);
  EXPECT_EQ(aurora::gfx::g_testStorageUploadCount, 1u);
  EXPECT_EQ(aurora::gfx::g_lastStorageUpload.size(), sizeof(positions));
  EXPECT_EQ(gxState().arrays[GX_VA_POS].requiredSize, sizeof(positions));
}

TEST_F(GXArrayRegistryTest, UnknownSourceRetainsProvenPrefixFallback) {
  std::array<float, 48> positions{};
  position_format();
  GXSetArray(GX_VA_POS, positions.data(), 12);
  decode_fifo(capture_fifo());
  aurora::gfx::g_testStorageUploadCount = 0;
  for (u8 index = 0; index < 16; ++index) draw_position(index);
  EXPECT_FALSE(gxState().arrays[GX_VA_POS].sizeKnown);
  EXPECT_EQ(aurora::gfx::g_testStorageUploadCount, 16u);
  EXPECT_EQ(aurora::gfx::g_lastStorageUpload.size(), sizeof(positions));
}

} // namespace
