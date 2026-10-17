// data Box i64 = Some i64 | None
// A Box is represented by one pointer: null is None and a non-null pointer
// points to the Some payload.
!box = !qtt.adt<"Box", [!qtt.ctor<"Box", "Some"(i64)>, !qtt.ctor<"Box", "None"()>]>
!some = !qtt.ctor<"Box", "Some"(i64)>
!none = !qtt.ctor<"Box", "None"()>
!storage = !llvm.ptr
module {
  qrep.layout @box attributes {
    adt = !box, storage = !storage, view = !storage,
    constructors = [{constructor = !some, storage = !storage, view = !storage},
                    {constructor = !none, storage = none, view = none}]
  } {
    qrep.impl borrow(%v: !storage) {
      qrep.yield %v : !storage
    }
    qrep.impl clone(%v: !storage) {
      %copy = func.call @box_clone(%v) : (!llvm.ptr) -> !llvm.ptr
      qrep.yield %copy : !storage
    }
    qrep.impl drop(%v: !storage) {
      func.call @box_drop(%v) : (!llvm.ptr) -> ()
      qrep.yield
    }
    qrep.impl borrow(%v: !storage) {
      qrep.yield %v : !storage
    } {constructor = !some}
    qrep.impl clone(%v: !storage) {
      %copy = func.call @box_clone(%v) : (!llvm.ptr) -> !llvm.ptr
      qrep.yield %copy : !storage
    } {constructor = !some}
    qrep.impl drop(%v: !storage) {
      func.call @box_drop(%v) : (!llvm.ptr) -> ()
      qrep.yield
    } {constructor = !some}
    qrep.impl borrow(%v: none) {
      qrep.yield %v : none
    } {constructor = !none}
    qrep.impl clone(%v: none) {
      qrep.yield %v : none
    } {constructor = !none}
    qrep.impl drop(%v: none) {
      qrep.yield
    } {constructor = !none}
    qrep.impl pack(%value: i64) {
      %size = arith.constant 8 : i64
      %p = func.call @qtt_malloc(%size) : (i64) -> !llvm.ptr
      llvm.store %value, %p : i64, !llvm.ptr
      qrep.yield %p : !storage
    } {constructor = !some}
    qrep.impl unpack(%v: !storage) {
      %value = llvm.load %v : !llvm.ptr -> i64
      qrep.yield %value : i64
    } {constructor = !some}
    qrep.impl upcast(%v: !storage) {
      qrep.yield %v : !storage
    } {constructor = !some}
    qrep.impl downcast(%v: !storage) {
      qrep.yield %v : !storage
    } {constructor = !some}
    qrep.impl pack() {
      %unit = builtin.unrealized_conversion_cast to none
      qrep.yield %unit : none
    } {constructor = !none}
    qrep.impl unpack(%v: none) {
      qrep.yield
    } {constructor = !none}
    qrep.impl upcast(%v: none) {
      %null = llvm.mlir.zero : !storage
      qrep.yield %null : !storage
    } {constructor = !none}
    qrep.impl downcast(%v: !storage) {
      %unit = builtin.unrealized_conversion_cast to none
      qrep.yield %unit : none
    } {constructor = !none}
    qrep.discriminate(%v: !storage) {
      %null = llvm.mlir.zero : !llvm.ptr
      %is_none = llvm.icmp "eq" %v, %null : !llvm.ptr
      cf.cond_br %is_none, ^none, ^some
    ^none:
      %unit = builtin.unrealized_conversion_cast to none
      qrep.dispatch !none, %unit : none
    ^some:
      qrep.dispatch !some, %v : !storage
    }
  }

  func.func private @qtt_malloc(i64) -> !llvm.ptr
  func.func private @qtt_free(!llvm.ptr)

  // These helpers make the null representation explicit while keeping the
  // layout clone/drop implementations executable for both constructors.
  func.func private @box_clone(%p: !llvm.ptr) -> !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    %is_none = llvm.icmp "eq" %p, %null : !llvm.ptr
    cf.cond_br %is_none, ^none, ^some
  ^none:
    return %p : !llvm.ptr
  ^some:
    %size = arith.constant 8 : i64
    %copy = func.call @qtt_malloc(%size) : (i64) -> !llvm.ptr
    %value = llvm.load %p : !llvm.ptr -> i64
    llvm.store %value, %copy : i64, !llvm.ptr
    return %copy : !llvm.ptr
  }
  func.func private @box_drop(%p: !llvm.ptr) {
    %null = llvm.mlir.zero : !llvm.ptr
    %is_none = llvm.icmp "eq" %p, %null : !llvm.ptr
    cf.cond_br %is_none, ^done, ^free
  ^free:
    func.call @qtt_free(%p) : (!llvm.ptr) -> ()
    cf.br ^done
  ^done:
    return
  }

  func.func @make_box(%value: i64) -> !box attributes {qrep.layout = @box} {
    %b = qtt.construct %value : (i64) -> !some
    %v = qtt.upcast %b : !some to !box
    return %v : !box
  }
  func.func @make_none() -> !box attributes {qrep.layout = @box} {
    %b = qtt.construct : () -> !none
    %v = qtt.upcast %b : !none to !box
    return %v : !box
  }
  func.func @read_box(%v: !box) -> i64 attributes {qrep.layout = @box} {
    %r = qtt.case %v : !box -> (i64) {
      Some(%b) {
        %value = qtt.unpack %b : (!some) -> i64
        qtt.yield %value : i64
      }
      None(%n) {
        %minus_one = arith.constant -1 : i64
        qtt.yield %minus_one : i64
      }
    }
    return %r : i64
  }
  func.func @test_none() -> i32 attributes {qrep.layout = @box, llvm.emit_c_interface} {
    %v = func.call @make_none() : () -> !box
    %copy = qrep.clone %v : (!box) -> !box
    %value = func.call @read_box(%copy) : (!box) -> i64
    qrep.drop %v : (!box) -> ()
    qrep.drop %copy : (!box) -> ()
    %minus_one = arith.constant -1 : i64
    %ok = arith.cmpi eq, %value, %minus_one : i64
    %result = arith.extui %ok : i1 to i32
    return %result : i32
  }
  func.func @test_box() -> i32 attributes {qrep.layout = @box, llvm.emit_c_interface} {
    %value = arith.constant 42 : i64
    %v = func.call @make_box(%value) : (i64) -> !box
    %copy = qrep.clone %v : (!box) -> !box
    %read = func.call @read_box(%copy) : (!box) -> i64
    qrep.drop %v : (!box) -> ()
    qrep.drop %copy : (!box) -> ()
    %ok = arith.cmpi eq, %read, %value : i64
    %result = arith.extui %ok : i1 to i32
    return %result : i32
  }
}
