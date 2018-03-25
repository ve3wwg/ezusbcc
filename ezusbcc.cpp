//////////////////////////////////////////////////////////////////////
// ezusbcc.cpp -- GPIF assembler for EZ-USB
// Date: Thu Mar 22 22:29:43 2018   (C) Warren W. Gay VE3WWG
///////////////////////////////////////////////////////////////////////
// 
// This is a simple assember, to generate wave tables. This program
// accepts the source code from stdin and generates the C code on
// stdout. Listing and errors are put to stderr.
//
// SOURCE CODE FORMAT (UPPERCASE ONLY):
//
// ; Comments..
//
// 	.PSEUDOOP	<arg>			; Comment
// 	...
//	OPCODE		operand1 ... operandn  	; comment
//
// PSEUDO OPS:
//
//	.TRICTL		{ 0 | 1 }		; Affects Outputs
//	.GPIFREADYCFG5	{ 0 | 1 }		; TC when 1, else RDY5
//	.GPIFREADYCFG7	{ 0 | 1 }		; INTRDY available when 1
//	.EPXGPIFFLGSEL	{ PF | EF | FF }	; Selected FIFO flag
//	.EP		{ 2 | 4 | 6 | 8 }	; Default 2
//	.WAVEFORM	n			; Names output C code array
//
// NDP OPCODES:
//	[S][+][G][D][N]   	[count=1] [OEn] [CTLn]
// or	Z			[count=1] [OEn] [CTLn]
//
// DP OPCODES:
//	J[S][+][G][D][N][*]   	A OP B [OEn] [CTLn] $1 $2
// where:
//	A/B is one of:		RDY0 RDY1 RDY2 RDY3 RDY4 RDY5 TC PF EF FF INTRDY
//				  These are subject to environment.
// and  OP is one of:		AND OR XOR /AND (/A AND B)
//
// OPCODE CHARACTERS:
//	S	SGL (Single)
//	+	INCAD
//	G	GINT
//	D	Data
//	N	Next/SGLCRC
//	Z	Placeholder when none of the above
//	*	Re-execute (DP only)
//
// TO DECOMPILE:
//
//
//    specify a file name instead of placing the input on stdin.
//    For example:
//
//    $ ./ezusbcc gpif.c
//
// Note that the decompile doesn't figure out the environment
// that it runs within. As a result, some values will show as
// RDY5|TC or PF|EF|FF where it can't know. It may also get
// the OEx CTLx wrong. If it sees OE3 or OE2, it will assume
// from that point on that TRICTL is in effect.

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <array>

static void uncompile(int argc,char **argv);

enum class PseudoOps {
	Trictl,			// TRICTL
	GpifReadyCfg5,		// 
	GpifReadyCfg7,		// 
	EpxGpifFlgSel,		// PF, EF or FF
	Ep,			// 2, 4, 6 or 8
	WaveForm,		// x
};

static const std::map<std::string,int> pseudotab = {
	{ ".TRICTL",		int(PseudoOps::Trictl) },
	{ ".GPIFREADYCFG5",	int(PseudoOps::GpifReadyCfg5) },
	{ ".GPIFREADYCFG7",	int(PseudoOps::GpifReadyCfg7) },
	{ ".EPXGPIFFLGSEL",	int(PseudoOps::EpxGpifFlgSel) },
	{ ".EP",		int(PseudoOps::Ep) },
	{ ".WAVEFORM",		int(PseudoOps::WaveForm) },
};

static const std::map<std::string,int> flgsel = {
	{ "PF",	0 },
	{ "EF", 1 },
	{ "FF", 2 },
};

