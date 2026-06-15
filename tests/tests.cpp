/*
 * This file registers the Catch2 test main (provided by Catch2WithMain) and
 * a session listener that copies the fixture tree to a temp directory before
 * any tests run.  Tests read and write only from DATA_DIR (the temp copy);
 * the source tree under tests/data is never modified.
 */

#include "test_utils.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

struct TestDataInitListener : Catch::EventListenerBase
{
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(Catch::TestRunInfo const&) override { initTestDataDir(); }
};

CATCH_REGISTER_LISTENER(TestDataInitListener)
