(INIT i8    (SINT 8))
(INIT i16   (SINT 16))
(INIT i32   (SINT 32))
(INIT i64   (SINT 64))

(INIT u8    (UINT 8))
(INIT u16   (UINT 16))
(INIT u32   (UINT 32))
(INIT u64   (UINT 64))

(INIT type (TYPE))

(INIT slice (BLOCK '(type) type (
    (RETURN 
      (STRUCT 
        u64
        (PTR (ARG 0))))
  )))
 

(INIT main 
  (BLOCK '((CALL slice '(CALL slice '(i8)))) i32 (
    (RETURN 0)
  )))

