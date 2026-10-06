module {
  func.func @match(%input: !qtt.adt<"incomplete">) {
    "qtt.case"(%input) ({}) : (!qtt.adt<"incomplete">) -> ()
    return
  }
}
