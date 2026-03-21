# Tests for kstars/ekos/observatory/
#
# TODO: Add tests for:
#   - ObservatoryModel state machine (weather alert, auto-park, auto-unpark)
#   - ObservatoryDomeModel (parking, shutter open/close, slaving)
#   - ObservatoryWeatherModel (warning/alert thresholds, grace period)
#
# These models contain testable state machine logic that does not require
# a live INDI dome or weather station.  Mock the INDI device signals directly.
#
# Example target structure (uncomment and complete when adding tests):
#
# IF (INDI_FOUND)
# INCLUDE_DIRECTORIES(${INDI_INCLUDE_DIR})
# ADD_EXECUTABLE( test_observatorymodel test_observatorymodel.cpp )
# TARGET_LINK_LIBRARIES( test_observatorymodel ${TEST_LIBRARIES} )
# ADD_TEST( NAME TestObservatoryModel COMMAND test_observatorymodel )
# SET_TESTS_PROPERTIES( TestObservatoryModel PROPERTIES LABELS "stable" )
# ENDIF()
