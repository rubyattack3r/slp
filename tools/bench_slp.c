#define _POSIX_C_SOURCE 200809L
#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_parser.h"
#include "slp_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void *bench_alloc(void *p, size_t s, void *u) {
    (void)u; if(!s){free(p);return NULL;} return realloc(p,s);
}
static SlpAllocator bench_allocator = {bench_alloc, NULL};

#ifdef _WIN32
#include <windows.h>
static double now_sec(void) {
    LARGE_INTEGER freq, counter;
    if (QueryPerformanceFrequency(&freq)) {
        QueryPerformanceCounter(&counter);
        return (double)counter.QuadPart / freq.QuadPart;
    }
    return (double)clock() / CLOCKS_PER_SEC;
}
#else
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

static double bench_compile(const char *src, int iterations) {
    SlpVM *vm = slp_vm_new(&bench_allocator);
    double start = now_sec();
    for (int i = 0; i < iterations; i++) {
        SlpParser parser;
        slp_parser_init(&parser, src, &bench_allocator);
        SlpASTNode *ast = slp_parser_parse(&parser);
        if (ast) {
            SlpObjFunction *fn = slp_compile(vm, ast, &bench_allocator);
            (void)fn;
            slp_parser_free_node(ast, &bench_allocator);
        }
    }
    double elapsed = now_sec() - start;
    slp_vm_free(vm);
    return elapsed;
}

static double bench_interpret(const char *src, int iterations) {
    double start = now_sec();
    for (int i = 0; i < iterations; i++) {
        SlpVM *vm = slp_vm_new(&bench_allocator);
        slp_vm_interpret(vm, src);
        slp_vm_free(vm);
    }
    double elapsed = now_sec() - start;
    return elapsed;
}

static double bench_interpret_reuse_vm(const char *src, int iterations) {
    SlpVM *vm = slp_vm_new(&bench_allocator);
    double start = now_sec();
    for (int i = 0; i < iterations; i++) {
        slp_vm_interpret(vm, src);
    }
    double elapsed = now_sec() - start;
    slp_vm_free(vm);
    return elapsed;
}

static void print_result(const char *name, int iters, double elapsed) {
    double per_iter_us = (elapsed / iters) * 1e6;
    double per_sec = iters / elapsed;
    printf("  %-35s %8d iterations in %6.3fs  (%7.1f us/op, %10.0f ops/s)\n",
           name, iters, elapsed, per_iter_us, per_sec);
}

int main(void) {
    printf("=== SlpVM Benchmark Suite ===\n\n");

    int compile_iters = 1000;
    int interpret_iters = 10000;

    printf("--- Compile throughput ---\n");
    print_result("simple expr (1+2)",
        compile_iters, bench_compile("1 + 2;", compile_iters));
    print_result("variable assign ($x=10;$y=$x+1)",
        compile_iters, bench_compile("$x = 10; $y = $x + 1;", compile_iters));
    print_result("if/else",
        compile_iters, bench_compile("if (true) { 1; } else { 2; }", compile_iters));
    print_result("while loop",
        compile_iters, bench_compile("while (false) { 1; }", compile_iters));
    print_result("for loop",
        compile_iters, bench_compile("for ($i=0; $i<10; $i=$i+1) { 1; }", compile_iters));
    print_result("try/catch",
        compile_iters, bench_compile("try { 1; } catch $e { 2; }", compile_iters));

    printf("\n--- Interpret throughput (fresh VM per iteration) ---\n");
    print_result("noop (empty)",
        interpret_iters, bench_interpret("1;", interpret_iters));
    print_result("arithmetic (1+2)",
        interpret_iters, bench_interpret("1 + 2;", interpret_iters));
    print_result("compare (1<2)",
        interpret_iters, bench_interpret("1 < 2;", interpret_iters));
    print_result("assign+read ($x=42;$x)",
        interpret_iters, bench_interpret("$x = 42;", interpret_iters));

    printf("\n--- Interpret throughput (reused VM) ---\n");
    print_result("arithmetic (1+2)",
        interpret_iters, bench_interpret_reuse_vm("1 + 2;", interpret_iters));
    print_result("assign+arith ($x=10;$y=$x*2+1)",
        interpret_iters, bench_interpret_reuse_vm("$x = 10; $y = $x * 2 + 1;", interpret_iters));

    printf("\n--- Loop performance (reused VM, single run) ---\n");
    {
        const char *fib_src =
            "$a = 0; $b = 1; $i = 0;"
            "while ($i < 30) {"
            "  $c = $a + $b;"
            "  $a = $b;"
            "  $b = $c;"
            "  $i = $i + 1;"
            "}";
        int fib_iters = 5000;
        double elapsed = bench_interpret_reuse_vm(fib_src, fib_iters);
        print_result("fibonacci(30) loop", fib_iters, elapsed);
    }
    {
        const char *sum_src =
            "$sum = 0;"
            "for ($i = 1; $i <= 1000; $i = $i + 1) {"
            "  $sum = $sum + $i;"
            "}";
        int sum_iters = 1000;
        double elapsed = bench_interpret_reuse_vm(sum_src, sum_iters);
        print_result("sum(1..1000) for loop", sum_iters, elapsed);
    }
    {
        const char *nested_src =
            "$count = 0;"
            "$i = 0;"
            "while ($i < 50) {"
            "  $j = 0;"
            "  while ($j < 50) {"
            "    $count = $count + 1;"
            "    $j = $j + 1;"
            "  }"
            "  $i = $i + 1;"
            "}";
        int nest_iters = 100;
        double elapsed = bench_interpret_reuse_vm(nested_src, nest_iters);
        print_result("nested loop 50x50", nest_iters, elapsed);
    }

    printf("\n--- GC pressure ---\n");
    {
        const char *str_src =
            "$s = \"hello\";";
        int str_iters = 50000;
        double elapsed = bench_interpret_reuse_vm(str_src, str_iters);
        print_result("string alloc (short)", str_iters, elapsed);
    }

    printf("\nDone.\n");
    return 0;
}
