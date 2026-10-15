!nat = !qtt.adt<"Nat", [!qtt.ctor<"Nat", "Z"()>, !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>]>
!z = !qtt.ctor<"Nat", "Z"()>
!s = !qtt.ctor<"Nat", "S"(!nat)>
module {
  qtt.func @identity : (!nat) -> !nat {
    qtt.func_entry default @fallback(%n: !nat) -> !nat {
      qtt.return %n : !nat
    }
  }
  qtt.func @pred : (!nat) -> !nat {
    qtt.func_entry @successor(%n: !s) -> !nat {
      %p = qtt.unpack %n : (!s) -> !nat
      qtt.return %p : !nat
    }
    qtt.func_entry default @fallback(%n: !nat) -> !nat {
      qtt.return %n : !nat
    }
  }
  qtt.func @total : (!nat) -> !nat {
    qtt.func_entry @zero(%n: !z) -> !nat {
      %r = qtt.upcast %n : !z to !nat
      qtt.return %r : !nat
    }
    qtt.func_entry @successor(%n: !s) -> !nat {
      %p = qtt.unpack %n : (!s) -> !nat
      %r = qtt.dispatch_call @total(%p) : (!nat) -> !nat
      qtt.return %r : !nat
    }
  }
  qtt.func @pair : (!nat, !nat, i64) -> (i64, i64) {
    qtt.func_entry @zero_any(%a: !z, %b: !nat, %x: i64) -> (i64, i64) {
      %tag = arith.constant 0 : i64
      qtt.return %x, %tag : i64, i64
    }
    qtt.func_entry @succ_zero(%a: !s, %b: !z, %x: i64) -> (i64, i64) {
      %tag = arith.constant 1 : i64
      qtt.return %x, %tag : i64, i64
    }
    qtt.func_entry @succ_succ(%a: !s, %b: !s, %x: i64) -> (i64, i64) {
      %tag = arith.constant 2 : i64
      qtt.return %x, %tag : i64, i64
    }
  }
  qtt.func @scalar : (i64) -> i64 {
    qtt.func_entry default @fallback(%x: i64) -> i64 {
      %r = qtt.dispatch_call @scalar(%x) : (i64) -> i64
      qtt.return %r : i64
    }
  }
  qtt.func @entry_resolve : (!nat) -> () {
    qtt.func_entry @zero(%n: !z) {
      qtt.return
    }
    qtt.func_entry default @fallback(%n: !nat) {
      %z = qtt.construct : () -> !z
      qtt.entry_call @zero(%z) : (!z) -> ()
      qtt.return
    }
  }
  qtt.func @unit : () -> () {
    qtt.func_entry default @fallback() {
      qtt.return
    }
  }
  qtt.func @opaque : (!qtt.adt<"Opaque">) -> !qtt.adt<"Opaque"> {
    qtt.func_entry default @fallback(%x: !qtt.adt<"Opaque">) -> !qtt.adt<"Opaque"> {
      qtt.return %x : !qtt.adt<"Opaque">
    }
  }
  func.func @caller(%n: !nat, %s: !s) -> !nat {
    %a = qtt.dispatch_call @pred(%n) : (!nat) -> !nat
    %b = qtt.entry_call @pred::@successor(%s) : (!s) -> !nat
    %c = qtt.entry_call @identity::@fallback(%b) : (!nat) -> !nat
    return %c : !nat
  }
  func.func @unit_resolve() {
    qtt.entry_call @unit::@fallback() : () -> ()
    return
  }
}
