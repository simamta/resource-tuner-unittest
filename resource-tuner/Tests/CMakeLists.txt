if(BUILD_TESTS)
    # Install Test Configs
    file(GLOB testConfigs "${CMAKE_CURRENT_SOURCE_DIR}/Configs/*.yaml")
    install(
        FILES ${testConfigs}
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/resource-tuner/tests/configs/
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )

    # Install Custom Resource Nodes
    install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Configs/ResourceSysFsNodes/
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/resource-tuner/tests/nodes/
        FILES_MATCHING
        PATTERN "*.txt"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )

    # ---- Test utilities (unchanged) ----
    add_library(RestuneTestUtils
                ${CMAKE_CURRENT_SOURCE_DIR}/Utils/Baseline.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/Utils/TestAggregator.cpp)
    set_target_properties(RestuneTestUtils PROPERTIES VERSION 1.0.0 SOVERSION 1)
    target_link_libraries(RestuneTestUtils UrmAuxUtils)
    target_include_directories(RestuneTestUtils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Utils/Include)
    install(TARGETS RestuneTestUtils DESTINATION ${CMAKE_INSTALL_LIBDIR})

    add_library(RestunePlugin ${CMAKE_CURRENT_SOURCE_DIR}/Utils/Setup.cpp)
    set_target_properties(RestunePlugin PROPERTIES VERSION 1.0.0 SOVERSION 1)
    target_link_libraries(RestunePlugin RestuneExtAPIs)
    install(TARGETS RestunePlugin DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # ---- Integration tests (unchanged) ----
    add_executable(RestuneIntegrationTests Integration/IntegrationTests.cpp)
    target_link_libraries(RestuneIntegrationTests PUBLIC UrmAuxUtils
                                                         UrmClient
                                                         RestuneTestUtils
                                                         ${LIBYAML_LIBRARIES})
    target_include_directories(RestuneIntegrationTests PRIVATE ${LIBYAML_INCLUDE_DIRS})
    install(TARGETS RestuneIntegrationTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    
    # ----------------------------------------------------------------------------
    # Unit Test: rt_serverrequests_tests (single binary with all unit tests)
    # ----------------------------------------------------------------------------

    # Collect all unit test sources in one variable
    set(RT_UNIT_TEST_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_main.cpp                 # provides Catch2 main()
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_server_requests.cpp      # existing test
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_req_queue_cleanup.cpp    # Test #1 (cleanup trigger)
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_req_queue_tune_non_terminal.cpp
        # Add future tests here:
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_req_queue_tune_terminal.cpp
        # ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_req_queue_tune_terminal.cpp
        # ...
    )

    add_executable(rt_serverrequests_tests
           ${RT_UNIT_TEST_SOURCES}
    )

    # Include paths: Catch2 (vendored) and any project headers tests need
    target_include_directories(rt_serverrequests_tests PRIVATE
           ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/catch2          # catch_amalgamated.hpp
           # Add any extra include dirs as your tests grow:
           # ${CMAKE_SOURCE_DIR}/resource-tuner/public_headers
           # ${CMAKE_SOURCE_DIR}/server/include
    )

    # Link against core libs so tests inherit PUBLIC includes & symbols
    target_link_libraries(rt_serverrequests_tests PRIVATE
         RestuneCore
         RestuneExtAPIs
         RestuneSignals
         UrmAuxUtils
         UrmClient
         catch2_amalgamated
    )

    # C++ standard
    target_compile_features(rt_serverrequests_tests PRIVATE cxx_std_17)

    # Test registration (JUnit output for CI)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-results")
    add_test(
         NAME rt_serverrequests_tests_junit
         COMMAND rt_serverrequests_tests -r junit -o ${CMAKE_BINARY_DIR}/test-results/rt_serverrequests_tests.xml
    )
    add_test(
         NAME rt_serverrequests_tests_console
         COMMAND rt_serverrequests_tests --success --durations yes
    )

    # ----------------------------------------------------------------------------
    # Component tests (Catch2 runner) — SINGLE TARGET
    # ----------------------------------------------------------------------------
    # Source list for component tests + Catch2 main TU
    set(COMPONENT_TEST_SOURCES
        # Catch2-converted file(s)
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/RequestQueueTests_catch.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/Trigger_catch.cpp   # <- use this


        # Catch2 main for component runner
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/catch2_main_component.cpp

        # Other component tests (restored from legacy target)
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/MemoryPoolTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/ClientDataManagerTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/ParserTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/ExtensionIntfTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/MiscTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/ThreadPoolTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/SafeOpsTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/RateLimiterTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/TimerTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/DeviceInfoTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/CocoTableTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/RequestMapTests.cpp
        #${CMAKE_CURRENT_SOURCE_DIR}/Component/Trigger.cpp
    )

    add_executable(RestuneComponentTests ${COMPONENT_TEST_SOURCES})

    # Include dirs: Catch2 (vendored) and project headers needed by tests
    target_include_directories(RestuneComponentTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/catch2
        ${CMAKE_CURRENT_SOURCE_DIR}/Utils/Include
        ${PROJECT_SOURCE_DIR}/resource-tuner/Core/Framework/Include
        ${PROJECT_SOURCE_DIR}/resource-tuner/Core/Extensions/Include
        ${PROJECT_SOURCE_DIR}/resource-tuner/Signals/Include
    )

    # Link project libraries + Catch2 static lib
    target_link_libraries(RestuneComponentTests PRIVATE
        UrmAuxUtils
        RestuneExtAPIs
        RestuneCore
        RestuneSignals
        RestuneTestUtils
        catch2_amalgamated
    )

    target_compile_features(RestuneComponentTests PRIVATE cxx_std_17)

    # Install
    install(TARGETS RestuneComponentTests DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Register with CTest
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-results")
    add_test(NAME restune_component_tests_console
             COMMAND RestuneComponentTests --success --durations yes)
    add_test(NAME restune_component_tests_junit
             COMMAND RestuneComponentTests -r junit -o ${CMAKE_BINARY_DIR}/test-results/restune_component_tests.xml)
    
endif()
