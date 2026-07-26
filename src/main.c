#include <stdio.h>
#include <stdlib.h>

// Memory
#define MEMORY_MAX (1 << 16)  // 1 * (2 ^ 16) = 65,536
uint16_t memory[MEMORY_MAX];  /* 65536 locations */

// Registers
// There are 8 general purpose registers (R0-R7), 
// and 1 program counter (R_PC) that shows what the next instruction is, 
// and 1 condition flags register (R_COND) that tells us info about the previous instruction. 
enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* program counter */
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT];

// Instruction Set
// There are only 16 opcodes in LC-3 architecture
// Each instruction is 16 bits long, with the left 4 bits storing the opcode. The rest of the bits are used to store the parameters.
enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};