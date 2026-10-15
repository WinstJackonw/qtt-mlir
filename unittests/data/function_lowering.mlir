!nat = !qtt.adt<"Nat", [!qtt.ctor<"Nat", "Z"()>, !qtt.ctor<"Nat", "S"(!qtt.adt<"Nat">)>]>
!z = !qtt.ctor<"Nat", "Z"()>
!s = !qtt.ctor<"Nat", "S"(!nat)>
module {
  qtt.func @fn_identity : (!nat) -> !nat {
    qtt.func_entry default @default(%n: !nat) -> !nat { qtt.return %n : !nat }
  }
  qtt.func @fn_pred : (!nat) -> !nat {
    qtt.func_entry default @default(%n: !nat) -> !nat { qtt.return %n : !nat }
    qtt.func_entry @s(%n: !s) -> !nat {
      %r = qtt.unpack %n : (!s) -> !nat
      qtt.return %r : !nat
    }
  }
  qtt.func @fn_recursive : (!nat) -> !nat {
    qtt.func_entry @z(%n: !z) -> !nat {
      %r = qtt.upcast %n : !z to !nat
      qtt.return %r : !nat
    }
    qtt.func_entry @s(%n: !s) -> !nat {
      %p = qtt.unpack %n : (!s) -> !nat
      %r = qtt.dispatch_call @fn_recursive(%p) : (!nat) -> !nat
      qtt.return %r : !nat
    }
  }
  qtt.func @fn_direct : (!nat) -> !nat {
    qtt.func_entry default @default(%n: !nat) -> !nat {
      %s = qtt.construct %n : (!nat) -> !s
      %r = qtt.entry_call @fn_pred::@s(%s) : (!s) -> !nat
      qtt.return %r : !nat
    }
  }
  qtt.func @fn_pair : (!nat, !nat) -> (!nat, !nat) {
    qtt.func_entry @z_any(%a: !z, %b: !nat) -> (!nat, !nat) {
      %r = qtt.upcast %a : !z to !nat
      qtt.return %r, %b : !nat, !nat
    }
    qtt.func_entry @s_z(%a: !s, %b: !z) -> (!nat, !nat) {
      %p = qtt.unpack %a : (!s) -> !nat
      %r = qtt.upcast %b : !z to !nat
      qtt.return %p, %r : !nat, !nat
    }
    qtt.func_entry @s_s(%a: !s, %b: !s) -> (!nat, !nat) {
      %p = qtt.unpack %a : (!s) -> !nat
      %q = qtt.unpack %b : (!s) -> !nat
      qtt.return %p, %q : !nat, !nat
    }
  }
  qtt.func @fn_partial : (!nat, !nat) -> (!nat, !nat) {
    qtt.func_entry @s_z(%a: !s, %b: !z) -> (!nat, !nat) {
      %p = qtt.unpack %a : (!s) -> !nat
      %r = qtt.upcast %b : !z to !nat
      qtt.return %p, %r : !nat, !nat
    }
    qtt.func_entry default @default(%a: !nat, %b: !nat) -> (!nat, !nat) {
      qtt.return %a, %b : !nat, !nat
    }
  }
  qtt.func @fn_void : (!nat) -> () {
    qtt.func_entry @s(%a: !s) { qtt.return }
    qtt.func_entry default @default(%a: !nat) { qtt.return }
  }
}
