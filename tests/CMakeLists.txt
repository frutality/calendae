find_package(Qt6 REQUIRED COMPONENTS Test)

set(TGC_TEST_NAMES
    test_pkce
    test_tokenresponse
    test_authmanager
    test_wakefromsuspend
    test_credentialsprovider
    test_keychainjob
    test_oauthloopbackserver
    test_calendar
    test_googlecalendarapi
    test_monthgrid
    test_monthkeys
    test_montheventstore
    test_eventcachestore
    test_event
    test_eventtimerange
    test_timegridrange
    test_eventgrouping
    test_reminderschedule
    test_storebackedeventscontroller
    test_reminderscheduler
    test_eventpilllabel
    test_timegridhourgutterwidget
    test_oauthcredentialsdialog
    test_calendarsidebarwidget
    test_monthdaycellwidget
    test_timegridalldaycellwidget
    test_eventdialog
    test_monthviewwidget
    test_timegriddaycolumnwidget
    test_timegridviewwidget
    test_montheventscontroller
    test_timegrideventscontroller
)

foreach(test_name IN LISTS TGC_TEST_NAMES)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tgc_calendar Qt::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
