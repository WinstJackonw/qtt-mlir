// Line 1: full definition, must parse and print back identically.
// Line 2: short reference form, must resolve to the same uniqued type.
!qtt.adt<"foo::Expr", [!qtt.ctor<"foo::Expr", "Lit"(i64)>, !qtt.ctor<"foo::Expr", "Nil"()>]>
!qtt.adt<"foo::Expr">
