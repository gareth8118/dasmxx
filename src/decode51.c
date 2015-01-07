/*****************************************************************************
 *
 * Copyright (C) 2014-2015, Neil Johnson
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms,
 * with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of Neil Johnson nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dasmxx.h"

/*****************************************************************************
 * Globally-visible decoder properties
 *****************************************************************************/

/* Decoder short name */
const char * dasm_name            = "dasm8051";

/* Decoder description */
const char * dasm_description     = "Intel 8051";

/* Decoder maximum instruction length in bytes */
const int    dasm_max_insn_length = 4;

/* Decoder maximum opcode field width */
const int    dasm_max_opcode_width = 9;

/*****************************************************************************
 * Private data types, macros, constants.
 *****************************************************************************/

/* Common output formats */
#define FORMAT_NUM_8BIT      "$%02X"
#define FORMAT_NUM_16BIT   "$%04X"
#define FORMAT_REG      "R%d"

/* Create a single-bit mask */
#define BIT(n)         ( 1 << (n) )

/* Construct a 16-bit word out of low and high bytes */
#define MK_WORD(l,h)      ( ((l) & 0xFF) | (((h) & 0xFF) << 8) )

/* Indicate whether decode was successful or not. */
#define INSN_FOUND      ( 1 )
#define INSN_NOT_FOUND      ( 0 )

/* Neaten up emitting a comma "," within an operand. */
#define COMMA         operand( ", " )

/**
   The optab_t type describes each entry in the op tables.
**/
typedef struct optab_s {
   UBYTE opc;
   const char * opcode;
   void (*operands)( FILE *, ADDR *, UBYTE, XREF_TYPE); /* operand function */
   XREF_TYPE xtype;
   enum {
      OPTAB_INSN,
      OPTAB_RANGE,
      OPTAB_MASK,
      OPTAB_TABLE
   } type;
   union {
      struct {
         UBYTE min, max;
      } range;
      struct {
         UBYTE mask, val;
      } mask;
      struct optab_s * table;
   } u;
} optab_t;

/**
   Macros to construct entries in op tables.
**/

/**
   The given instruction byte jumps to another decode table.
**/
#define TABLE(M_tablename, M_opc) \
    { .type    = OPTAB_TABLE,  \
      .opc     = M_opc,        \
      .opcode  = "TABLE",      \
      .u.table = M_tablename   \
    },

/**
   A single insruction matches against one op byte.
**/   
#define INSN(M_opcode, M_ops, M_opc, M_xt) \
    { .type     = OPTAB_INSN,          \
      .opc      = M_opc,             \
      .opcode   = M_opcode,          \
      .operands = operand_ ## M_ops,   \
      .xtype    = M_xt               \
    },

/**
   A RANGE matches the first byte anywhere between M_min and M_max inclusive.
**/   
#define RANGE(M_opcode, M_ops, M_min, M_max, M_xt)    \
    { .type     = OPTAB_RANGE,       \
      .opcode   = M_opcode,            \
      .operands = operand_ ## M_ops, \
      .xtype    = M_xt,               \
      .u.range.min = M_min,            \
      .u.range.max = M_max            \
    },

/**
   A MASK matches a set of instruction bytes described by a bit mask and a
   value to match against applied to the first search byte.
**/   
#define MASK(M_opcode, M_ops, M_mask, M_val, M_xt)     \
    { .type     = OPTAB_MASK,         \
      .opcode   = M_opcode,            \
      .operands = operand_ ## M_ops, \
      .xtype    = M_xt,               \
      .u.mask.mask = M_mask,         \
      .u.mask.val  = M_val            \
    },

/**
   Mark end of op table.
**/
#define END      { .opcode = NULL }

/*****************************************************************************
 * Private data.
 *****************************************************************************/

/* Start address of each instruction as it is decoded. */
static ADDR g_insn_addr = 0;

/* Global output buffer into which the decoded output is written. */
static char * g_output_buffer = NULL;

/*****************************************************************************
 *        Private Functions
 *****************************************************************************/

/***********************************************************
 *
 * FUNCTION
 *      opcode
 *
 * DESCRIPTION
 *      Writes the given opcode string into the output buffer.
 *
 * RETURNS
 *      none
 *
 ************************************************************/
 
static void opcode( const char *opcode )
{
   int n = sprintf( g_output_buffer, "%-*s", dasm_max_opcode_width, opcode );
   g_output_buffer += n;
}

