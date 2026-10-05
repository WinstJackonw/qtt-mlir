module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>
  func.func @bad(%a: i64) {
    %r = qtt.upcast %a : i64 to !qtt.adt<"expr">
    return
  }
}
