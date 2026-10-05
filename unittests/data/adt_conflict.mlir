// Line 1 defines "foo::Expr"; line 2 redefines it and must be rejected.
!qtt.adt<"foo::Expr", [!qtt.ctor<"foo::Expr", "Lit"(i64)>, !qtt.ctor<"foo::Expr", "Nil"()>]>
!qtt.adt<"foo::Expr", [!qtt.ctor<"foo::Expr", "Other"()>]>