/***********************************************************
 *
 * FUNCTION
 *      operand
 *
 * DESCRIPTION
 *      Writes the given operand string and any arguments
 *      into the output buffer.  The string is processed with
 *      the usual printf() conversions.
 *
 * RETURNS
 *      none
 *
 ************************************************************/
 
static void operand( const char *operand, ... )
{
   va_list ap;
   int n;
   
   va_start( ap, operand );
   n = vsprintf( g_output_buffer, operand, ap );
   va_end( ap );
   
   g_output_buffer += n;
}

/******************************************************************************/
/**                            Operand Functions                             **/
/******************************************************************************/

#define OPERAND_FUNC(M_name) \
   static void operand_ ## M_name (FILE *f, ADDR * addr, UBYTE opc, XREF_TYPE xtype )

/******************************************************************************/
/**                            Empty Operands                                **/
/******************************************************************************/

OPERAND_FUNC(none)
{
   /* empty */
}

/******************************************************************************/
/**                            Single Operands                               **/
/******************************************************************************/

/***********************************************************
 * Hard-coded registers.
 ************************************************************/

OPERAND_FUNC(A)
{
   operand( "A" );
}

OPERAND_FUNC(B)
{
   operand( "B" );
}

OPERAND_FUNC(C)
{
   operand( "C" );
}

OPERAND_FUNC(AB)
{
   operand( "AB" );
}

OPERAND_FUNC(PC)
{
   operand( "PC" );
}

OPERAND_FUNC(dptr)
{
   operand( "DPTR" );
}

/***********************************************************
 * General register.
 *   reg.num comes from bottom 3 bits of OPC.
 ************************************************************/

OPERAND_FUNC(reg)
{
   UBYTE reg = opc & 0x07;
   
   operand( FORMAT_REG, reg );
}

/***********************************************************
 * Indirect register.
 *   reg.num is either 0 or 1 depending on the OPC.
 ************************************************************/

OPERAND_FUNC(indreg)
{
   UBYTE reg = opc & 0x01;
   
   operand( "@R%d", reg );
}

/***********************************************************
 * 8-bit Immediate operand
 *   value comes from next byte.
 ************************************************************/

OPERAND_FUNC(imm8)
{
   UBYTE imm8 = next( f, addr );
   
   operand( "#" FORMAT_NUM_8BIT, imm8 );
}

/***********************************************************
 * 16-bit Immediate operand
 *   Data comes from next two bytes.
 ************************************************************/
 
OPERAND_FUNC(imm16)
{
   UBYTE msb   = next( f, addr );
   UBYTE lsb   = next( f, addr );
   UWORD imm16 = MK_WORD( lsb, msb );

   operand( "#" FORMAT_NUM_16BIT, imm16 );
}

/***********************************************************
 * 8-bit Bit Address
 *   value comes from next byte.
 ************************************************************/

OPERAND_FUNC(addrbit)
{
   UBYTE bit = next( f, addr );
   
   operand( FORMAT_NUM_8BIT, bit );
}

/***********************************************************
 * 8-bit Immediate operand
 *   value comes from next byte.
 ************************************************************/

OPERAND_FUNC(iram)
{
   UBYTE iaddr = next( f, addr );
   const char * s;
   
   if ( ( s = xref_findaddrlabel( iaddr ) ) )
      operand( "%s", s );
   else
      operand( "%%" FORMAT_NUM_8BIT, iaddr );
}

/***********************************************************
 * 11-bit address
 *   Address comes from next two bytes.
 ************************************************************/
 
OPERAND_FUNC(addr11)
{
   UBYTE msb_addr  = ( opc >> 5) & 0x07;
   UBYTE lsb_addr  = next( f, addr );
   UWORD addr11    = MK_WORD( lsb_addr, msb_addr );
   UWORD addr16    = (UWORD)*addr;
   addr16 = ( addr16 & 0xF800 ) | addr11;

   operand( "@%s", xref_genwordaddr( NULL, "$", addr16 ) );
   xref_addxref( xtype, g_insn_addr, addr16 );
}

/***********************************************************
 * 16-bit address
 *   Address comes from next two bytes.
 ************************************************************/
 
OPERAND_FUNC(addr16)
{
   UBYTE msb_addr  = next( f, addr );
   UBYTE lsb_addr  = next( f, addr );
   UWORD addr16    = MK_WORD( lsb_addr, msb_addr );

   operand( "@%s", xref_genwordaddr( NULL, "$", addr16 ) );
   xref_addxref( xtype, g_insn_addr, addr16 );
}

