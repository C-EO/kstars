# Tests for kstars/skycomponents/
#
# TODO: Add tests for:
#   - ArtificialHorizonComponent (loading, isVisible, ceiling regions)
#   - StarComponent (binary file loading, HTMesh indexing)
#   - SkyComposite initialisation and updateSky()
#   - CatalogComponent (OpenNGC / SAC import and lookup)
#   - SatellitesComponent (TLE parsing, position propagation)
#
# Example target structure (uncomment and complete when adding tests):
#
# ADD_EXECUTABLE( test_skycomponents test_skycomponents.cpp )
# TARGET_LINK_LIBRARIES( test_skycomponents ${TEST_LIBRARIES} )
# ADD_TEST( NAME TestSkyComponents COMMAND test_skycomponents )
# SET_TESTS_PROPERTIES( TestSkyComponents PROPERTIES LABELS "stable" )
