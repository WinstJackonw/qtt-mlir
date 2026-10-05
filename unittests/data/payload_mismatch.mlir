// Parses, but must fail verification: payload is i32 while the constructor
// declares i64.
module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>

  func.func @bad(%a: i32) {
    %c = qtt.construct %a : (i32) -> !qtt.ctor<"expr", "Lit"(i64)>
    return
  }
}
