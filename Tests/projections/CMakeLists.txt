# Tests for kstars/projections/
#
# TODO: Add tests for all 6 projector types:
#   - GnomonicProjector
#   - LambertProjector
#   - AzimuthalEquidistantProjector
#   - OrthographicProjector
#   - StereographicProjector
#   - EquirectangularProjector
#
# Each projector should be tested for:
#   - Round-trip: toScreen() → fromScreen() returns the original SkyPoint
#   - Known-value: a star at a known RA/DEC produces the expected pixel position
#   - Boundary: points near/beyond the projection horizon are handled correctly
#
# Example target structure (uncomment and complete when adding tests):
#
# ADD_EXECUTABLE( test_projections test_projections.cpp )
# TARGET_LINK_LIBRARIES( test_projections ${TEST_LIBRARIES} )
# ADD_TEST( NAME TestProjections COMMAND test_projections )
# SET_TESTS_PROPERTIES( TestProjections PROPERTIES LABELS "stable" )
