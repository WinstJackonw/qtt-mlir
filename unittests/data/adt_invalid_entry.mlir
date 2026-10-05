// Line 1: body entries must be constructor types. Line 2: the constructor
// belongs to another ADT and must be rejected.
!qtt.adt<"a", [i64]>
!qtt.adt<"a", [!qtt.ctor<"b", "X"()>]>