/***********************************************************
 * 8-bit Relative Address.
 ************************************************************/

OPERAND_FUNC(rel8)
{
   BYTE ofst = (BYTE)next( f, addr );
   ADDR dest = *addr + ofst;
   
   operand( xref_genwordaddr( NULL, "$", dest ) );
   xref_addxref( xtype, g_insn_addr, dest );
}

/******************************************************************************/
/**                            Double Operands                               **/
/******************************************************************************/

OPERAND_FUNC(A_imm8)
{
    operand_A( f, addr, opc, xtype  );
    COMMA;
    operand_imm8( f, addr, opc, xtype );
}

OPERAND_FUNC(A_iram)
{
    operand_A( f, addr, opc, xtype  );
    COMMA;
    operand_iram( f, addr, opc, xtype );
}

OPERAND_FUNC(iram_A)
{
    operand_iram( f, addr, opc, xtype );
    COMMA;
    operand_A( f, addr, opc, xtype  );
}

OPERAND_FUNC(iram_imm8)
{
    operand_iram( f, addr, opc, xtype );
    COMMA;
    operand_imm8( f, addr, opc, xtype  );
}

OPERAND_FUNC(A_indreg)
{
    operand_A( f, addr, opc, xtype  );
    COMMA;
    operand_indreg( f, addr, opc, xtype );
}

OPERAND_FUNC(indreg_A)
{
    operand_indreg( f, addr, opc, xtype  );
    COMMA;
    operand_A( f, addr, opc, xtype );
}

OPERAND_FUNC(indreg_imm8)
{
    operand_indreg( f, addr, opc, xtype  );
    COMMA;
    operand_imm8( f, addr, opc, xtype );
}

OPERAND_FUNC(indreg_iram)
{
    operand_indreg( f, addr, opc, xtype  );
    COMMA;
    operand_iram( f, addr, opc, xtype );
}

OPERAND_FUNC(iram_indreg)
{
    operand_iram( f, addr, opc, xtype  );
    COMMA;
    operand_indreg( f, addr, opc, xtype );
}

OPERAND_FUNC(A_reg)
{
    operand_A( f, addr, opc, xtype  );
    COMMA;
    operand_reg( f, addr, opc, xtype );
}

OPERAND_FUNC(reg_A)
{
    operand_reg( f, addr, opc, xtype  );
    COMMA;
    operand_A( f, addr, opc, xtype );
}

OPERAND_FUNC(iram_rel8)
{
    operand_iram( f, addr, opc, xtype );
    COMMA;
    operand_rel8( f, addr, opc, xtype  );
}

OPERAND_FUNC(reg_rel8)
{
    operand_reg( f, addr, opc, xtype );
    COMMA;
    operand_rel8( f, addr, opc, xtype  );
}

OPERAND_FUNC(reg_imm8)
{
    operand_reg( f, addr, opc, xtype );
    COMMA;
    operand_imm8( f, addr, opc, xtype  );
}

OPERAND_FUNC(reg_iram)
{
    operand_reg( f, addr, opc, xtype  );
    COMMA;
    operand_iram( f, addr, opc, xtype );
}

OPERAND_FUNC(iram_reg)
{
    operand_iram( f, addr, opc, xtype  );
    COMMA;
    operand_reg( f, addr, opc, xtype );
}

OPERAND_FUNC(iram_iram)
{
    operand_iram( f, addr, opc, xtype  );
    COMMA;
    operand_iram( f, addr, opc, xtype );
}

OPERAND_FUNC(C_addrbit)
{
    operand_C( f, addr, opc, xtype );
    COMMA;
    operand_addrbit( f, addr, opc, xtype );
}

OPERAND_FUNC(addrbit_C)
{
    operand_addrbit( f, addr, opc, xtype );
    COMMA;
    operand_C( f, addr, opc, xtype );
}

OPERAND_FUNC(C_n_addrbit)
{
    operand_C( f, addr, opc, xtype );
    COMMA;
    operand( "/" );
    operand_addrbit( f, addr, opc, xtype );
}

OPERAND_FUNC(A_plus_dptr)
{
    operand( "@" );
    operand_A( f, addr, opc, xtype );
    operand( "+" );
    operand_dptr( f, addr, opc, xtype );
}

OPERAND_FUNC(A_dptr)
{
    operand_A( f, addr, opc, xtype );
    COMMA;
    operand( "@" );
    operand_dptr( f, addr, opc, xtype );
}

