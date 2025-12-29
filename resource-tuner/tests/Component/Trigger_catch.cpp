// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <catch2/catch_test_macros.hpp>
#include "TestAggregator.h"

// The original Trigger.cpp provided a main() that iterated all registered
// Component tests from TestAggregator and invoked them. In Catch2, we must
// not define a main() inside test sources; instead, we create a single
// TEST_CASE that performs the same iteration, while main() is provided by
// the tiny Catch2 TU (component/catch2_main_component.cpp).

TEST_CASE("Trigger: run all aggregated component tests", "[Component][Aggregator]")
{
    auto allTests = TestAggregator::getAllTests();
    for (auto test : allTests) {
        test();
    }
    // No assertions here because the original runner didn't assert;
    // individual tests are expected to assert internally.
}
