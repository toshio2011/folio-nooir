#include <gtest/gtest.h>

#include "src/util/HeapLifetimeTraceStore.h"

using heap_lifetime_trace::CallerId;
using heap_lifetime_trace::EventKind;
using heap_lifetime_trace::TraceStore;

TEST(HeapLifetimeTrace, MatchesAllocationToFreeAcrossOwnerContexts) {
  TraceStore<4, 8> trace;
  trace.recordAllocation(0x1000, 96, 0x804, 100, CallerId::HomeActivity, 7);
  trace.recordFree(0x1000, 240, CallerId::HomeDestroy, 9);

  const auto summary = trace.summary();
  EXPECT_EQ(summary.allocations, 1U);
  EXPECT_EQ(summary.frees, 1U);
  EXPECT_EQ(summary.liveCount, 0U);
  EXPECT_EQ(summary.unmatchedFrees, 0U);

  heap_lifetime_trace::AllocationEvent allocation{};
  heap_lifetime_trace::AllocationEvent freed{};
  ASSERT_TRUE(trace.eventAt(0, allocation));
  ASSERT_TRUE(trace.eventAt(1, freed));
  EXPECT_EQ(allocation.kind, EventKind::Allocate);
  EXPECT_EQ(allocation.address, 0x1000U);
  EXPECT_EQ(allocation.size, 96U);
  EXPECT_EQ(allocation.requestedCaps, 0x804U);
  EXPECT_EQ(allocation.allocationCaller, static_cast<uint8_t>(CallerId::HomeActivity));
  EXPECT_EQ(allocation.eventCaller, static_cast<uint8_t>(CallerId::HomeActivity));
  EXPECT_EQ(freed.kind, EventKind::Free);
  EXPECT_EQ(freed.address, allocation.address);
  EXPECT_EQ(freed.allocationCaller, static_cast<uint8_t>(CallerId::HomeActivity));
  EXPECT_EQ(freed.eventCaller, static_cast<uint8_t>(CallerId::HomeDestroy));
  EXPECT_EQ(freed.timestampUs, 240U);
}

TEST(HeapLifetimeTrace, KeepsAddressReuseAsANewLifetime) {
  TraceStore<2, 8> trace;
  trace.recordAllocation(0x2000, 128, 0x804, 100, CallerId::BleHostInit, 1);
  trace.recordFree(0x2000, 200, CallerId::BleShutdown, 1);
  trace.recordAllocation(0x2000, 64, 0x804, 300, CallerId::HomeRender, 2);

  ASSERT_EQ(trace.summary().liveCount, 1U);
  heap_lifetime_trace::LiveAllocation live{};
  ASSERT_TRUE(trace.liveAt(0, live));
  EXPECT_EQ(live.address, 0x2000U);
  EXPECT_EQ(live.size, 64U);
  EXPECT_EQ(live.allocatedAtUs, 300U);
  EXPECT_EQ(live.caller, CallerId::HomeRender);
}

TEST(HeapLifetimeTrace, StoresAllocatorRoundedSizeAndCarriesItToTheFreeEvent) {
  TraceStore<2, 4> trace;
  trace.recordAllocation(0x2800, 17, 0x804, 100, CallerId::NimbleInit, 3);

  heap_lifetime_trace::LiveAllocation live{};
  ASSERT_TRUE(trace.liveAt(0, live));
  EXPECT_EQ(live.size, 17U);
  EXPECT_EQ(live.actualSize, 0U);

  EXPECT_TRUE(trace.setActualSize(0x2800, 20));
  ASSERT_TRUE(trace.liveAt(0, live));
  EXPECT_EQ(live.actualSize, 20U);
  heap_lifetime_trace::AllocationEvent allocated{};
  ASSERT_TRUE(trace.eventAt(0, allocated));
  EXPECT_EQ(allocated.actualSize, 20U);

  trace.recordFree(0x2800, 200, CallerId::BleShutdown, 3);
  heap_lifetime_trace::AllocationEvent freed{};
  ASSERT_TRUE(trace.eventAt(1, freed));
  EXPECT_EQ(freed.kind, EventKind::Free);
  EXPECT_EQ(freed.size, 17U);
  EXPECT_EQ(freed.actualSize, 20U);
}

TEST(HeapLifetimeTrace, ActualSizeUpdateRequiresAnActiveTrackedPointer) {
  TraceStore<2, 4> trace;
  EXPECT_FALSE(trace.setActualSize(0x2900, 32));
  trace.recordAllocation(0x2900, 24, 0x804, 100, CallerId::HomeActivity, 2);
  EXPECT_TRUE(trace.setActualSize(0x2900, 24));
  trace.recordFree(0x2900, 200, CallerId::HomeDestroy, 2);
  EXPECT_FALSE(trace.setActualSize(0x2900, 24));
}

TEST(HeapLifetimeTrace, CountsBoundedTableAndEventRingDrops) {
  TraceStore<1, 1> trace;
  trace.recordAllocation(0x3000, 32, 0x804, 1, CallerId::Boot, 0);
  trace.recordAllocation(0x4000, 48, 0x804, 2, CallerId::NimbleInit, 0);

  const auto summary = trace.summary();
  EXPECT_EQ(summary.allocations, 2U);
  EXPECT_EQ(summary.liveCount, 1U);
  EXPECT_EQ(summary.droppedLiveRecords, 1U);
  EXPECT_EQ(summary.droppedEvents, 1U);
  heap_lifetime_trace::AllocationEvent recent{};
  ASSERT_TRUE(trace.eventAt(0, recent));
  EXPECT_EQ(recent.address, 0x4000U);
  EXPECT_EQ(recent.kind, EventKind::Allocate);
}

TEST(HeapLifetimeTrace, RecordsFailuresAndUnmatchedFreesWithoutInventingLiveBlocks) {
  TraceStore<2, 8> trace;
  trace.recordFailure(4096, 0x804, 100, CallerId::BleControllerInit, 0xA55AU);
  trace.recordFree(0x5000, 120, CallerId::BleShutdown, 4);

  const auto summary = trace.summary();
  EXPECT_EQ(summary.failedAllocations, 1U);
  EXPECT_EQ(summary.unmatchedFrees, 1U);
  EXPECT_EQ(summary.liveCount, 0U);
  heap_lifetime_trace::AllocationEvent failure{};
  heap_lifetime_trace::AllocationEvent unmatched{};
  ASSERT_TRUE(trace.eventAt(0, failure));
  ASSERT_TRUE(trace.eventAt(1, unmatched));
  EXPECT_EQ(failure.kind, EventKind::Failed);
  EXPECT_EQ(failure.size, 4096U);
  EXPECT_EQ(failure.callerId, 0xA55AU);
  EXPECT_EQ(unmatched.kind, EventKind::UnmatchedFree);
  EXPECT_EQ(unmatched.address, 0x5000U);
}

TEST(HeapLifetimeTrace, IgnoresNullAddressAllocationsAndFrees) {
  TraceStore<2, 4> trace;
  trace.recordAllocation(0, 64, 0x804, 10, CallerId::Boot, 0);
  trace.recordFree(0, 20, CallerId::Boot, 0);
  EXPECT_EQ(trace.summary().allocations, 0U);
  EXPECT_EQ(trace.summary().frees, 0U);
  EXPECT_EQ(trace.eventCount(), 0U);
}
