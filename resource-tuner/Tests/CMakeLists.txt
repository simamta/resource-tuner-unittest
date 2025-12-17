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

    add_executable(RestuneIntegrationTests Integration/IntegrationTests.cpp)
    target_link_libraries(RestuneIntegrationTests PUBLIC UrmAuxUtils
                                                         UrmClient
                                                         RestuneTestUtils
                                                         ${LIBYAML_LIBRARIES})
    target_include_directories(RestuneIntegrationTests PRIVATE ${LIBYAML_INCLUDE_DIRS})
    install(TARGETS RestuneIntegrationTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    add_executable(
        RestuneComponentTests
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/MemoryPoolTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/ClientDataManagerTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/ParserTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/ExtensionIntfTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/MiscTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/RequestQueueTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/ThreadPoolTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/SafeOpsTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/RateLimiterTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/TimerTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/DeviceInfoTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/CocoTableTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/RequestMapTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Component/Trigger.cpp)

    target_link_libraries(RestuneComponentTests PUBLIC UrmAuxUtils
                                                       RestuneExtAPIs
                                                       RestuneCore
                                                       RestuneSignals
                                                       RestuneTestUtils)

    install(TARGETS RestuneComponentTests DESTINATION ${CMAKE_INSTALL_BINDIR})

    # ---------------------------------------------------------------------------
    # Unit Test: rt_serverrequests_tests (Catch2 header-only, vendored)
    # Self-contained: compiles only the minimal sources needed for ServerRequests
    # ---------------------------------------------------------------------------
    add_executable(rt_serverrequests_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_server_requests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/catch2/catch_amalgamated.cpp
    )

    # Include paths: Catch2 and project headers
    target_include_directories(rt_serverrequests_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/catch2             # catch_amalgamated.hpp
    )
    

    
    # Link against the core libraries so the test inherits PUBLIC includes & symbols
    target_link_libraries(rt_serverrequests_tests PRIVATE
        RestuneCore
        RestuneExtAPIs
        RestuneSignals
        UrmAuxUtils
        UrmClient
    )

    # C++ standard
    target_compile_features(rt_serverrequests_tests PRIVATE cxx_std_17)
    
    # Extra test to emit JUnit XML into build/test-results
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-results")
    add_test(
        NAME rt_serverrequests_tests_junit
        COMMAND rt_serverrequests_tests -r junit -o ${CMAKE_BINARY_DIR}/test-results/rt_serverrequests_tests.xml
    )
  
endif()
