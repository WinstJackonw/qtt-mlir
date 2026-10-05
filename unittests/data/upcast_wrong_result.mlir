module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>
  func.func @bad(%a: i64) {
    %c = qtt.construct %a : (i64) -> !qtt.ctor<"expr", "Lit"(i64)>
    %r = qtt.upcast %c : !qtt.ctor<"expr", "Lit"(i64)> to i64
    return
  }
}
