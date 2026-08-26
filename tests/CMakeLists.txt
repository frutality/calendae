find_package(Qt6 REQUIRED COMPONENTS Test)

set(TGC_TEST_NAMES
    test_pkce
    test_tokenresponse
    test_credentialsprovider
    test_oauthloopbackserver
    test_calendar
    test_googlecalendarapi
    test_monthgrid
    test_event
    test_eventtimerange
)

foreach(test_name IN LISTS TGC_TEST_NAMES)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tgc_calendar Qt::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
