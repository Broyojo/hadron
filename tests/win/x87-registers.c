/* The x87 instructions FEX runs through a C fallback must leave every general-purpose register as
 * it was. FEX-2611.1 built them as preserve_all functions, which clang 23.1 miscompiles for
 * Windows on Arm: FSIN, FCOS, FPTAN, F2XM1 and FPATAN returned with RBX holding FEX's
 * CpuStateFrame pointer, FYL2X with R12 (docs/findings.md #79: Subnautica's Mono code computed
 * Math.Cos with FCOS and then stored through RBX into FEX's thread state). Each instruction runs
 * with a known value in every register but RSP and RDI, with arguments inside and outside the
 * fast paths' ranges. Exit 0 when every register survives. Build:
 *   x86_64-w64-mingw32-clang -O2 x87-registers.c -o x87-registers.exe
 * Usage: x87-registers.exe */
#include <stdio.h>
#include <stdint.h>

struct block
{
    double in, out;
    uint64_t regs[14];
};

static const char *names[14] = { "rax", "rbx", "rcx", "rdx", "rsi", "rbp", "r8", "r9", "r10", "r11",
                                 "r12", "r13", "r14", "r15" };

/* Loads in, runs insns, stores st(0) to out and every register to regs. */
#define RUN(insns, blk) \
    __asm__ volatile( \
        "push %%rbx\n\tpush %%rbp\n\tpush %%r12\n\tpush %%r13\n\tpush %%r14\n\tpush %%r15\n\tpush %%rdi\n\t" \
        "movabs $0x1111111111111111, %%rax\n\t" \
        "movabs $0x2222222222222222, %%rbx\n\t" \
        "movabs $0x3333333333333333, %%rcx\n\t" \
        "movabs $0x4444444444444444, %%rdx\n\t" \
        "movabs $0x5555555555555555, %%rsi\n\t" \
        "movabs $0x6666666666666666, %%rbp\n\t" \
        "movabs $0x7777777777777777, %%r8\n\t" \
        "movabs $0x8888888888888888, %%r9\n\t" \
        "movabs $0x9999999999999999, %%r10\n\t" \
        "movabs $0xaaaaaaaaaaaaaaaa, %%r11\n\t" \
        "movabs $0xbbbbbbbbbbbbbbbb, %%r12\n\t" \
        "movabs $0xcccccccccccccccc, %%r13\n\t" \
        "movabs $0xdddddddddddddddd, %%r14\n\t" \
        "movabs $0xeeeeeeeeeeeeeeee, %%r15\n\t" \
        "fldl (%%rdi)\n\t" \
        insns "\n\t" \
        "fstpl 8(%%rdi)\n\t" \
        "fninit\n\t" \
        "mov %%rax, 16(%%rdi)\n\tmov %%rbx, 24(%%rdi)\n\tmov %%rcx, 32(%%rdi)\n\tmov %%rdx, 40(%%rdi)\n\t" \
        "mov %%rsi, 48(%%rdi)\n\tmov %%rbp, 56(%%rdi)\n\tmov %%r8, 64(%%rdi)\n\tmov %%r9, 72(%%rdi)\n\t" \
        "mov %%r10, 80(%%rdi)\n\tmov %%r11, 88(%%rdi)\n\tmov %%r12, 96(%%rdi)\n\tmov %%r13, 104(%%rdi)\n\t" \
        "mov %%r14, 112(%%rdi)\n\tmov %%r15, 120(%%rdi)\n\t" \
        "pop %%rdi\n\tpop %%r15\n\tpop %%r14\n\tpop %%r13\n\tpop %%r12\n\tpop %%rbp\n\tpop %%rbx\n\t" \
        : : "D"(blk) : "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "memory")

static int check(const char *name, const struct block *blk)
{
    int i, bad = 0;

    for (i = 0; i < 14; i++)
    {
        uint64_t want = 0x1111111111111111ull * (i + 1);
        if (blk->regs[i] == want) continue;
        printf("FAIL %-8s %g: %s = %016llx\n", name, blk->in, names[i], (unsigned long long)blk->regs[i]);
        bad = 1;
    }
    if (!bad) printf("ok   %-8s %g -> %g\n", name, blk->in, blk->out);
    return bad;
}

#define TEST(name, insns) do { RUN(insns, &blk); failures += check(name, &blk); } while (0)

int main(void)
{
    static const double args[] = { 0.5, 3.0, 1e10, 1e30 };
    int failures = 0;
    unsigned int a;

    for (a = 0; a < sizeof(args) / sizeof(args[0]); a++)
    {
        struct block blk = { args[a] };

        TEST("fsin", "fsin");
        TEST("fcos", "fcos");
        TEST("fsincos", "fsincos\n\tfstp %%st(1)");
        TEST("fptan", "fptan\n\tfstp %%st(0)");
        TEST("fsqrt", "fsqrt");
        TEST("f2xm1", "f2xm1");
        TEST("fyl2x", "fld1\n\tfyl2x");
        TEST("fyl2xp1", "fld1\n\tfyl2xp1");
        TEST("fpatan", "fld1\n\tfpatan");
        TEST("fprem", "fld1\n\tfxch\n\tfprem\n\tfstp %%st(1)");
        TEST("fprem1", "fld1\n\tfxch\n\tfprem1\n\tfstp %%st(1)");
        TEST("fscale", "fld1\n\tfscale\n\tfstp %%st(1)");
        TEST("frndint", "frndint");
        TEST("fxtract", "fxtract\n\tfstp %%st(1)");
    }
    printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
