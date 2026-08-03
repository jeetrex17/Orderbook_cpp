#include "IdIndex.h"

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

TEST(IdIndex, FindOnAnEmptyMapReportsMissing) {
  IdIndex index;
  EXPECT_EQ(index.find(1), IdIndex::kMissing);
  EXPECT_EQ(index.size(), 0u);
}

TEST(IdIndex, InsertThenFind) {
  IdIndex index;
  index.insert(42, 7);
  EXPECT_EQ(index.find(42), 7u);
  EXPECT_EQ(index.size(), 1u);
}

TEST(IdIndex, InsertingAnExistingKeyOverwritesWithoutGrowing) {
  IdIndex index;
  index.insert(42, 7);
  index.insert(42, 9);
  EXPECT_EQ(index.find(42), 9u);
  EXPECT_EQ(index.size(), 1u);
}

TEST(IdIndex, EraseRemovesTheKey) {
  IdIndex index;
  index.insert(42, 7);
  EXPECT_TRUE(index.erase(42));
  EXPECT_EQ(index.find(42), IdIndex::kMissing);
  EXPECT_EQ(index.size(), 0u);
}

TEST(IdIndex, ErasingAMissingKeyIsANoOp) {
  IdIndex index;
  index.insert(42, 7);
  EXPECT_FALSE(index.erase(43));
  EXPECT_EQ(index.size(), 1u);
}

TEST(IdIndex, ZeroIsAUsableKey) {
  // Slots are zero-initialised, so a naive "key == 0 means empty" scheme would
  // silently lose this entry.
  IdIndex index;
  index.insert(0, 5);
  EXPECT_EQ(index.find(0), 5u);
  EXPECT_TRUE(index.erase(0));
  EXPECT_EQ(index.find(0), IdIndex::kMissing);
}

TEST(IdIndex, SurvivesGrowth) {
  IdIndex index;
  constexpr uint64_t kCount = 10'000; // far past the initial 64 slots
  for (uint64_t i = 0; i < kCount; ++i) {
    index.insert(i, static_cast<uint32_t>(i * 2));
  }
  EXPECT_EQ(index.size(), kCount);
  for (uint64_t i = 0; i < kCount; ++i) {
    EXPECT_EQ(index.find(i), static_cast<uint32_t>(i * 2)) << "key " << i;
  }
}

// The failure mode backward-shift deletion exists to prevent: erasing a key
// early in a probe chain must not strand the keys behind it. These properties
// drive long insert/erase mixes against std::unordered_map as an oracle.

RC_GTEST_PROP(IdIndex, BehavesLikeUnorderedMap,
              (const std::vector<std::pair<uint64_t, bool>>& ops)) {
  IdIndex index;
  std::unordered_map<uint64_t, uint32_t> oracle;

  uint32_t next_value = 1;
  for (const auto& [raw_key, is_erase] : ops) {
    // A narrow key space forces collisions and long probe chains, which is
    // where deletion goes wrong.
    const uint64_t key = raw_key % 128;

    if (is_erase) {
      RC_ASSERT(index.erase(key) == (oracle.erase(key) > 0));
    } else {
      const uint32_t value = next_value++;
      index.insert(key, value);
      oracle[key] = value;
    }

    RC_ASSERT(index.size() == oracle.size());

    // Every key the oracle holds must still be reachable, and nothing else.
    for (uint64_t probe = 0; probe < 128; ++probe) {
      const auto expected = oracle.find(probe);
      if (expected == oracle.end()) {
        RC_ASSERT(index.find(probe) == IdIndex::kMissing);
      } else {
        RC_ASSERT(index.find(probe) == expected->second);
      }
    }
  }
}

RC_GTEST_PROP(IdIndex, SurvivesRepeatedFillAndDrain,
              (const std::vector<uint8_t>& raw_keys)) {
  // Insert everything, then erase it all in a different order. If deletion
  // strands entries, the second pass reports a key as missing that is present.
  IdIndex index;
  std::vector<uint64_t> keys;
  for (const uint8_t k : raw_keys) {
    if (index.find(k) == IdIndex::kMissing) {
      index.insert(k, static_cast<uint32_t>(keys.size() + 1));
      keys.push_back(k);
    }
  }
  RC_ASSERT(index.size() == keys.size());

  // Erase back to front.
  for (std::size_t i = keys.size(); i > 0; --i) {
    RC_ASSERT(index.erase(keys[i - 1]));
    // Everything not yet erased is still findable.
    for (std::size_t j = 0; j + 1 < i; ++j) {
      RC_ASSERT(index.find(keys[j]) != IdIndex::kMissing);
    }
  }
  RC_ASSERT(index.size() == 0);
}

} // namespace
