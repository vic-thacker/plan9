TEXT _CHDIR(SB), 1, $0
MOVW R3, 0(FP)
MOVW $3, R3
SYSCALL
RETURN
