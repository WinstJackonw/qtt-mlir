// Forward reference, definition, then conflicts in count, name, payload and order.
!qtt.adt<"Expr">
!qtt.adt<"Expr", [!qtt.ctor<"Expr", "Lit"(i64)>, !qtt.ctor<"Expr", "Nil"()>]>
!qtt.adt<"Expr", [!qtt.ctor<"Expr", "Lit"(i64)>]>
!qtt.adt<"Expr", [!qtt.ctor<"Expr", "Other"(i64)>, !qtt.ctor<"Expr", "Nil"()>]>
!qtt.adt<"Expr", [!qtt.ctor<"Expr", "Lit"(i32)>, !qtt.ctor<"Expr", "Nil"()>]>
!qtt.adt<"Expr", [!qtt.ctor<"Expr", "Nil"()>, !qtt.ctor<"Expr", "Lit"(i64)>]>
