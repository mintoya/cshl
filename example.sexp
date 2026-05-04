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
  (BLOCK () i32 (
    (DECL a i32)
    (DECL b i32)
    (SET a 42)
    (SET b 98)

    (DECL ap (PTR i32))
    (DECL bp (PTR i32))

    (REF ap a)
    (REF bp b)

    (CP ap bp)

    (RETURN a))))


(INIT i8_s (CALL slice (i8)))

(DECL r1 u8)

(CALL r1 main ())

(INIT r2 (CALL 
  (BLOCK () u64 (
  (DECL i u64)
  (DECL g u64)
  (DECL cnd u8)

  (SET g 10)

  (LABEL start)
  (EQUAL cnd i 10)
  (JMP_IF end cnd)


  (ADD i i 1)
  (JMP start)

  (LABEL end)
  (RETURN i))) ()))

; (EXTERN putchar ( i32 ) i32 )

