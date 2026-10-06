module {
  func.func private @adt() -> !qtt.adt<"expr", [!qtt.ctor<"expr", "Nil"()>, !qtt.ctor<"expr", "Pair"(i64, i32)>]>

  func.func @unpack(%a: i64, %b: i32) {
    %nil = qtt.construct : () -> !qtt.ctor<"expr", "Nil"()>
    qtt.unpack %nil : (!qtt.ctor<"expr", "Nil"()>) -> ()
    %pair = qtt.construct %a, %b : (i64, i32) -> !qtt.ctor<"expr", "Pair"(i64, i32)>
    %pair_arg0, %pair_arg1 = qtt.unpack %pair : (!qtt.ctor<"expr", "Pair"(i64, i32)>) -> (i64, i32)
    return
  }
}
