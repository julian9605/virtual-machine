#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

// @{Memory Mapped Registers}
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

// @{TRAP Codes}
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

// @{Memory Storage}
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

// @{Register Storage}
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

// Condition Flags
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

// @{Input Buffering}
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

// @{Handle Interrupt}
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

// @{Sign Extend}
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

// @{Swap}
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

// @{Update Flags}
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

// @{Read Image File}
void read_image_file(FILE* file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

// @{Read Image}
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

// @{Memory Access}
void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

// @{Main Loop}
int main(int argc, const char* argv[])
{
    // @{Load Arguments}
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                {
                    /* destination register (DR) */
                    uint16_t r0 = (instr >> 9) & 0x7;
                    /* first operand (SR1) */
                    uint16_t r1 = (instr >> 6) & 0x7;
                    /* whether we are in immediate mode */
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag)
                    {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] + imm5;
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] + reg[r2];
                    }

                    update_flags(r0);
                }
                break;
            case OP_AND:
                {
                    // destination register
                    uint16_t r0 = (instr >> 9) & 0x7;
                    // first register (SR1)
                    uint16_t r1 = (instr >> 6) & 0x7;
                    // whether we are in immediate mode
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag) {
                        // sign extend imm5
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        // and operation
                        reg[r0] = reg[r1] & imm5;
                    }
                    else {
                        // SR2
                        uint16_t r2 = instr & 0x7;
                        // and operation
                        reg[r0] = reg[r1] & reg[r2];
                    }

                    update_flags(r0);
                }
                break;
            case OP_NOT:
                {
                    // destination register
                    uint16_t r0 = (instr >> 9) & 0x7;
                    // source register
                    uint16_t r1 = (instr >> 6) & 0x7;

                    reg[r0] = ~reg[r1];
                    update_flags(r0);
                }
                break;
            case OP_BR:
                {
                    // uint16_t n = (instr >> 11) & 0x1;
                    // uint16_t z = (instr >> 10) & 0x1;
                    // uint16_t p = (instr >> 9) & 0x1;
                    uint16_t cond_flag = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                    if (cond_flag & reg[R_COND]) {
                        reg[R_PC] += pc_offset;
                    }
                }
                break;
            case OP_JMP:
                {
                    // base register
                    uint16_t r0 = (instr >> 6) & 0x7;

                    // RET
                    if (r0 == 0x7) {
                        reg[R_PC] = 0x7;
                    }
                    // JMP
                    else {
                        reg[R_PC] = reg[r0]; // the address is in the contents of r0
                    }
                }
                break;
            case OP_JSR:
                {
                    // save PC to R7
                    reg[R_R7] = reg[R_PC];

                    uint16_t jsr_flag = (instr >> 11) & 0x1;

                    // JSR
                    if (jsr_flag) {
                        uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
                        reg[R_PC] += pc_offset;
                    }
                    // JSRR
                    else {
                        uint16_t r0 = (instr >> 6) & 0x7;
                        reg[R_PC] = reg[r0];
                    }

                    // update_flags(R_R7);
                }
                break;
            case OP_LD:
                {
                    // destination register
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                    // The contents of memory at this address are loaded into DR.
                    reg[r0] = mem_read(reg[R_PC] + pc_offset);
                    update_flags(r0);
                }
                break;
            case OP_LDI:
                {
                    /* destination register (DR) */
                    uint16_t r0 = (instr >> 9) & 0x7;
                    /* PCoffset 9*/
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    /* add pc_offset to the current PC, look at that memory location to get the final address */
                    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                    update_flags(r0);
                }
                break;
            case OP_LDR:
                {
                    // destination register
                    uint16_t r0 = (instr >> 9) & 0x7;
                    // base register
                    uint16_t r1 = (instr >> 6) & 0x7;
                    // offset6
                    uint16_t offset = sign_extend(instr & 0x3F, 6);

                    reg[r0] = mem_read(reg[r1] + offset);
                    update_flags(r0);
                }
                break;
            case OP_LEA:
                {
                    // destination register
                    uint16_t r0 = (instr >> 9) & 0x7;
                    // pcoffset9
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                    reg[r0] = reg[R_PC] + pc_offset;
                    update_flags(r0);
                }
                break;
            case OP_ST:
                {
                    // source register
                    uint16_t r0 = (instr >> 9) & 0x7;
                    // pcoffset9
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                    // store
                    mem_write(reg[R_PC] + pc_offset, reg[r0]);
                }
                break;
            case OP_STI:
                {
                    // source register
                    uint16_t src = (instr >> 9) & 0x7;
                    // pcoffset9
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);

                    // store indirect
                    mem_write(mem_read(reg[R_PC] + pc_offset), reg[src]);
                }
                break;
            case OP_STR:
                {
                    // source register
                    uint16_t src = (instr >> 9) & 0x7;
                    // base register
                    uint16_t base = (instr >> 6) & 0x7;
                    // offset6
                    uint16_t offset = sign_extend(instr & 0x3F, 6);

                    // store base + offset
                    mem_write(reg[base] + offset, reg[src]);
                }
                break;
            case OP_TRAP:
                {
                    // trapvect8
                    uint16_t trapvect = instr & 0xFF;

                    // store PC in R7
                    reg[R_R7] = reg[R_PC];
                    // system call
                    switch (trapvect)
                    {
                        case TRAP_GETC:
                            {
                                reg[R_R0] = (uint16_t)getchar();
                                update_flags(R_R0);
                            }
                            break;
                        case TRAP_OUT:
                            {
                                putc((char)reg[R_R0], stdout);
                                fflush(stdout);
                            }
                            break;
                        case TRAP_PUTS:
                            {
                                /* one char per word */
                                uint16_t* c = memory + reg[R_R0];
                                while (*c)
                                {
                                    putc((char)*c, stdout);
                                    ++c;
                                }
                                fflush(stdout);
                            }
                            break;
                        case TRAP_IN:
                            {
                                printf("Enter a character: ");
                                char c = getchar();
                                putc((char)c, stdout);
                                fflush(stdout);
                                reg[R_R0] = (uint16_t)c;
                                update_flags(R_R0);
                            }
                            break;
                        case TRAP_PUTSP:
                            {
                                // i don't even know what's happening anymore
                                uint16_t* c = memory + reg[R_R0];
                                while (*c) {
                                    // assume big endian
                                    char char1 = (*c) & 0xFF;
                                    putc((char)char1, stdout);
                                    char char2 = (*c) >> 8;
                                    if (char2) {
                                        putc((char)char2, stdout);
                                    }
                                    ++c;
                                }
                                fflush(stdout);
                            }
                            break;
                        case TRAP_HALT:
                            {
                                // abort();
                                // printf("Process execution halted.\n");
                                puts("HALT");
                                fflush(stdout);
                                running = 0;
                            }    
                            break;
                    }

                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                abort();
                break;
        }
    }
    
    // @{Shutdown}
    restore_input_buffering();
}

