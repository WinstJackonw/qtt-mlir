module {
  func.func private @types() -> !qtt.adt<"pair", [!qtt.ctor<"pair", "A"(i64, i64)>, !qtt.ctor<"pair", "B"(i64, i64)>]>
  func.func private @empty_type() -> !qtt.adt<"empty", [!qtt.ctor<"empty", "None"()>]>
  func.func private @box_type() -> !qtt.adt<"box"(i64), [!qtt.ctor<"box"(i64), "Box"(i64)>]>
  func.func private @list_type() -> !qtt.adt<"list", [!qtt.ctor<"list", "Nil"()>, !qtt.ctor<"list", "Cons"(!qtt.adt<"list">)>]>
  func.func private @first()
  func.func private @second()
  func.func private @wrong()
  func.func @unpack_construct(%x: i64, %y: i64) -> i64 {
    %c = qtt.construct %x, %y : (i64, i64) -> !qtt.ctor<"pair", "A"(i64, i64)>
    %a, %b = qtt.unpack %c : (!qtt.ctor<"pair", "A"(i64, i64)>) -> (i64, i64)
    return %a : i64
  }
  func.func @reconstruct(%c: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "A"(i64, i64)> {
    %a, %b = qtt.unpack %c : (!qtt.ctor<"pair", "A"(i64, i64)>) -> (i64, i64)
    %r = qtt.construct %a, %b : (i64, i64) -> !qtt.ctor<"pair", "A"(i64, i64)>
    return %r : !qtt.ctor<"pair", "A"(i64, i64)>
  }
  func.func @identity(%v: !qtt.adt<"pair">) -> !qtt.adt<"pair"> {
    %r = qtt.cast %v : !qtt.adt<"pair"> to !qtt.adt<"pair">
    return %r : !qtt.adt<"pair">
  }
  func.func @upcast_back(%v: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "A"(i64, i64)> {
    %a = qtt.upcast %v : !qtt.ctor<"pair", "A"(i64, i64)> to !qtt.adt<"pair">
    %r = qtt.cast %a : !qtt.adt<"pair"> to !qtt.ctor<"pair", "A"(i64, i64)>
    return %r : !qtt.ctor<"pair", "A"(i64, i64)>
  }
  func.func @cast_back(%v: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "A"(i64, i64)> {
    %a = qtt.cast %v : !qtt.ctor<"pair", "A"(i64, i64)> to !qtt.adt<"pair">
    %r = qtt.cast %a : !qtt.adt<"pair"> to !qtt.ctor<"pair", "A"(i64, i64)>
    return %r : !qtt.ctor<"pair", "A"(i64, i64)>
  }
  func.func @known(%x: i64, %y: i64) -> (i64, i64) {
    %c = qtt.construct %x, %y : (i64, i64) -> !qtt.ctor<"pair", "A"(i64, i64)>
    %v = qtt.upcast %c : !qtt.ctor<"pair", "A"(i64, i64)> to !qtt.adt<"pair">
    %r, %s = qtt.case %v : !qtt.adt<"pair"> -> (i64, i64) {
      B(%b) { func.call @wrong() : () -> ()
        qtt.yield %y, %x : i64, i64 }
      A(%a) {
        func.call @first() : () -> ()
        %p, %q = qtt.unpack %a : (!qtt.ctor<"pair", "A"(i64, i64)>) -> (i64, i64)
        %inner = qtt.case %v : !qtt.adt<"pair"> -> (i64) {
          B(%ib) { qtt.yield %y : i64 }
          A(%ia) { qtt.yield %p : i64 }
        }
        func.call @second() : () -> ()
        qtt.yield %inner, %q : i64, i64
      }
    }
    return %r, %s : i64, i64
  }
  func.func @known_cast(%x: i64, %y: i64) -> (i64, i64) {
    %c = qtt.construct %x, %y : (i64, i64) -> !qtt.ctor<"pair", "B"(i64, i64)>
    %v = qtt.cast %c : !qtt.ctor<"pair", "B"(i64, i64)> to !qtt.adt<"pair">
    %r, %s = qtt.case %v : !qtt.adt<"pair"> -> (i64, i64) {
      A(%a) { func.call @wrong() : () -> ()
        qtt.yield %y, %x : i64, i64 }
      B(%b) { func.call @first() : () -> ()
        func.call @second() : () -> ()
        qtt.yield %x, %y : i64, i64 }
    }
    return %r, %s : i64, i64
  }
  func.func @nullary() {
    %c = qtt.construct : () -> !qtt.ctor<"empty", "None"()>
    %v = qtt.upcast %c : !qtt.ctor<"empty", "None"()> to !qtt.adt<"empty">
    qtt.case %v : !qtt.adt<"empty"> -> () {
      None(%n) { qtt.unpack %n : (!qtt.ctor<"empty", "None"()>) -> ()
        func.call @first() : () -> ()
        func.call @second() : () -> ()
        qtt.yield }
    }
    return
  }
  func.func @unknown(%v: !qtt.adt<"pair">, %x: i64, %y: i64) -> i64 {
    %r = qtt.case %v : !qtt.adt<"pair"> -> (i64) {
      A(%a) { qtt.yield %x : i64 }
      B(%b) { qtt.yield %y : i64 }
    }
    return %r : i64
  }
  func.func @reordered(%c: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "A"(i64, i64)> {
    %a, %b = qtt.unpack %c : (!qtt.ctor<"pair", "A"(i64, i64)>) -> (i64, i64)
    %r = qtt.construct %b, %a : (i64, i64) -> !qtt.ctor<"pair", "A"(i64, i64)>
    return %r : !qtt.ctor<"pair", "A"(i64, i64)>
  }
  func.func @narrow_widen(%v: !qtt.adt<"pair">) -> !qtt.adt<"pair"> {
    %c = qtt.cast %v : !qtt.adt<"pair"> to !qtt.ctor<"pair", "A"(i64, i64)>
    %r = qtt.upcast %c : !qtt.ctor<"pair", "A"(i64, i64)> to !qtt.adt<"pair">
    return %r : !qtt.adt<"pair">
  }
  func.func @different_ctor(%v: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "B"(i64, i64)> {
    %a = qtt.upcast %v : !qtt.ctor<"pair", "A"(i64, i64)> to !qtt.adt<"pair">
    %r = qtt.cast %a : !qtt.adt<"pair"> to !qtt.ctor<"pair", "B"(i64, i64)>
    return %r : !qtt.ctor<"pair", "B"(i64, i64)>
  }
  func.func @rebuild_other(%c: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "B"(i64, i64)> {
    %a, %b = qtt.unpack %c : (!qtt.ctor<"pair", "A"(i64, i64)>) -> (i64, i64)
    %r = qtt.construct %a, %b : (i64, i64) -> !qtt.ctor<"pair", "B"(i64, i64)>
    return %r : !qtt.ctor<"pair", "B"(i64, i64)>
  }
  func.func @partial(%c: !qtt.ctor<"pair", "A"(i64, i64)>) -> !qtt.ctor<"pair", "A"(i64, i64)> {
    %a, %b = qtt.unpack %c : (!qtt.ctor<"pair", "A"(i64, i64)>) -> (i64, i64)
    %r = qtt.construct %a, %a : (i64, i64) -> !qtt.ctor<"pair", "A"(i64, i64)>
    return %r : !qtt.ctor<"pair", "A"(i64, i64)>
  }
  func.func @parameterized(%c: !qtt.ctor<"box"(i64), "Box"(i64)>) -> !qtt.ctor<"box"(i64), "Box"(i64)> {
    %v = qtt.upcast %c : !qtt.ctor<"box"(i64), "Box"(i64)> to !qtt.adt<"box"(i64)>
    %r = qtt.case %v : !qtt.adt<"box"(i64)> -> (!qtt.ctor<"box"(i64), "Box"(i64)>) {
      Box(%b) { qtt.yield %b : !qtt.ctor<"box"(i64), "Box"(i64)> }
    }
    return %r : !qtt.ctor<"box"(i64), "Box"(i64)>
  }
  func.func @recursive(%tail: !qtt.adt<"list">) -> !qtt.adt<"list"> {
    %c = qtt.construct %tail : (!qtt.adt<"list">) -> !qtt.ctor<"list", "Cons"(!qtt.adt<"list">)>
    %v = qtt.upcast %c : !qtt.ctor<"list", "Cons"(!qtt.adt<"list">)> to !qtt.adt<"list">
    %r = qtt.case %v : !qtt.adt<"list"> -> (!qtt.adt<"list">) {
      Nil(%n) { qtt.yield %v : !qtt.adt<"list"> }
      Cons(%b) {
        %t = qtt.unpack %b : (!qtt.ctor<"list", "Cons"(!qtt.adt<"list">)>) -> !qtt.adt<"list">
        qtt.yield %t : !qtt.adt<"list">
      }
    }
    return %r : !qtt.adt<"list">
  }
}