static const std::map<unsigned,std::map<std::string,unsigned>> oetab = {
	{ 0, {			// TRICTL=0
		{ "CTL5", 5 },
		{ "CTL4", 4 },
		{ "CTL3", 3 },
		{ "CTL2", 2 },
		{ "CTL1", 1 },
		{ "CTL0", 0 },
	  }
	},
	{ 1, {			// TRICTL=1
		{ "OE3",  7 },
		{ "OE2",  6 },
		{ "OE1",  5 },
		{ "OE0",  4 },
		{ "CTL3", 3 },
		{ "CTL2", 2 },
		{ "CTL1", 1 },
		{ "CTL0", 0 },
	  }
	}
};

static const std::map<std::string,unsigned> functab = {
	{ "AND",   0b00 },
	{ "OR",    0b01 },
	{ "XOR",   0b10 },
	{ "/AND",  0b11 }
};

static const std::map<unsigned/*GpifReadyCfg5*/,
	std::map<unsigned/*EPxGPIFFLGSEL*/,
	std::map<unsigned/*GPIFREADYCFG.7*/,
	std::map<std::string,unsigned>
	>>> opertab = {
		{ 0/*gpifReadyCfg5=0*/,	{
			{ 0/*EPxGPIFFLGSEL=0 (PF)*/, {
				{ 0/*GPIFREADYCFG.7=0*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "RDY5",  0b101 },	// GPIFREADYCFG.5 = 0
					{ "PF",    0b110 },	// EPxGPIFFLGSEL
				}},
				{ 1/*GPIFREADYCFG.7=1*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "RDY5",  0b101 },	// GPIFREADYCFG.5 = 0
					{ "PF",    0b110 },	// EPxGPIFFLGSEL=0
				}},
			}},
			{ 1/*EPxGPIFFLGSEL=1 (EF)*/, {
				{ 0/*GPIFREADYCFG.7=0*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "RDY5",  0b101 },	// GPIFREADYCFG.5 = 0
					{ "EF",    0b110 },	// EPxGPIFFLGSEL=1
				}},
				{ 1/*GPIFREADYCFG.7=1*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "RDY5",  0b101 },	// GPIFREADYCFG.5 = 0
					{ "EF",    0b110 },	// EPxGPIFFLGSEL = 1
					{ "INTRDY",0b111 },
				}},
			}},
			{ 2/*EPxGPIFFLGSEL=1 (FF)*/, {
				{ 0/*GPIFREADYCFG.7=0*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "RDY5",  0b101 },	// GPIFREADYCFG.5 = 0
					{ "FF",    0b110 },	// EPxGPIFFLGSEL=2
				}},
				{ 1/*GPIFREADYCFG.7=1*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "RDY5",  0b101 },	// GPIFREADYCFG.5 = 0
					{ "FF",    0b110 },	// EPxGPIFFLGSEL = 2
					{ "INTRDY",0b111 },
				}},
			}},
		}},
		{ 1/*GpifReadyCfg5=1*/, {
			{ 0/*EPxGPIFFLGSEL=0 (PF)*/, {
				{ 0/*GPIFREADYCFG.7=0*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "TC",    0b101 },	// GPIFREADYCFG.5 = 1
					{ "PF",    0b110 },	// EPxGPIFFLGSEL
				}},
				{ 1/*GPIFREADYCFG.7=1*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "TC",    0b101 },	// GPIFREADYCFG.5 = 1
					{ "PF",    0b110 },	// EPxGPIFFLGSEL=0
				}},
			}},
			{ 1/*EPxGPIFFLGSEL=1 (EF)*/, {
				{ 0/*GPIFREADYCFG.7=0*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "TC",    0b101 },	// GPIFREADYCFG.5 = 1
					{ "EF",    0b110 },	// EPxGPIFFLGSEL=1
				}},
				{ 1/*GPIFREADYCFG.7=1*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "TC",    0b101 },	// GPIFREADYCFG.5 = 1
					{ "EF",    0b110 },	// EPxGPIFFLGSEL = 1
					{ "INTRDY",0b111 },
				}},
			}},
			{ 2/*EPxGPIFFLGSEL=1 (FF)*/, {
				{ 0/*GPIFREADYCFG.7=0*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "TC",    0b101 },	// GPIFREADYCFG.5 = 1
					{ "FF",    0b110 },	// EPxGPIFFLGSEL=2
				}},
				{ 1/*GPIFREADYCFG.7=1*/, {
					{ "RDY0",  0b000 },
					{ "RDY1",  0b001 },
					{ "RDY2",  0b010 },
					{ "RDY3",  0b011 },
					{ "RDY4",  0b100 },
					{ "TC",    0b101 },	// GPIFREADYCFG.5 = 1
					{ "FF",    0b110 },	// EPxGPIFFLGSEL = 2
					{ "INTRDY",0b111 },
				}},
			}},
		}}
	};

union u_opcode {
	uint8_t			byte;
	struct s_opcode {
		uint8_t	dp : 1;		// 1 == DP
		uint8_t	data : 1;	// 1 == Drive FIFO / Sample
		uint8_t	next : 1;	// 1 == move next to FIFO, or use UDMACRCH:L
		uint8_t	incad : 1;	// 1 == increment GPIFADR
		uint8_t	gint : 1;	// 1 == generate a GPIFWF
		uint8_t	sgl : 1;	// 1 == use SGLDATH:L / UDMACRCH:L
		uint8_t	reserved : 2;	// Ignored
	}			bits;
};

union u_logfunc {
	enum class e_logfunc {
		a_and_b = 0,
		a_or_b = 1,
		a_xor_b = 2,
		na_and_b = 3
	};
	uint8_t			byte;
	struct s_logfunc {
		uint8_t	termb : 3;	// TERM B
		uint8_t	terma : 3;	// TERM A
		uint8_t	lfunc : 2;	// e_log_func
	}			bits;
};

union u_branch {
	uint8_t			byte;
	struct s_branch {
		uint8_t	branch0 : 3;
		uint8_t	branch1 : 3;
		uint8_t	reserved : 1;
		uint8_t	reexecute : 1;
	}			bits;
};

union u_output {
	uint8_t			byte;
	struct s_output1 {
		uint8_t	ctl0 : 1;
		uint8_t	ctl1 : 1;
		uint8_t	ctl2 : 1;
		uint8_t	ctl3 : 1;
		uint8_t	oes0 : 1;
		uint8_t	oes1 : 1;
		uint8_t	oes2 : 1;
		uint8_t	oes3 : 1;
	}			bits1;
	struct s_output0 {
		uint8_t	ctl0 : 1;
		uint8_t	ctl1 : 1;
		uint8_t	ctl2 : 1;
		uint8_t	ctl3 : 1;
		uint8_t	ctl4 : 1;
		uint8_t	ctl5 : 1;
		uint8_t	reserved : 2;
	}			bits0;
};

struct s_instr {
	std::string		stropcode;
	std::vector<std::string> stroperands;
	std::string		strcomment;
	std::string		error;

	u_branch		branch;
	u_opcode		opcode;
	u_logfunc		logfunc;
	u_output		output;

	void clear() {
		stropcode.clear();
		stroperands.clear();
		strcomment.clear();
		opcode.byte = 0;
		logfunc.byte = 0;
		branch.byte = 0;
		output.byte = 0;
	};
};

static bool
parse(std::istream& istr,s_instr& instr) {
	std::stringstream ss;
	char pk;

	instr.clear();

	for (;;) {
		if ( istr.eof() )
			return false;
		istr >> instr.stropcode;
		if ( instr.stropcode[0] != ';' && !instr.stropcode.empty() )
			break;
		while ( !istr.eof() && istr.get() != '\n' )
			;
	}

	while ( !istr.eof() && (pk = istr.peek()) != '\n' ) {
		std::string token;

		istr >> token;
		if ( token[0] != ';' ) {
			instr.stroperands.push_back(token);
		} else	{
			while ( (pk = istr.peek()) != '\n' ) {
				ss << pk;
				istr.get();
			}
			if ( ss.tellp() > 0 )
				instr.strcomment = ss.str();
			break;
		}
	}
		
	while ( !istr.eof() ) {
		if ( istr.get() == '\n' )
			break;
	}
	return true;
}

int
main(int argc,char **argv) {
	if ( argc > 1 )
		uncompile(argc,argv);

	std::vector<s_instr> instrs;
	std::map<unsigned,unsigned> environ = {
		{ unsigned(PseudoOps::Trictl),		0u },
		{ unsigned(PseudoOps::GpifReadyCfg5),	0u },
		{ unsigned(PseudoOps::GpifReadyCfg7),	0u },
		{ unsigned(PseudoOps::EpxGpifFlgSel),	0u },
		{ unsigned(PseudoOps::Ep),		2u },
		{ unsigned(PseudoOps::WaveForm),	0u },
	};
	unsigned& trictl = environ.at(unsigned(PseudoOps::Trictl));
	unsigned& gpifreadycfg5 = environ.at(unsigned(PseudoOps::GpifReadyCfg5));
	unsigned& gpifreadycfg7 = environ.at(unsigned(PseudoOps::GpifReadyCfg7));
	unsigned& epxgpifflgsel= environ.at(unsigned(PseudoOps::EpxGpifFlgSel));
	unsigned& waveformx= environ.at(unsigned(PseudoOps::WaveForm));
	unsigned state = 0;

	{
		s_instr instr;

		while ( parse(std::cin,instr) ) {
			auto it = pseudotab.find(instr.stropcode);
			if ( it != pseudotab.end() ) {
				PseudoOps pseudoop = PseudoOps(it->second);
				char *ep;
				unsigned value = 0;

				if ( instr.stroperands.size() != 1 ) {
					std::cerr << "*** ERROR: Only one operand valid for pseudo op " << instr.stropcode << '\n';
					exit(1);					
				}
				if ( pseudoop != PseudoOps::EpxGpifFlgSel ) {
					value = strtoul(instr.stroperands[0].c_str(),&ep,10);
					bool fail = false;

					if ( pseudoop != PseudoOps::WaveForm ) {
						fail = value > ( pseudoop != PseudoOps::Ep ? 1 : 8 );

						if ( !fail && pseudoop == PseudoOps::Ep && (value & 1) )
							fail = true;		// Only EP 2, 4, 6 or 8
					} else	fail = false;

					if ( (ep && *ep) || fail ) {
						std::cerr << "*** ERROR: Invalid operand '" << instr.stroperands[0] << "' for " << instr.stropcode << '\n';
						exit(1);
					}
				} else	{
					auto it = flgsel.find(instr.stroperands[0]);
					if ( it == flgsel.end() ) {
						std::cerr << "*** ERROR: Operand of " << instr.stropcode << " must be PF, EF, or FF\n";
						exit(1);
					}
					value = !!it->second;
				}
				environ[unsigned(pseudoop)] = value;
				continue;
			} else	{
				instrs.push_back(instr);
			}
		}
	}

	for ( auto& instr : instrs ) {
		// Parse opcode:
		for ( auto c : instr.stropcode ) {
			switch ( c ) {
			case 'J':
				instr.opcode.bits.dp = 1;
				break;
			case 'S':
				instr.opcode.bits.sgl = 1;
				break;
			case '+':
				instr.opcode.bits.incad = 1;
				break;
			case 'G':
				instr.opcode.bits.gint = 1;
				break;
			case 'N':
				instr.opcode.bits.next = 1;
				break;
			case 'D':
				instr.opcode.bits.data = 1;
				break;
			case 'Z':
				break;
			case '*':
				if ( instr.opcode.bits.dp ) {
					instr.branch.bits.reexecute = 1;
					break;
				}
				// Fall thru
			default:
				{
					std::stringstream ss;

					ss << "Unknown opcode '" << c << "'";
					instr.error = ss.str();
				}
			}			
		}

		// Parse operands:
		if ( instr.opcode.bits.dp ) {
			// DP
			if ( instr.stroperands.size() < 3 ) {
				instr.error = "missing operand A func B";
				continue;
			}
			std::string& opera = instr.stroperands[0];
			std::string& func  = instr.stroperands[1];
			std::string& operb = instr.stroperands[2];
			auto& opermap = opertab.at(gpifreadycfg5).at(epxgpifflgsel).at(gpifreadycfg7);

			{
				auto it = opermap.find(opera);
				if ( it == opermap.end() ) {
					std::stringstream ss;
					ss << "Invalid operand A '" << opera << "'";
					instr.error = ss.str();
					continue;
				}
				instr.logfunc.bits.terma = it->second;
			}

			{
				auto it = opermap.find(operb);
				if ( it == opermap.end() ) {
					std::stringstream ss;
					ss << "Invalid operand B '" << operb << "'\n"
						<< "  Must be one of: ";
					for ( auto& pair : opermap )
						ss << pair.first << ' ';
					instr.error = ss.str();
					continue;
				}
				instr.logfunc.bits.termb = it->second;
			}

			{
				auto it = functab.find(func);

				if ( it == functab.end() ) {
					std::stringstream ss;
					ss << "Invalid function '" << func << "'";
					instr.error = ss.str();
					continue;
				}
				instr.logfunc.bits.lfunc = it->second;
			}

			instr.branch.bits.branch0 = instr.branch.bits.branch1 = 7;	// Default to state 7
			unsigned statex = 0;

			for ( unsigned ox=3; ox<instr.stroperands.size(); ++ox ) {
				std::string& operand = instr.stroperands[ox];
				const auto& oemap = oetab.at(trictl);

				if ( !operand.empty() && operand[0] == '$' ) {
					char *cp = nullptr;
					unsigned long state = strtoul(operand.c_str()+1,&cp,10);

					if ( (cp && *cp) || state > 7 || (state != 7 && state > instrs.size()) ) {
						std::stringstream ss;
						ss << "invalid target state '" << operand << "'";
						instr.error = ss.str();
						break;
					}

					switch ( statex++ ) {
					case 0:
						instr.branch.bits.branch0 = state;
						break;
					case 1:
						instr.branch.bits.branch1 = state;
						break;
					default:
						{
							std::stringstream ss;
							ss << "Too many target states starting with '" << operand << "'";
							instr.error = ss.str();
						}
					}
					if ( !instr.error.empty() )
						break;
				} else	{
					auto it = oemap.find(operand);
					if ( it == oemap.end() ) {
						std::stringstream ss;
						ss << "invalid operand '" << operand << "' (TRICTL=" << trictl << ")\n"
							<< "  Must be one of: ";
						for ( auto& pair : oemap )
							ss << pair.first << ' ';
						instr.error = ss.str();
						break;
					}
					unsigned shift = it->second;
					instr.output.byte |= 1 << shift;
				}
			}
			if ( instr.error.empty() && statex != 2 ) {
				std::stringstream ss;
				instr.error = "Branch0 and/or branch1 states were not specified.";
			}
		} else	{
			// NDP
			instr.branch.byte = 1;		// Default to a 1-count

			for ( auto& operand : instr.stroperands ) {
				if ( operand[0] >= '0' && operand[0] <= '9' ) {
					// Count
					char *ep;
					unsigned count = strtoul(operand.c_str(),&ep,10);
					std::stringstream ss;

					if ( ep && *ep ) {
						ss << "Invalid count '" << operand << "'";
						instr.error = ss.str();
					} else if ( count > 256 ) {
						ss << "Invalid count value " << count;
						instr.error = ss.str();
					} else	{
						if ( count == 256 )
							count = 0u;
						instr.branch.byte = count;
					}
				} else	{
					// Bits
					const auto& oemap = oetab.at(trictl);

					auto it = oemap.find(operand);
					if ( it == oemap.end() ) {
						std::stringstream ss;
						ss << "invalid operand '" << operand << "' (TRICTL=" << trictl << ")\n"
							<< "  Must be one of: ";
						for ( auto& pair : oemap )
							ss << pair.first << ' ';
						instr.error = ss.str();
						break;
					}
					unsigned shift = it->second;
					instr.output.byte |= 1 << shift;
				}
			}
		}
	}

	auto revlookup = [&](unsigned ps) -> std::string {
		for ( auto pair : pseudotab ) {
			const std::string& op = pair.first;
			const unsigned u = pair.second;
			
			if ( ps == u )
				return op;
		}
		assert(0);
	};

	std::cerr << ";\n;\tEnvironment in effect:\n"
		<< ";\n";

	for ( auto& pair : environ ) {
		const unsigned ps = pair.first;
		const unsigned value = pair.second;
		const std::string& op = revlookup(ps);
		const std::array<const char *,3> opers = { { "PF", "EF", "FF" } };

		switch ( PseudoOps(ps) ) {
		case PseudoOps::Trictl:
		case PseudoOps::GpifReadyCfg5:
		case PseudoOps::GpifReadyCfg7:
		case PseudoOps::Ep:
		case PseudoOps::WaveForm:
			std::cerr << '\t' << op << '\t' << value << '\n';
			break;
		case PseudoOps::EpxGpifFlgSel:
			std::cerr << '\t' << op << '\t' << opers[value] << '\n';
			break;
		}
	}
	std::cerr << ";\n";

	for ( auto& instr : instrs ) {
		std::cerr << '$' << state++ << "  ";

		std::cerr.width(2);
		std::cerr.fill('0');
		std::cerr << std::uppercase << std::hex << unsigned(instr.branch.byte);

		std::cerr.fill('0');
		std::cerr.width(2);
		std::cerr << std::hex << unsigned(instr.opcode.byte);

		std::cerr.width(2);
		std::cerr.fill('0');
		std::cerr << std::hex << unsigned(instr.logfunc.byte);

		std::cerr.fill('0');
		std::cerr.width(2);
		std::cerr << std::hex << unsigned(instr.output.byte);

		std::cerr << '\t' << instr.stropcode << '\t';
		for ( auto& operand : instr.stroperands )
			std::cerr << operand << " ";
		if ( !instr.strcomment.empty() )
			std::cerr << "\t; " << instr.strcomment;
		std::cerr << '\n';
		if ( !instr.error.empty() )
			std::cerr << "*** ERROR: " << instr.error << '\n';
		if ( state > 7 ) {
			std::cerr << "*** ERROR: Too many states. Limit is 6 states max.\n";
			exit(1);
		}
	}

	instrs.resize(8);

	std::cout << "static unsigned char waveform" << waveformx << "[32] = { \n\t";

	for ( auto& instr : instrs ) {
		std::cout << "0x";
		std::cout.width(2);
		std::cout.fill('0');
		std::cout << std::uppercase << std::hex << unsigned(instr.branch.byte) << ',';
	}			
	std::cout << "\n\t";

	for ( auto& instr : instrs ) {
		std::cout << "0x";
		std::cout.fill('0');
		std::cout.width(2);
		std::cout << std::hex << unsigned(instr.opcode.byte) << ',';
	}
	std::cout << "\n\t";

	for ( auto& instr : instrs ) {
		std::cout << "0x";
		std::cout.fill('0');
		std::cout.width(2);
		std::cout << std::hex << unsigned(instr.output.byte) << ',';
	}
	std::cout << "\n\t";

	for ( auto& instr : instrs ) {
		std::cout << "0x";
		std::cout.width(2);
		std::cout.fill('0');
		std::cout << std::hex << unsigned(instr.logfunc.byte) << ',';
	}

	std::cout << "\n};\n\n";

	return 0;
}

static void
decompile(unsigned waveformx,uint8_t data[32]) {
	unsigned ux;
	bool trictl = false;

	std::cout << "; WaveForm " << waveformx << '\n';

	for ( ux=0; ux<32-4; ux += 4 ) {
		u_branch branch;
		u_opcode opcode;
		u_logfunc logfunc;
		u_output output;
		std::stringstream opc, oper;

		branch.byte= data[ux+0];
		opcode.byte = data[ux+1];
		logfunc.byte = data[ux+2];
		output.byte = data[ux+3];

		if ( opcode.bits.dp == 1 )
			opc << 'J';
		if ( opcode.bits.sgl )
			opc << 'S';
		if ( opcode.bits.incad )
			opc << '+';
		if ( opcode.bits.gint )
			opc << 'G';
		if ( opcode.bits.next )
			opc << 'N';
		if ( opcode.bits.data )
			opc << 'D';
		if ( !opcode.byte )
			opc << 'Z';
		if ( opcode.bits.dp && branch.bits.reexecute )
			opc << '*';

		if ( output.bits1.oes3 || output.bits1.oes2 )
			trictl = true;		// Assume TRICTL

		auto outs = [&]() {
			if ( trictl ) {
				if ( output.bits1.oes3 )
					oper << "OES3 ";
				if ( output.bits1.oes2 )
					oper << "OES2 ";
				if ( output.bits1.oes1 )
					oper << "OES1 ";
				if ( output.bits1.oes0 )
					oper << "OES0 ";
				if ( output.bits1.ctl3 )
					oper << "CTL3 ";
				if ( output.bits1.ctl2 )
					oper << "CTL2 ";
				if ( output.bits1.ctl1 )
					oper << "CTL1 ";
				if ( output.bits1.ctl0 )
					oper << "CTL0 ";
			} else	{
				if ( output.bits0.ctl5 )
					oper << "CTL5 ";
				if ( output.bits0.ctl4 )
					oper << "CTL4 ";
				if ( output.bits0.ctl3 )
					oper << "CTL3 ";
				if ( output.bits0.ctl2 )
					oper << "CTL2 ";
				if ( output.bits0.ctl1 )
					oper << "CTL1 ";
				if ( output.bits0.ctl0 )
					oper << "CTL0 ";
			}
		};

		if ( opcode.bits.dp == 0 ) {
			// NDP
			
			if ( branch.byte == 0 )
				oper << "256 ";
			else	oper << unsigned(branch.byte) << ' ';
			outs();

		} else	{
			auto aorb = [&](unsigned term) {
				switch ( term ) {
				case 0:
					oper << "RDY0 ";
					break;
				case 1:
					oper << "RDY1 ";
					break;
				case 2:
					oper << "RDY2 ";
					break;
				case 3:
					oper << "RDY3 ";
					break;
				case 4:
					oper << "RDY4 ";
					break;
				case 5:
					oper << "RDY5|TC ";
					break;
				case 6:
					oper << "PF|EF|FF ";
					break;
				case 7:
					oper << "INTRDY ";
					break;
				}
			};
			aorb(logfunc.bits.terma);
			switch ( logfunc.bits.lfunc ) {
			case 0b00:
				oper << "AND ";
				break;
			case 0b01:
				oper << "OR ";
				break;
			case 0b10:
				oper << "XOR ";
				break;
			case 0b11:
				oper << "/AND ";
				break;
			}
			aorb(logfunc.bits.termb);
			outs();

			oper << "$" << unsigned(branch.bits.branch0)
				<< " $" << unsigned(branch.bits.branch1);
		}

		std::cout.width(2);
		std::cout.fill('0');
		std::cout << std::uppercase << std::hex << unsigned(branch.byte);
		std::cout.width(2);
		std::cout.fill('0');
		std::cout << std::uppercase << std::hex << unsigned(opcode.byte);
		std::cout.width(2);
		std::cout.fill('0');
		std::cout << std::uppercase << std::hex << unsigned(logfunc.byte);
		std::cout.width(2);
		std::cout.fill('0');
		std::cout << std::uppercase << std::hex << unsigned(output.byte)
			<< '\t' << opc.str()
			<< '\t' << oper.str() << '\n';
	}
}

static void
decompile(const char *path) {
	std::ifstream gpif_c;
	char buf[2048];
	bool foundf = false;

	gpif_c.open(path,std::ifstream::in);
	if ( gpif_c.fail() ) {
		fprintf(stderr,"%s: Opening %s for read\n",
			strerror(errno),path);
		exit(1);
	}

	while ( gpif_c.good() ) {
		if ( !gpif_c.getline(buf,sizeof buf).good() )
			break;
		if ( !strncmp(buf,"const char xdata WaveData[128] =",32) ) {
			foundf = true;
			break;
		}
	}

	if ( !foundf ) {
		std::cerr << "Did not find line: 'const char xdata WaveData[128] =' in " << path << '\n';
		gpif_c.close();
		exit(1);
	}

	char ch;

	foundf = false;
	while ( gpif_c.good() ) {
		ch = gpif_c.get();
		if ( !gpif_c.good() )
			break;
		if ( ch == '{' ) {
			foundf = true;
			break;
		}
	}

	if ( !foundf ) {
		std::cerr << "Missing opening brace: " << path << '\n';
		gpif_c.close();
		exit(1);
	}

	std::vector<uint8_t> raw;
	std::stringstream sbuf;

	while ( gpif_c.good() ) {
		ch = gpif_c.get();
		if ( ch == '/' ) {
			if ((ch = gpif_c.get()) == '/' ) {
				while ( gpif_c.good() && gpif_c.get() != '\n' )
					;
				continue;
			} else if ( ch == '*' ) {
				do	{
					while ( gpif_c.good() && (ch = gpif_c.get()) != '*' )
						;
					if ( !gpif_c.good() )
						break;
					ch = gpif_c.get();
				} while ( gpif_c.good() && ch != '/' );
				if ( !gpif_c.good() )
					break;
				continue;
			}
		}
		if ( ch == '}' )
			break;
		if ( strchr("\n\r\t\b ",ch) != nullptr )
			continue;

		if ( ch != ',' ) {
			sbuf << ch;
		} else	{
			std::string data(sbuf.str());
			sbuf.clear();
			sbuf.str("");

			char *ep;
			unsigned long udata = strtoul(data.c_str(),&ep,0);

			if ( (ep && *ep != 0) || udata > 0xFF ) {
				std::cerr << "Invalid data: '" << data << "'\n";
				gpif_c.close();
				exit(1);
			}
			raw.push_back(uint8_t(udata));
		}
	}

	std::cout << raw.size() << " bytes.\n";
	gpif_c.close();

	switch ( raw.size() ) {
	case 32:
	case 64:
	case 96:
	case 128:
		break;
	default:
		std::cerr << "Unusual data size! Extraction failed.\n";
		exit(1);
	}

	uint8_t unpacked[32];

	memset(unpacked,0,sizeof unpacked);
	for ( unsigned ux=0; ux < raw.size(); ux += 32 ) {
		unsigned lx = ux;		// Length index
		unsigned opx = lx + 8;		// Opcode index
		unsigned otx = opx + 8;		// Output index
		unsigned lfx = otx + 8;		// Logical function index

		for ( unsigned bx=0; bx < 32; ) {
			unpacked[bx++] = raw[lx++];
			unpacked[bx++] = raw[opx++];
			unpacked[bx++] = raw[lfx++];
			unpacked[bx++] = raw[otx++];
		}
		decompile(ux/32,unpacked);
	}

	exit(0);
}

static void
uncompile(int argc,char **argv) {

	for ( int ax=1; ax < argc; ++ax )
		decompile(argv[ax]);

	exit(0);
}

// End ezusbcc.cpp
