module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>, !qtt.ctor<"expr", "Nil"()>]>
  func.func @match(%input: !qtt.adt<"expr">) {
    "qtt.case"(%input) ({
    ^bb0(%lit: !qtt.ctor<"expr", "Lit"(i64)>):
      %same = qtt.cast %lit : !qtt.ctor<"expr", "Lit"(i64)> to !qtt.ctor<"expr", "Lit"(i64)>
      qtt.yield
    }, {
    ^bb0(%nil: !qtt.ctor<"expr", "Nil"()>):
      qtt.yield
    }) : (!qtt.adt<"expr">) -> ()
    return
  }
}
