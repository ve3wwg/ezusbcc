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

DECOMPILING:
============

There is limited capability to decompile a gpif.c module into
 "assembler form". This decompile is unable to know when
TRICTL is in effect, unless OE3 or OE2 are referenced. Other
modes such as PF|EF|FF is also not known but emitted as such.

To decompile, specify a file name:

    $ ./ezusbcc gpif.c
    128 bytes.
    ; WaveForm 0
    01000007	Z	1 CTL2 CTL1 CTL0 
    02000002	Z	2 CTL1 
    01020002	D	1 CTL1 
    01000007	Z	1 CTL2 CTL1 CTL0 
    3F013F07	J	INTRDY AND INTRDY CTL2 CTL1 CTL0 $7 $7
    01000007	Z	1 CTL2 CTL1 CTL0 
    01000007	Z	1 CTL2 CTL1 CTL0 
    ; WaveForm 1
    03020005	D	3 CTL2 CTL0 
    01020007	D	1 CTL2 CTL1 CTL0 
    3F053F07	JN	INTRDY AND INTRDY CTL2 CTL1 CTL0 $7 $7
    01000007	Z	1 CTL2 CTL1 CTL0 
    01000007	Z	1 CTL2 CTL1 CTL0 
    01000007	Z	1 CTL2 CTL1 CTL0 
    01000007	Z	1 CTL2 CTL1 CTL0 
    ; WaveForm 2
    ...