OPERAND_FUNC(dptr_A)
{
    operand( "@" );
    operand_dptr( f, addr, opc, xtype );
    COMMA;
    operand_A( f, addr, opc, xtype );
}

OPERAND_FUNC(dptr_imm16)
{
    operand_dptr( f, addr, opc, xtype );
    COMMA;
    operand_imm16( f, addr, opc, xtype );
}

/******************************************************************************/
/**                            Triple Operands                               **/
/******************************************************************************/

OPERAND_FUNC(A_imm8_rel8)
{
    operand_A( f, addr, opc, xtype );
    COMMA;
    operand_imm8( f, addr, opc, xtype );
    COMMA;
    operand_rel8( f, addr, opc, xtype );
}

OPERAND_FUNC(A_iram_rel8)
{
    operand_A( f, addr, opc, xtype );
    COMMA;
    operand_iram( f, addr, opc, xtype );
    COMMA;
    operand_rel8( f, addr, opc, xtype );
}

OPERAND_FUNC(indreg_imm8_rel8)
{
    operand_indreg( f, addr, opc, xtype );
    COMMA;
    operand_imm8( f, addr, opc, xtype );
    COMMA;
    operand_rel8( f, addr, opc, xtype );
}

OPERAND_FUNC(reg_imm8_rel8)
{
    operand_reg( f, addr, opc, xtype );
    COMMA;
    operand_imm8( f, addr, opc, xtype );
    COMMA;
    operand_rel8( f, addr, opc, xtype );
}

OPERAND_FUNC(A_A_dptr)
{
    operand_A( f, addr, opc, xtype );
    COMMA;
    operand( "@" );
    operand_A( f, addr, opc, xtype );
    operand( "+" );
    operand_dptr( f, addr, opc, xtype );
}

OPERAND_FUNC(A_A_PC)
{
    operand_A( f, addr, opc, xtype );
    COMMA;
    operand( "@" );
    operand_A( f, addr, opc, xtype );
    operand( "+" );
    operand_PC( f, addr, opc, xtype );
}

/******************************************************************************/
/** Instruction Decoding Tables                                              **/
/** Note: tables are here as they refer to operand functions defined above.  **/
/******************************************************************************/

#define INCDEC(M_name, M_msb) \
        INSN( M_name, A,                ( M_msb | 0x04 ), X_NONE ) \
        INSN( M_name, iram,             ( M_msb | 0x05 ), X_NONE ) \
        RANGE( M_name, indreg,          ( M_msb | 0x06 ), ( M_msb | 0x07 ), X_NONE ) \
        MASK( M_name, reg,              ( 0xF8 ), ( M_msb | 0x08 ), X_NONE )

#define ARITH(M_name, M_msb) \
        INSN( M_name, A_imm8,             ( M_msb | 0x04 ), X_NONE ) \
        INSN( M_name, A_iram,             ( M_msb | 0x05 ), X_NONE ) \
        RANGE( M_name, A_indreg,          ( M_msb | 0x06 ), ( M_msb | 0x07 ), X_NONE ) \
        MASK( M_name, A_reg,              ( 0xF8 ), ( M_msb | 0x08 ), X_NONE )

#define BITWISE(M_name, M_msb) \
        INSN( M_name, iram_A,             ( M_msb | 0x02 ), X_NONE ) \
        INSN( M_name, iram_imm8,          ( M_msb | 0x03 ), X_NONE ) \
        INSN( M_name, A_imm8,             ( M_msb | 0x04 ), X_NONE ) \
        INSN( M_name, A_iram,             ( M_msb | 0x05 ), X_NONE ) \
        RANGE( M_name, A_indreg,          ( M_msb | 0x06 ), ( M_msb | 0x07 ), X_NONE ) \
        MASK( M_name, A_reg,              ( 0xF8 ), ( M_msb | 0x08 ), X_NONE )

