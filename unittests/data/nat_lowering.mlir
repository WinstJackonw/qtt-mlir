!nat = !qtt.adt<"Nat", [!qtt.ctor<"Nat", "Z"()>, !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>]>
!z = !qtt.ctor<"Nat", "Z"()>
!s = !qtt.ctor<"Nat", "S"(!nat)>
module {
  qrep.layout @nat attributes {
    adt = !nat, storage = i64, view = i64,
    constructors = [{constructor = !z, storage = none, view = none},
                    {constructor = !s, storage = i64, view = i64}]
  } {
    qrep.impl clone(%n: i64) {
      qrep.yield %n : i64
    }
    qrep.impl drop(%n: i64) {
      qrep.yield
    }
    qrep.impl clone(%n: none) {
      qrep.yield %n : none
    } {constructor = !z}
    qrep.impl drop(%n: none) {
      qrep.yield
    } {constructor = !z}
    qrep.impl clone(%n: i64) {
      qrep.yield %n : i64
    } {constructor = !s}
    qrep.impl drop(%n: i64) {
      qrep.yield
    } {constructor = !s}
    qrep.impl borrow(%n: i64) {
      qrep.yield %n : i64
    }
    qrep.impl borrow(%n: none) {
      qrep.yield %n : none
    } {constructor = !z}
    qrep.impl borrow(%n: i64) {
      qrep.yield %n : i64
    } {constructor = !s}
    qrep.discriminate(%n: i64) {
      %zero = arith.constant 0 : i64
      %s = arith.cmpi ne, %n, %zero : i64
      cf.cond_br %s, ^successor, ^zero
    ^zero:
      %z = builtin.unrealized_conversion_cast to none
      qrep.dispatch !z, %z : none
    ^successor:
      qrep.dispatch !s, %n : i64
    }
    qrep.impl pack() {
      %z = builtin.unrealized_conversion_cast to none
      qrep.yield %z : none
    } {constructor = !z}
    qrep.impl pack(%n: i64) {
      %one = arith.constant 1 : i64
      %s = arith.addi %n, %one : i64
      qrep.yield %s : i64
    } {constructor = !s}

    qrep.impl upcast(%n: none) {
      %zero = arith.constant 0 : i64
      qrep.yield %zero : i64
    } {constructor = !z}
    qrep.impl downcast(%n: i64) {
      %z = builtin.unrealized_conversion_cast to none
      qrep.yield %z : none
    } {constructor = !z}
    qrep.impl unpack(%n: none) {
      qrep.yield
    } {constructor = !z}
    qrep.impl upcast(%n: i64) {
      qrep.yield %n : i64
    } {constructor = !s}
    qrep.impl downcast(%n: i64) {
      qrep.yield %n : i64
    } {constructor = !s}
    qrep.impl unpack(%n: i64) {
      %one = arith.constant 1 : i64
      %p = arith.subi %n, %one : i64
      qrep.yield %p : i64
    } {constructor = !s}
  }
  func.func private @definition() -> !qtt.adt<"Nat", [!qtt.ctor<"Nat", "Z"()>, !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>]> attributes {qrep.layout = @nat}
  func.func @zero() -> !qtt.adt<"Nat"> attributes {qrep.layout = @nat} {
    %z = qtt.construct : () -> !qtt.ctor<"Nat", "Z"()>
    %n = qtt.upcast %z : !qtt.ctor<"Nat", "Z"()> to !qtt.adt<"Nat">
    return %n : !qtt.adt<"Nat">
  }
  func.func @succ(%n: !qtt.adt<"Nat">) -> !qtt.adt<"Nat"> attributes {qrep.layout = @nat} {
    %s = qtt.construct %n : (!qtt.adt<"Nat">) -> !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>
    %r = qtt.upcast %s : !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)> to !qtt.adt<"Nat">
    return %r : !qtt.adt<"Nat">
  }
  func.func @pred(%n: !qtt.adt<"Nat">) -> !qtt.adt<"Nat"> attributes {qrep.layout = @nat} {
    %r = qtt.case %n : !qtt.adt<"Nat"> -> (!qtt.adt<"Nat">) {
      Z(%z) {
        qtt.unpack %z : (!qtt.ctor<"Nat", "Z"()>) -> ()
        qtt.yield %n : !qtt.adt<"Nat">
      }
      S(%s) {
        %p = qtt.unpack %s : (!qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>) -> !qtt.adt<"Nat">
        qtt.yield %p : !qtt.adt<"Nat">
      }
    }
    return %r : !qtt.adt<"Nat">
  }
  func.func @call_succ(%n: !qtt.adt<"Nat">) -> !qtt.adt<"Nat"> attributes {qrep.layout = @nat} {
    %r = func.call @succ(%n) : (!qtt.adt<"Nat">) -> !qtt.adt<"Nat">
    return %r : !qtt.adt<"Nat">
  }
  func.func @cast_pred(%n: !qtt.adt<"Nat">) -> !qtt.adt<"Nat"> attributes {qrep.layout = @nat} {
    %same = qtt.cast %n : !qtt.adt<"Nat"> to !qtt.adt<"Nat">
    %s = qtt.cast %same : !qtt.adt<"Nat"> to !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>
    %p = qtt.unpack %s : (!qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>) -> !qtt.adt<"Nat">
    return %p : !qtt.adt<"Nat">
  }
  func.func @ctor_only(%n: !nat) -> !nat attributes {qrep.layout = @nat} {
    %s = qtt.construct %n : (!nat) -> !s
    %p = qtt.unpack %s : (!s) -> !nat
    return %p : !nat
  }
  func.func @nested_construct(%n: !nat) -> !nat attributes {qrep.layout = @nat} {
    %s = qtt.construct %n : (!nat) -> !s
    %a = qtt.upcast %s : !s to !nat
    %ss = qtt.construct %a : (!nat) -> !s
    %r = qtt.upcast %ss : !s to !nat
    return %r : !nat
  }
  func.func @case_pair(%n: !nat) -> (!nat, !nat) attributes {qrep.layout = @nat} {
    %p, %original = qtt.case %n : !nat -> (!nat, !nat) {
      S(%s) {
        %pred = qtt.unpack %s : (!s) -> !nat
        %a = qtt.upcast %s : !s to !nat
        qtt.yield %pred, %a : !nat, !nat
      }
      Z(%z) {
        %a = qtt.upcast %z : !z to !nat
        qtt.yield %a, %n : !nat, !nat
      }
    }
    return %p, %original : !nat, !nat
  }
  func.func @case_no_results(%n: !nat) attributes {qrep.layout = @nat} {
    qtt.case %n : !nat -> () {
      S(%s) { qtt.yield }
      Z(%z) { qtt.yield }
    }
    return
  }
  func.func @nested_pred(%n: !nat) -> !nat attributes {qrep.layout = @nat} {
    %r = qtt.case %n : !nat -> (!nat) {
      S(%s) {
        %p = qtt.unpack %s : (!s) -> !nat
        %inner_r = qtt.case %p : !nat -> (!nat) {
          Z(%inner_z) { qtt.yield %p : !nat }
          S(%inner_s) {
            %p2 = qtt.unpack %inner_s : (!s) -> !nat
            qtt.yield %p2 : !nat
          }
        }
        qtt.yield %inner_r : !nat
      }
      Z(%z) { qtt.yield %n : !nat }
    }
    return %r : !nat
  }
}
