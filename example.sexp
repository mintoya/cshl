(INIT i8    (SINT 8))
(INIT i16   (SINT 16))
(INIT i32   (SINT 32))
(INIT i64   (SINT 64))
(INIT u8    (UINT 8))
(INIT u16   (UINT 16))
(INIT u32   (UINT 32))
(INIT u64   (UINT 64))

(INIT type (TYPE))

(INIT slice (BLOCK (type) type (
    (RETURN (STRUCT u64 (PTR (ARG 0)))))))

(INIT main 
  (BLOCK () u64 (
    ; 1. Allocate 'a' on the stack and set it to 42
    (INIT a 42)

    ; 2. Declare an uninitialized pointer 'ptr'
    (DECL ptr (PTR u64))

    ; 3. Set 'ptr' to the address of 'a' (ptr = &a)
    (SET ptr (REF a))

    ; 4. Declare 'b' and initialize it to 99
    (INIT b 99)

    ; 5. Copy 'b' into the memory pointed to by 'ptr' (*ptr = b)
    ; Note: CP takes two pointers. So we pass 'ptr' (which holds the address of a), 
    ; and '(REF b)' (the address of our new value).
    (CP ptr (REF b))

    ; If memory copying and references work, returning 'a' will now yield 99, not 42.
    (RETURN a)
  ))
)

; Actually call main so the interpreter executes it
(CALL main ())
