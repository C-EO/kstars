# Tests for kstars/time/

ADD_EXECUTABLE( test_kstarsdatetime
    test_kstarsdatetime.cpp
)
TARGET_LINK_LIBRARIES( test_kstarsdatetime ${TEST_LIBRARIES} )
ADD_TEST( NAME TestKStarsDateTime COMMAND test_kstarsdatetime )
SET_TESTS_PROPERTIES( TestKStarsDateTime PROPERTIES LABELS "stable" )

# TODO: Add tests for:
#   - SimClock  (tick emission, pause/resume, time-warp factor)
#   - TimeZoneRule (DST transition dates, UTC offset calculation)
