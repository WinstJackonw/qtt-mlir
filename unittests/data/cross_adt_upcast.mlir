// Parses, but must fail verification: the constructor belongs to @adt while
// the upcast targets the unrelated ADT @other.
module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>
  func.func private @other() -> !qtt.adt<"other", [!qtt.ctor<"other", "Lit"(i64)>]>

  func.func @bad(%a: i64) {
    %c = qtt.construct %a : (i64) -> !qtt.ctor<"expr", "Lit"(i64)>
    %r = qtt.upcast %c : !qtt.ctor<"expr", "Lit"(i64)> to !qtt.adt<"other">
    return
  }
}
