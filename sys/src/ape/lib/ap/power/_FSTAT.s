TEXT _FSTAT(SB), 1, $0
MOVW R3, 0(FP)
MOVW $11, R3
SYSCALL
RETURN
