TEXT _CLOSE(SB), 1, $0
MOVW R3, 0(FP)
MOVW $4, R3
SYSCALL
RETURN
