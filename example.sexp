; symbol that is an alias for the type type 
(INIT type (TYPE))

(INIT i8    (SINT 8))
(INIT i16   (SINT 16))
(INIT i32   (SINT 32))
(INIT i64   (SINT 64))
(INIT u8    (UINT 8))
(INIT u16   (UINT 16))
(INIT u32   (UINT 32))
(INIT u64   (UINT 64))


; function that takes a type and returns a slice of that type  
(INIT slice (BLOCK (type) type (
    (RETURN (STRUCT u64 (PTR (ARG 0)))))))

(INIT main 
  (BLOCK () u64 (
    (INIT a 42)
    (INIT b 98)

    (CP (REF a) (REF b))

    (RETURN a))))

(INIT result (CALL main ())) ; result remains  on the stack
