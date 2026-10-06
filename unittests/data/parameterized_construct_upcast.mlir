module {
  func.func private @adt() -> !qtt.adt<"Either"(i64, f64), [!qtt.ctor<"Either"(i64, f64), "Left"(i64)>, !qtt.ctor<"Either"(i64, f64), "Right"(f64)>]>
  func.func @make(%a: i64) -> !qtt.adt<"Either"(i64, f64)> {
    %c = qtt.construct %a : (i64) -> !qtt.ctor<"Either"(i64, f64), "Left"(i64)>
    %r = qtt.upcast %c : !qtt.ctor<"Either"(i64, f64), "Left"(i64)> to !qtt.adt<"Either"(i64, f64)>
    return %r : !qtt.adt<"Either"(i64, f64)>
  }
}
