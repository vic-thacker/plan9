	TEXT	main+0(SB),$44
	MOV	$0,i+-4(SP)
	MOV	i+-4(SP),R0
	ADDO	$1,R0
	MOV	R0,i+-4(SP)
	MOV	$10,R0
	MOV	i+-4(SP),R1
	MOV	i+-4(SP),R0
	MOV	$0,a+-44(SP)(R0*4)
	RTS	,
