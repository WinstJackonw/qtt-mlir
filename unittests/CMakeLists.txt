# Lightweight tests with checks active in all build configurations.
add_executable(QTTDialectTests QTTDialectTests.cpp)
target_link_libraries(QTTDialectTests PRIVATE
  QTTMLIRQTTIR
  QTTMLIRQREPIR
  QTTMLIRQTTToQREP
  MLIRParser
  MLIRFuncDialect
)
target_compile_definitions(QTTDialectTests PRIVATE
  "QTT_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

# Keep each behavior independently selectable and visible in CTest output.
foreach(test_case
  DialectRegistration
  QREPRegistration
  ADTTypeRoundtrip
  ADTTypeArguments
  ParameterizedConstructUpcast
  ADTTypeConflictRejected
  ADTDuplicateCtorRejected
  CtorTypeRoundtrip
  CtorOfUndefinedADTAccepted
  CtorUnknownConstructorRejected
  UnknownTypeRejected
  IsSubType
  ConstructUpcastRoundtrip
  UnpackRoundtrip
  CasePrettyPrint
  CaseResultsRoundtrip
  YieldWrongCount
  YieldWrongType
  YieldWrongParent
  CaseMissingYield
  CaseBranchValidation
  CaseIncompleteADTRejected
  PayloadMismatchRejected
  CrossADTUpcastRejected
  ADTRecursiveRoundtrip
  ADTDefinitionLifecycle
  ConstructArityRoundtrip
  ConstructWrongResult
  ConstructTooFewArguments
  ConstructTooManyArguments
  UpcastWrongInput
  UpcastWrongResult
  CtorPayloadCoexists
  ADTInvalidEntryRejected
)
  add_test(NAME QTT.${test_case} COMMAND QTTDialectTests ${test_case})
endforeach()
