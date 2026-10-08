module {
  func.func private @adt() -> !qtt.adt<"choice", [!qtt.ctor<"choice", "A"()>, !qtt.ctor<"choice", "B"()>]>
  func.func @bad(%input: !qtt.adt<"choice">) {
    "qtt.case"(%input) ({
    ^bb0(%a: !qtt.ctor<"choice", "A"()>):
      %same = qtt.cast %a : !qtt.ctor<"choice", "A"()> to !qtt.ctor<"choice", "A"()>
    }, {
    ^bb0(%b: !qtt.ctor<"choice", "B"()>):
      qtt.yield
    }) : (!qtt.adt<"choice">) -> ()
    return
  }
}
