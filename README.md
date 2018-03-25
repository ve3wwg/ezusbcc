OPEN SOURCED EZ-USB WAVEFORM COMPILER
=====================================

SOURCE CODE FORMAT (UPPERCASE ONLY):

    ; Comments..
    
     	.PSEUDOOP	<arg>			; Comment
     	...
    	OPCODE		operand1 ... operandn  	; comment
    
    PSEUDO OPS:
    
    	.TRICTL		{ 0 | 1 }		; Affects Outputs
    	.GPIFREADYCFG5	{ 0 | 1 }		; TC when 1, else RDY5
    	.GPIFREADYCFG7	{ 0 | 1 }		; INTRDY available when 1
    	.EPXGPIFFLGSEL	{ PF | EF | FF }	; Selected FIFO flag
    	.EP		{ 2 | 4 | 6 | 8 }	; Default 2
    	.WAVEFORM	n			; Names output C code array
    
    NDP OPCODES:
    	[S][+][G][D][N]   	[count=1] [OEn] [CTLn]
    
    DP OPCODES:
    	J[S][+][G][D][N][*]   	A OP B [OEn] [CTLn] $1 $2

    where:
    	A/B is one of:		RDY0 RDY1 RDY2 RDY3 RDY4 RDY5 TC PF EF FF INTRDY
    				  These are subject to environment.
    and  OP is one of:		AND OR XOR /AND (/A AND B)
    
    OPCODE CHARACTERS:

	S	SGL (Single)
	+	INCAD
	G	GINT
	D	Data
	N	Next/SGLCRC
	*	Re-execute (DP only)
