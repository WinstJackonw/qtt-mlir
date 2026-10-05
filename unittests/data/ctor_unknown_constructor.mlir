// Line 2 names a constructor that line 1 does not declare: must be rejected.
!qtt.adt<"foo::Expr", [!qtt.ctor<"foo::Expr", "Lit"(i64)>]>
!qtt.ctor<"foo::Expr", "Nope"()>
