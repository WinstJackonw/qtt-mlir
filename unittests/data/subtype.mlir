// Inputs for mlir::qtt::isSubType checks: the ctor of "expr" is a subtype of
// the "expr" ADT, but not of the unrelated "other" ADT.
!qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>
!qtt.adt<"other", [!qtt.ctor<"other", "Lit"(i64)>]>
!qtt.ctor<"expr", "Lit"(i64)>
