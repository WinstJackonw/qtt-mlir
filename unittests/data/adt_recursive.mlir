// Self-recursion, mutual recursion, and the initialized empty ADT.
!qtt.adt<"Self", [!qtt.ctor<"Self", "Next"(!qtt.adt<"Self">)>]>
!qtt.adt<"A", [!qtt.ctor<"A", "ToB"(!qtt.adt<"B", [!qtt.ctor<"B", "ToA"(!qtt.adt<"A">)>]>)>]>
!qtt.adt<"Void", []>