static optab_t base_optab[] = {

/*----------------------------------------------------------------------------
  Arithmetic
  ----------------------------------------------------------------------------*/

    INCDEC( "INC", 0x00 )
    INCDEC( "DEC", 0x10 )
    INSN(   "INC", dptr, 0xA3, X_NONE )

    ARITH( "ADD", 0x20 )
    ARITH( "ADDC", 0x30 )
    ARITH( "SUBB", 0x90 )

    INSN( "DA", A,  0xD4, X_NONE )

    INSN( "DIV", AB, 0x84, X_NONE )
    INSN( "MUL", AB, 0xA4, X_NONE )

/*----------------------------------------------------------------------------
  Bitwise
  ----------------------------------------------------------------------------*/

    BITWISE( "ORL", 0x40 )
    INSN   ( "ORL", C_addrbit,   0x72, X_NONE )
    INSN   ( "ORL", C_n_addrbit, 0xA0, X_NONE )

    BITWISE( "ANL", 0x50 )
    INSN   ( "ANL", C_addrbit,   0x82, X_NONE )
    INSN   ( "ANL", C_n_addrbit, 0xB0, X_NONE )

    BITWISE( "XRL", 0x60 )

    INSN( "CPL", A,        0xF4, X_NONE )
    INSN( "CPL", C,        0xB3, X_NONE )
    INSN( "CPL", addrbit,  0xB2, X_NONE )
 
    INSN( "CLR", A,        0xE4, X_NONE )
    INSN( "CLR", C,        0xC3, X_NONE )
    INSN( "CLR", addrbit,  0xC2, X_NONE )

    INSN( "SETB", C,       0xD3, X_NONE )
    INSN( "SETB", addrbit, 0xD2, X_NONE )

/*----------------------------------------------------------------------------
  Rotates
  ----------------------------------------------------------------------------*/

    INSN( "RR",  A, 0x03, X_NONE )
    INSN( "RRC", A, 0x13, X_NONE )
    INSN( "RL",  A, 0x23, X_NONE )
    INSN( "RLC", A, 0x33, X_NONE )

/*----------------------------------------------------------------------------
  Exchange and Swap
  ----------------------------------------------------------------------------*/

    INSN ( "XCH",  A_iram,       0xC5, X_NONE )
    RANGE( "XCH",  A_indreg,     0xC6, 0xC7, X_NONE )
    MASK ( "XCH",  A_reg,        0xF8, 0xC8, X_NONE )

    RANGE( "XCHD", A_indreg,    0xD6, 0xD7, X_NONE )

    INSN ( "SWAP", A, 0xC4, X_NONE )

/*----------------------------------------------------------------------------
  Stack Operations
  ----------------------------------------------------------------------------*/

    INSN( "PUSH", iram, 0xC0, X_NONE )
    INSN( "POP",  iram, 0xD0, X_NONE )

/*----------------------------------------------------------------------------
  Jump, Call and Return
  ----------------------------------------------------------------------------*/

    INSN( "JMP",   A_plus_dptr, 0x73, X_NONE )

    INSN( "SJMP",  rel8, 0x80, X_JMP )

    MASK( "AJMP",  addr11, 0x1F, 0x01, X_JMP )
    INSN( "LJMP",  addr16, 0x02, X_JMP )

    MASK( "ACALL", addr11, 0x1F, 0x11, X_CALL )
    INSN( "LCALL", addr16, 0x12, X_CALL )

    INSN( "RET",   none,   0x22, X_NONE )
    INSN( "RETI",  none,   0x32, X_NONE )

/*----------------------------------------------------------------------------
  Conditional Jumps
  ----------------------------------------------------------------------------*/

    INSN( "CJNE", A_imm8_rel8,      0xB4, X_JMP )
    INSN( "CJNE", A_iram_rel8,      0xB5, X_JMP )
    INSN( "CJNE", indreg_imm8_rel8, 0xB6, X_JMP )
    MASK( "CJNE", reg_imm8_rel8,    0xF8, 0xB8, X_JMP )

    INSN( "DJNZ", iram_rel8, 0xD5,       X_JMP )
    MASK( "DJNZ", reg_rel8,  0xF8, 0xD8, X_JMP )

    INSN( "JBC",  rel8, 0x10, X_JMP )
    INSN( "JB",   rel8, 0x20, X_JMP )
    INSN( "JNB",  rel8, 0x30, X_JMP )
    INSN( "JC",   rel8, 0x40, X_JMP )
    INSN( "JNC",  rel8, 0x50, X_JMP )
    INSN( "JZ",   rel8, 0x60, X_JMP )
    INSN( "JNZ",  rel8, 0x70, X_JMP )

/*----------------------------------------------------------------------------
  Moves
  ----------------------------------------------------------------------------*/

    INSN( "MOVC",  A_A_dptr,    0x93, X_NONE )
    INSN( "MOVC",  A_A_PC,      0x83, X_NONE )

    INSN(  "MOVX", dptr_A,      0xF0, X_NONE )
    RANGE( "MOVX", indreg_A,    0xF2, 0xF3, X_NONE )

    INSN(  "MOVX", A_dptr,      0xE0, X_NONE )
    RANGE( "MOVX", A_indreg,    0xE2, 0xE3, X_NONE )

    RANGE( "MOV",  indreg_imm8, 0x76, 0x77, X_NONE )
    RANGE( "MOV",  indreg_A,    0xF6, 0xF7, X_NONE )
    RANGE( "MOV",  indreg_iram, 0xA6, 0xA7, X_NONE )
    INSN ( "MOV",  A_imm8,      0x74, X_NONE )
    RANGE( "MOV",  A_indreg,    0xE6, 0xE7, X_NONE )
    MASK ( "MOV",  A_reg,       0xF8, 0xE8, X_NONE )
    INSN ( "MOV",  A_iram,      0xE5, X_NONE )
    INSN ( "MOV",  C_addrbit,   0xA2, X_NONE )
    INSN ( "MOV",  dptr_imm16,  0x90, X_NONE )
    MASK ( "MOV",  reg_imm8,    0xF8, 0x78, X_NONE )
    MASK ( "MOV",  reg_A,       0xF8, 0xF8, X_NONE )
    MASK ( "MOV",  reg_iram,    0xF8, 0xA8, X_NONE )
    INSN ( "MOV",  addrbit_C,   0x92, X_NONE )
    INSN ( "MOV",  iram_imm8,   0x75, X_NONE )
    RANGE( "MOV",  iram_indreg, 0x86, 0x87, X_NONE )
    MASK ( "MOV",  iram_reg,    0xF8, 0x88, X_NONE )
    INSN ( "MOV",  iram_A,      0xF5, X_NONE )
    INSN ( "MOV",  iram_iram,   0x85, X_NONE )

/*----------------------------------------------------------------------------
  Miscellaneous
  ----------------------------------------------------------------------------*/

    INSN( "NOP", none, 0x00, X_NONE )

/*----------------------------------------------------------------------------*/

   END
};

