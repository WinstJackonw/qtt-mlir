// References, independent definitions, and constructors retain ordered arguments.
!qtt.adt<"Either"(i64, f64)>
!qtt.adt<"Either"(i64, f64), [!qtt.ctor<"Either"(i64, f64), "Left"(i64)>, !qtt.ctor<"Either"(i64, f64), "Right"(f64)>]>
!qtt.adt<"Either"(f64, i64), [!qtt.ctor<"Either"(f64, i64), "Left"(f64)>, !qtt.ctor<"Either"(f64, i64), "Right"(i64)>]>
!qtt.ctor<"Either"(i64, f64), "Left"(i64)>
!qtt.adt<"Either"(i64)>
!qtt.adt<"Either">
!qtt.adt<"Either"(i64, f64), [!qtt.ctor<"Either"(f64, i64), "Left"(f64)>]>
!qtt.adt<"Either"(i64, f64), []>
!qtt.adt<"List"(i64), [!qtt.ctor<"List"(i64), "Cons"(i64, !qtt.adt<"List"(i64)>)>]>
!qtt.adt<"Box"(!qtt.adt<"Either"(i64, f64)>)>
