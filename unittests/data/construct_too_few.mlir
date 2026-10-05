module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>
  func.func @bad(%a: i64) {
    %r = qtt.construct : () -> !qtt.ctor<"expr", "Lit"(i64)>
    return
  }
}
