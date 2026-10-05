// Same ADT, same constructor name, different payloads: each reference is a
// distinct CtorType and must roundtrip on its own (payloads never conflict).
!qtt.adt<"expr", [!qtt.ctor<"expr", "Lit"(i64)>]>
!qtt.ctor<"expr", "Lit"(i64)>
!qtt.ctor<"expr", "Lit"(i32)>
