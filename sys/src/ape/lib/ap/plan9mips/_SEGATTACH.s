TEXT _SEGATTACH(SB), 1, $0
MOVW R1, 0(FP)
MOVW $30, R1
SYSCALL
RET
