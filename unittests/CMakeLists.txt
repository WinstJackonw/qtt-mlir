# Plain assert-based unit tests (no gtest available in this environment),
# registered with CTest.

set(QTT_UNIT_TEST_DEPS
  QTTMLIRQTTIR
  MLIRIR
  MLIRParser
  MLIRSupport
  MLIRFuncDialect
)

foreach(test_source QTTDialectTest.cpp)
  get_filename_component(test_name ${test_source} NAME_WE)

  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE ${QTT_UNIT_TEST_DEPS})

  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
