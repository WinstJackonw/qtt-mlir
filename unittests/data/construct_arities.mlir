module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Nil"()>, !qtt.ctor<"expr", "Pair"(i64, i32)>]>
  func.func @make(%a: i64, %b: i32) {
    %nil = qtt.construct : () -> !qtt.ctor<"expr", "Nil"()>
    %pair = qtt.construct %a, %b : (i64, i32) -> !qtt.ctor<"expr", "Pair"(i64, i32)>
    return
  }
}
