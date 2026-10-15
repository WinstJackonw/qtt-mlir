!box = !qtt.adt<"Box", [!qtt.ctor<"Box", "Mk"(i64 {qtt.multiplicity = "0"}, i64 {qtt.multiplicity = "1"}, i64)>]>
!mk = !qtt.ctor<"Box", "Mk"(i64 {qtt.multiplicity = "0"}, i64 {qtt.multiplicity = "1"}, i64)>
module {
  qtt.func @extract : (!box) -> i64 {
    qtt.func_entry @mk(%box: !mk) -> i64 {
      %erased, %linear, %shared = qtt.unpack %box : (!mk) -> (i64, i64, i64)
      qtt.return %linear : i64
    }
  }
  func.func @make(%a: i64, %b: i64, %c: i64) -> !box {
    %box = qtt.construct %a, %b, %c : (i64, i64, i64) -> !mk
    %adt = qtt.upcast %box : !mk to !box
    return %adt : !box
  }
  func.func @match(%box: !box) -> i64 {
    %result = qtt.case %box : !box -> (i64) {
      Mk(%value) {
        %erased, %linear, %shared = qtt.unpack %value : (!mk) -> (i64, i64, i64)
        qtt.yield %linear : i64
      }
    }
    return %result : i64
  }
}