/***********************************************************
 *
 * FUNCTION
 *      walk_table
 *
 * DESCRIPTION
 *      Disassembles the next instruction in the input stream.
 *      f - file stream to read (pass to calls to next() )
 *      outbuf - pointer to output buffer
 *      addr - address of first input byte for this insn
 *
 * RETURNS
 *      INSN_FOUND if a valid instruction found.
 *      INSN_NOT_FOUND otherwise.
 *
 ************************************************************/

static int walk_table( FILE * f, ADDR * addr, optab_t * optab, UBYTE opc )
{
   UBYTE peek_byte;
   int have_peeked = 0;
   
   if ( optab == NULL )
      return INSN_NOT_FOUND;
      
   while ( optab->opcode != NULL )
   {
      /* printf("type:%d  ", optab->type); */
      if ( optab->type == OPTAB_TABLE && optab->opc == opc )
      {
         opc = next( f, addr );
         return walk_table( f, addr, optab->u.table, opc );
      }
      else if ( ( optab->type == OPTAB_INSN && opc == optab->opc )
               ||
                ( optab->type == OPTAB_RANGE 
                  && opc >= optab->u.range.min 
                  && opc <= optab->u.range.max )
               ||
                ( optab->type == OPTAB_MASK 
                  && ( ( opc & optab->u.mask.mask ) == optab->u.mask.val ) ) )
      {
         opcode( optab->opcode );
         optab->operands( f, addr, opc, optab->xtype );
         return INSN_FOUND;
      }
      
      optab++;   
   }

   return INSN_NOT_FOUND;
}

/*****************************************************************************
 *        Public Functions
 *****************************************************************************/
 
/***********************************************************
 *
 * FUNCTION
 *      dasm_insn
 *
 * DESCRIPTION
 *      Disassembles the next instruction in the input stream.
 *      f - file stream to read (pass to calls to next() )
 *      outbuf - pointer to output buffer
 *      addr - address of first input byte for this insn
 *
 * RETURNS
 *      address of next input byte
 *
 ************************************************************/
 
ADDR dasm_insn( FILE *f, char *outbuf, ADDR addr )
{
   int opc;
   int found = 0;

   /* Store start address in a global for use in xref calls */   
   g_insn_addr = addr;
   
   /* Setup g_output_buffer to point to caller's output buffer */
   g_output_buffer = outbuf;

   /* Get first opcode byte */
   opc = next( f, &addr );

   /* Now walk table(s) looking for an instruction match */
   found = walk_table( f, &addr, base_optab, opc );
   
   /* If we didn't find a match, indicate this to the output */
   if ( found != INSN_FOUND )
      opcode( "???" );
   
   return addr;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/