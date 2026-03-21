# Tests for kstars/time/
#
# TODO: Add tests for:
#   - KStarsDateTime  (JD ↔ calendar, QDateTime interop, DST handling)
#   - SimClock        (tick emission, pause/resume, time-warp factor)
#   - TimeZoneRule    (DST transition dates, UTC offset calculation)
#
# Example target structure (uncomment and complete when adding tests):
#
# ADD_EXECUTABLE( test_kstarsdatetime test_kstarsdatetime.cpp )
# TARGET_LINK_LIBRARIES( test_kstarsdatetime ${TEST_LIBRARIES} )
# ADD_TEST( NAME TestKStarsDateTime COMMAND test_kstarsdatetime )
# SET_TESTS_PROPERTIES( TestKStarsDateTime PROPERTIES LABELS "stable" )
#
# ADD_EXECUTABLE( test_simclock test_simclock.cpp )
# TARGET_LINK_LIBRARIES( test_simclock ${TEST_LIBRARIES} )
# ADD_TEST( NAME TestSimClock COMMAND test_simclock )
# SET_TESTS_PROPERTIES( TestSimClock PROPERTIES LABELS "stable" )
