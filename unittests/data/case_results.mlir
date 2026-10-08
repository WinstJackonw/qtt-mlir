module {
  func.func private @adt() -> !qtt.adt<"choice", [!qtt.ctor<"choice", "A"()>, !qtt.ctor<"choice", "B case"()>]>
  func.func @choose(%input: !qtt.adt<"choice">, %a: i64, %b: i32) -> (i64, i32) {
    %x, %y = qtt.case %input : !qtt.adt<"choice"> -> (i64, i32) {
      A(%av) {
        %inner = qtt.case %input : !qtt.adt<"choice"> -> (i64) {
          A(%ia) { qtt.yield %a : i64 }
          "B case"(%ib) { qtt.yield %a : i64 }
        } {nested = true}
        qtt.yield %inner, %b : i64, i32
      }
      "B case"(%bv) { qtt.yield %a, %b : i64, i32 }
    } {marker = "kept"}
    return %x, %y : i64, i32
  }
}
