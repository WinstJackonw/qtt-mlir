// Positive roundtrip: ADT definition, qtt.construct and qtt.upcast.
module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>

  func.func @make(%a: i64) -> !qtt.adt<"expr"> {
    %c = qtt.construct %a : (i64) -> !qtt.ctor<"expr", "Lit"(i64)>
    %r = qtt.upcast %c : !qtt.ctor<"expr", "Lit"(i64)> to !qtt.adt<"expr">
    return %r : !qtt.adt<"expr">
  }
}
