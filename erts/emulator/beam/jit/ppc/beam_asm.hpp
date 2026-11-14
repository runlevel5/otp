/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <queue>
#include <map>
#include <functional>
#include <algorithm>
#include <cmath>

#include <asmjit/ppc64.h>

extern "C"
{
#ifdef HAVE_CONFIG_H
#    include "config.h"
#endif

#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "beam_catches.h"
#include "big.h"

#include "beam_asm.h"
}

#include "beam_jit_common.hpp"

using namespace asmjit;

#define ERTS_CCONV_ERTS
#define ERTS_CCONV_JIT

struct BeamAssembler : public BeamAssemblerCommon {
    BeamAssembler() : BeamAssemblerCommon(a) {
        Error err = code.attach(&a);
        ERTS_ASSERT(err == Error::kOk && "Failed to attach codeHolder");

        lateInit();
    }

    BeamAssembler(const std::string &log) : BeamAssembler() {
        if (erts_jit_asm_dump) {
            setLogger(log + ".asm");
        }
    }

protected:
    ppc::Assembler a;

    /* The native stack pointer. */
    const ppc::Gp sp = ppc::r1;

    /* Scratch register that is never live across helpers.
     *
     * It is used to move the link register to and from memory, and as the
     * target of instructions whose result is only wanted in the condition
     * register.
     *
     * Note that many instructions read this register as zero when it is used
     * as a base register, so it must NEVER be used to hold an address. */
    const ppc::Gp LR_TMP = ppc::r0;

    /* Points at the ErtsSchedulerRegisters struct of the scheduler, all of
     * which can be addressed with a 16-bit displacement. */
    const ppc::Gp scheduler_registers = ppc::r14;

    const ppc::Gp E = ppc::r15;
    const ppc::Gp c_p = ppc::r16;
    const ppc::Gp FCALLS = ppc::r17;
    const ppc::Gp HTOP = ppc::r18;

    /* Local copy of the active code index.
     *
     * This is set to ERTS_SAVE_CALLS_CODE_IX when save_calls is active, which
     * routes us to a common handler routine that calls save_calls before
     * jumping to the actual code. */
    const ppc::Gp active_code_ix = ppc::r19;

    /* X registers.
     *
     * All of them are callee-save, so they never have to be flushed just
     * because we are calling C code. */
#define ERTS_HIGHEST_CALLEE_SAVE_XREG 5
#define ERTS_HIGHEST_CALLER_SAVE_XREG 5
    const ppc::Gp XREG0 = ppc::r20;
    const ppc::Gp XREG1 = ppc::r21;
    const ppc::Gp XREG2 = ppc::r22;
    const ppc::Gp XREG3 = ppc::r23;
    const ppc::Gp XREG4 = ppc::r24;
    const ppc::Gp XREG5 = ppc::r25;

#define ERTS_LOWEST_CALLEE_SAVE_XREG (0)
#define ERTS_LOWEST_CALLER_SAVE_XREG (ERTS_HIGHEST_CALLEE_SAVE_XREG + 1)

    static const int num_register_backed_xregs = 6;
    const ppc::Gp register_backed_xregs[num_register_backed_xregs] =
            {XREG0, XREG1, XREG2, XREG3, XREG4, XREG5};

#ifdef ERTS_MSACC_EXTENDED_STATES
    const ppc::Mem erts_msacc_cache = getSchedulerRegRef(
            offsetof(ErtsSchedulerRegisters, aux_regs.d.erts_msacc_cache));
#endif

    /* * * * * * * * * */

    /*
     * The following registers are caller-save.
     *
     * Note that ARG1 is also the register for the return value.
     */
    const ppc::Gp ARG1 = ppc::r3;
    const ppc::Gp ARG2 = ppc::r4;
    const ppc::Gp ARG3 = ppc::r5;
    const ppc::Gp ARG4 = ppc::r6;
    const ppc::Gp ARG5 = ppc::r7;
    const ppc::Gp ARG6 = ppc::r8;
    const ppc::Gp ARG7 = ppc::r9;
    const ppc::Gp ARG8 = ppc::r10;

    const ppc::Gp TMP1 = ppc::r11;

    /*
     * Assume that SUPER_TMP will be destroyed by any helper function.
     *
     * The ELFv2 ABI expects the address of a function in this register when
     * the function is called through a pointer, which is how we call all C
     * code.
     */
    const ppc::Gp SUPER_TMP = ppc::r12;

    /*
     * The following registers are callee-save.
     *
     * Note that r2 (TOC pointer) and r13 (thread pointer) are reserved and
     * must not be used.
     */
    const ppc::Gp TMP2 = ppc::r26;
    const ppc::Gp TMP3 = ppc::r27;
    const ppc::Gp TMP4 = ppc::r28;
    const ppc::Gp TMP5 = ppc::r29;
    const ppc::Gp TMP6 = ppc::r30;

    /* Callee-saved floating-point registers. */
    const ppc::Vec FREG0 = ppc::f14;
    const ppc::Vec FREG1 = ppc::f15;
    const ppc::Vec FREG2 = ppc::f16;
    const ppc::Vec FREG3 = ppc::f17;
    const ppc::Vec FREG4 = ppc::f18;
    const ppc::Vec FREG5 = ppc::f19;
    const ppc::Vec FREG6 = ppc::f20;
    const ppc::Vec FREG7 = ppc::f21;
    static const int num_register_backed_fregs = 8;
    const ppc::Vec register_backed_fregs[num_register_backed_fregs] =
            {FREG0, FREG1, FREG2, FREG3, FREG4, FREG5, FREG6, FREG7};

    const ppc::Mem TMP_MEM1q = getSchedulerRegRef(
            offsetof(ErtsSchedulerRegisters, aux_regs.d.TMP_MEM[0]));
    const ppc::Mem TMP_MEM2q = getSchedulerRegRef(
            offsetof(ErtsSchedulerRegisters, aux_regs.d.TMP_MEM[1]));
    const ppc::Mem TMP_MEM3q = getSchedulerRegRef(
            offsetof(ErtsSchedulerRegisters, aux_regs.d.TMP_MEM[2]));
    const ppc::Mem TMP_MEM4q = getSchedulerRegRef(
            offsetof(ErtsSchedulerRegisters, aux_regs.d.TMP_MEM[3]));
    const ppc::Mem TMP_MEM5q = getSchedulerRegRef(
            offsetof(ErtsSchedulerRegisters, aux_regs.d.TMP_MEM[4]));

    /* Fill registers with undefined contents to find bugs faster.
     * A boxed value is most likely to cause noticeable trouble. */
    static const Uint64 bad_boxed_ptr = 0xcafebad0000002UL;

    /* Size of the stack frame that we set up before calling C code: the
     * header (back chain, CR, LR and TOC save words) followed by the
     * parameter save area for eight arguments, as the ELFv2 ABI wants. */
    static const int RUNTIME_FRAME_SIZE = 32 + 8 * sizeof(UWord);

    /* Offset to where a function saves its return address in the stack frame
     * of its caller. */
    static const int RUNTIME_FRAME_LR_OFFSET = 16;

    /* Highest displacement that can be used by an instruction that addresses
     * memory, 64-bit loads and stores furthermore require that it is a
     * multiple of 4. */
    static const ssize_t MAX_DISPLACEMENT = 0x7FFF;

    constexpr ppc::Mem getSchedulerRegRef(int offset) const {
        ASSERT((offset & (sizeof(Eterm) - 1)) == 0);
        ASSERT(offset >= 0 && offset <= MAX_DISPLACEMENT);
        return ppc::Mem(scheduler_registers, offset);
    }

    constexpr ppc::Mem getFRef(int index, size_t size = sizeof(UWord)) const {
        int base = offsetof(ErtsSchedulerRegisters, f_reg_array.d);
        int offset = index * sizeof(FloatDef);

        ASSERT(0 <= index && index <= 1023);
        return getSchedulerRegRef(base + offset);
    }

    constexpr ppc::Mem getXRef(int index) const {
        int base = offsetof(ErtsSchedulerRegisters, x_reg_array.d);
        int offset = index * sizeof(Eterm);

        ASSERT(0 <= index && index < ERTS_X_REGS_ALLOCATED);
        return getSchedulerRegRef(base + offset);
    }

    constexpr ppc::Mem getYRef(int index) const {
        ASSERT(0 <= index && index <= 1023);

        return ppc::Mem(E, index * sizeof(Eterm));
    }

    /* Note that the displacements of the following two are not multiples of
     * 4, so they cannot be used directly by 64-bit loads and stores. */
    constexpr ppc::Mem getCARRef(ppc::Gp Src) const {
        return ppc::Mem(Src, -TAG_PRIMARY_LIST);
    }

    constexpr ppc::Mem getCDRRef(ppc::Gp Src,
                                 size_t size = sizeof(UWord)) const {
        return ppc::Mem(Src, -TAG_PRIMARY_LIST + sizeof(Eterm));
    }

    /* Loads the X register array into `to`. Remember to sync the registers in
     * `emit_enter_runtime`. */
    void load_x_reg_array(ppc::Gp to) {
        int offset = offsetof(ErtsSchedulerRegisters, x_reg_array.d);

        lea(to, getSchedulerRegRef(offset));
    }

    void load_erl_bits_state(ppc::Gp to) {
        int offset =
                offsetof(ErtsSchedulerRegisters, aux_regs.d.erl_bits_state);

        lea(to, getSchedulerRegRef(offset));
    }

    void emit_assert_redzone_unused() {
        /* JIT_HARD_DEBUG is not supported yet. */
    }

    /*
     * Calls an Erlang function.
     */
    template<typename Any>
    void erlang_call(Any Target) {
        emit_assert_redzone_unused();
        aligned_call(Target);
    }

    /* Loads a 64-bit value from `mem`, which is either a base and an index
     * register, or a base register with any displacement.
     *
     * A 64-bit load can only encode a displacement that is a multiple of 4,
     * which the displacement into a tagged pointer never is. All other
     * displacements are loaded into SUPER_TMP and used as an index, which
     * keeps the instruction that computes the address out of the dependency
     * chain when following pointers. */
    void load_mem(ppc::Gp to, ppc::Mem mem) {
        int64_t offset = mem.offset();

        if (mem.has_index()) {
            a.ldx(to, mem);
        } else if (Support::is_int_n<16>(offset) && (offset & 3) == 0) {
            a.ld(to, mem);
        } else {
            ppc::Gp base = ppc::Gp::make_r64(mem.base_id());

            ASSERT(base != SUPER_TMP);
            mov_imm(SUPER_TMP, offset);
            a.ldx(to, ppc::Mem(base, SUPER_TMP));
        }
    }

    /* Stores a 64-bit value to `mem`, see `load_mem` for details. */
    void store_mem(ppc::Gp from, ppc::Mem mem) {
        int64_t offset = mem.offset();

        if (mem.has_index()) {
            a.stdx(from, mem);
        } else if (Support::is_int_n<16>(offset) && (offset & 3) == 0) {
            a.std(from, mem);
        } else {
            ppc::Gp base = ppc::Gp::make_r64(mem.base_id());

            ASSERT(base != SUPER_TMP && from != SUPER_TMP);
            mov_imm(SUPER_TMP, offset);
            a.stdx(from, ppc::Mem(base, SUPER_TMP));
        }
    }

    void branch(ppc::Mem target) {
        load_mem(SUPER_TMP, target);
        a.mtctr(SUPER_TMP);
        a.bctr();
    }

    template<typename FuncPtr>
    void aligned_call(FuncPtr(*target)) {
        mov_imm(SUPER_TMP, target);
        a.mtctr(SUPER_TMP);
        a.bctrl();
    }

    void aligned_call(Label target) {
        a.bl(target);
    }

    void aligned_call(ppc::Gp target) {
        a.mtctr(target);
        a.bctrl();
    }

    /* Calls the given address. In DEBUG builds, make
     * sure that the CP is aligned. */
    void aligned_call(ppc::Mem target) {
        ERTS_CT_ASSERT(_CPMASK == 3);
        ASSERT(is_CP(a.offset()));
        load_mem(SUPER_TMP, target);
        a.mtctr(SUPER_TMP);
        a.bctrl();
    }

    template<typename T, typename = void>
    struct function_traits;

    template<typename Range, typename... Domain>
    struct function_traits<Range (*)(Domain...)> {
        static constexpr size_t Arity = sizeof...(Domain);
    };

    /* Calls a C function. Its address must be in SUPER_TMP (r12) when it is
     * entered, as that is how it finds its TOC. */
    template<typename T, T Func>
    void runtime_call() {
        mov_imm(SUPER_TMP, Func);
        a.mtctr(SUPER_TMP);
        a.bctrl();
    }

    template<int Arity>
    void dynamic_runtime_call(ppc::Gp func) {
        ERTS_CT_ASSERT(Arity <= 4);
        if (func != SUPER_TMP) {
            a.mr(SUPER_TMP, func);
        }
        a.mtctr(SUPER_TMP);
        a.bctrl();
    }

    /* Explicitly position-independent absolute jump, for use in fragments that
     * need to be memcpy'd for performance reasons (e.g. NIF stubs) */
    template<typename T>
    void pic_jmp(T(*addr)) {
        mov_imm(SUPER_TMP, addr);
        a.mtctr(SUPER_TMP);
        a.bctr();
    }

    constexpr ppc::Mem getArgRef(const ArgRegister &arg) const {
        if (arg.isXRegister()) {
            return getXRef(arg.as<ArgXRegister>().get());
        } else if (arg.isYRegister()) {
            return getYRef(arg.as<ArgYRegister>().get());
        }

        return getFRef(arg.as<ArgFRegister>().get());
    }

    /* Returns the current code address for the `Export` or `ErlFunEntry` in
     * `Src`.
     *
     * Export tracing, save_calls, etc are implemented by shared fragments that
     * assume that the respective entry is in ARG1, so we have to copy it over
     * if it isn't already.
     *
     * Clobbers SUPER_TMP, which holds the index of the returned reference. */
    ppc::Mem emit_setup_dispatchable_call(const ppc::Gp &Src) {
        return emit_setup_dispatchable_call(Src, active_code_ix);
    }

    ppc::Mem emit_setup_dispatchable_call(const ppc::Gp &Src,
                                          const ppc::Gp &CodeIndex) {
        if (ARG1 != Src) {
            a.mr(ARG1, Src);
        }

        ERTS_CT_ASSERT(offsetof(ErlFunEntry, dispatch) == 0);
        ERTS_CT_ASSERT(offsetof(Export, dispatch) == 0);
        ERTS_CT_ASSERT(offsetof(ErtsDispatchable, addresses) == 0);

        a.sldi(SUPER_TMP, CodeIndex, 3);

        return ppc::Mem(ARG1, SUPER_TMP);
    }

    /* Prefer `eHeapAlloc` over `eStack | eHeap` when calling
     * functions in the runtime system that allocate heap
     * memory (`HAlloc`, heap factories, etc).
     *
     * Prefer `eHeapOnlyAlloc` over `eHeapAlloc` for functions
     * that assume there's already a certain amount of free
     * space on the heap, such as those using `HeapOnlyAlloc`
     * or similar. It's slightly cheaper in release builds,
     * and in debug builds it updates `eStack` to ensure that
     * we can make heap size assertions. */
    enum Update : int {
        eStack = (1 << 0),
        eHeap = (1 << 1),
        eReductions = (1 << 2),
        eCodeIndex = (1 << 3),
        eXRegs = (1 << 4),
        eHeapAlloc = Update::eHeap | Update::eStack,
#ifndef DEBUG
        eHeapOnlyAlloc = Update::eHeap,
#else
        eHeapOnlyAlloc = Update::eHeapAlloc
#endif
    };

    /* The return address is kept in the link register when a function is
     * entered, and we push it to the Erlang stack to be able to call other
     * functions.
     *
     * Frame pointers on the Erlang stack are not supported yet. */
    void emit_enter_erlang_frame() {
        ASSERT(erts_frame_layout == ERTS_FRAME_LAYOUT_RA);
        a.mflr(LR_TMP);
        a.stdu(LR_TMP, ppc::Mem(E, -8));
    }

    void emit_leave_erlang_frame() {
        ASSERT(erts_frame_layout == ERTS_FRAME_LAYOUT_RA);
        a.ld(LR_TMP, ppc::Mem(E));
        a.addi(E, E, imm(8));
        a.mtlr(LR_TMP);
    }

    /* Saves the link register and sets up a new frame on the native stack,
     * which is required when a shared fragment that was entered by a call
     * needs to call C code or another fragment.
     *
     * Module code does not need this as it keeps its return address on the
     * Erlang stack, and `process_main` has set up a frame that the C code we
     * call can use. */
    void emit_enter_runtime_frame() {
        a.mflr(LR_TMP);
        a.std(LR_TMP, ppc::Mem(sp, RUNTIME_FRAME_LR_OFFSET));
        a.stdu(sp, ppc::Mem(sp, -RUNTIME_FRAME_SIZE));
    }

    void emit_leave_runtime_frame() {
        a.addi(sp, sp, imm(RUNTIME_FRAME_SIZE));
        a.ld(LR_TMP, ppc::Mem(sp, RUNTIME_FRAME_LR_OFFSET));
        a.mtlr(LR_TMP);
    }

    /* We keep the first six X registers in machine registers, all of which
     * are callee-save.
     *
     * We therefore only save them when told to sync all registers with the
     * `Update::eXRegs` flag, and then we ignore the ones above `live` to
     * reduce the save/restore traffic. It's enough for this figure to be at
     * least as high as the number of actually live registers, and we default
     * to all six registers when we don't know the exact number. */

    template<int Spec = 0>
    void emit_enter_runtime(int live = num_register_backed_xregs) {
        ERTS_CT_ASSERT((Spec & (Update::eReductions | Update::eStack |
                                Update::eHeap | Update::eXRegs)) == Spec);

        if (Spec & Update::eStack) {
            a.std(E, ppc::Mem(c_p, offsetof(Process, stop)));
        } else {
#ifdef DEBUG
            /* Store some garbage in the process structure to catch missing
             * updates. */
            a.std(active_code_ix, ppc::Mem(c_p, offsetof(Process, stop)));
#endif
        }

        if (Spec & Update::eHeap) {
            a.std(HTOP, ppc::Mem(c_p, offsetof(Process, htop)));
        } else {
#ifdef DEBUG
            a.std(active_code_ix, ppc::Mem(c_p, offsetof(Process, htop)));
#endif
        }

        if (Spec & Update::eReductions) {
            ERTS_CT_ASSERT(sizeof(((Process *)nullptr)->fcalls) ==
                           sizeof(Sint32));
            a.stw(FCALLS, ppc::Mem(c_p, offsetof(Process, fcalls)));
        }

        /* Save register-backed X registers to the X register array when
         * needed. The backing registers must NOT be used afterwards.
         *
         * In a DEBUG build, the backing X registers will be overwritten with
         * garbage values. */
        if (live > 0 && (Spec & Update::eXRegs)) {
            int num_to_save = MIN(live, num_register_backed_xregs);

#ifdef DEBUG
            /* Destroy the saved X registers to find bugs sooner.*/
            mov_imm(SUPER_TMP, bad_boxed_ptr + 0x20 + (Spec << 8));
#endif

            for (int i = 0; i < num_to_save; i++) {
                a.std(register_backed_xregs[i], getXRef(i));

#ifdef DEBUG
                a.mr(register_backed_xregs[i], SUPER_TMP);
#endif
            }
        }
    }

    template<int Spec = 0>
    void emit_leave_runtime(int live = num_register_backed_xregs) {
        ERTS_CT_ASSERT(
                (Spec & (Update::eReductions | Update::eStack | Update::eHeap |
                         Update::eXRegs | Update::eCodeIndex)) == Spec);

        if (Spec & Update::eHeap) {
            a.ld(HTOP, ppc::Mem(c_p, offsetof(Process, htop)));
        }

        if (Spec & Update::eStack) {
            a.ld(E, ppc::Mem(c_p, offsetof(Process, stop)));
        }

        if (Spec & Update::eReductions) {
            a.lwa(FCALLS, ppc::Mem(c_p, offsetof(Process, fcalls)));
        }

        if (Spec & Update::eCodeIndex) {
            /* Updates the local copy of the active code index, retaining
             * save_calls if active. */
            mov_imm(SUPER_TMP, &the_active_code_index);
            a.lwz(SUPER_TMP, ppc::Mem(SUPER_TMP));
            a.cmpdi(active_code_ix, imm(ERTS_SAVE_CALLS_CODE_IX));
            a.isel(active_code_ix, active_code_ix, SUPER_TMP, ppc::cr0.eq());
        }

        /* Restore register-backed X registers from the X register array when
         * needed. The register array must NOT be used afterwards.
         *
         * In a DEBUG build, the register array will be overwritten with
         * garbage values. */
        if (live > 0 && (Spec & Update::eXRegs)) {
            int num_to_restore = MIN(live, num_register_backed_xregs);

#ifdef DEBUG
            /* Destroy the restored X registers to find bugs sooner.*/
            mov_imm(SUPER_TMP, bad_boxed_ptr + 0x80 + (Spec << 8));
#endif

            for (int i = 0; i < num_to_restore; i++) {
                a.ld(register_backed_xregs[i], getXRef(i));

#ifdef DEBUG
                a.std(SUPER_TMP, getXRef(i));
#endif
            }
        }
    }

    /* The following tests branch on a single bit of the primary tag. The bit
     * is isolated in LR_TMP, which only serves as a place to put a result we
     * do not need, and the outcome ends up in CR0.
     *
     * Note that a conditional branch only reaches 32KB. */
    void emit_is_cons(Label Fail, ppc::Gp Src) {
        const int bitNumber = 1;
        ERTS_CT_ASSERT(_TAG_PRIMARY_MASK - TAG_PRIMARY_LIST ==
                       (1 << bitNumber));
        a.andi_rc(LR_TMP, Src, imm(1 << bitNumber));
        a.bne(Fail);
    }

    void emit_is_not_cons(Label Fail, ppc::Gp Src) {
        const int bitNumber = 1;
        ERTS_CT_ASSERT(_TAG_PRIMARY_MASK - TAG_PRIMARY_LIST ==
                       (1 << bitNumber));
        a.andi_rc(LR_TMP, Src, imm(1 << bitNumber));
        a.beq(Fail);
    }

    void emit_is_boxed(Label Fail, ppc::Gp Src) {
        const int bitNumber = 0;
        ERTS_CT_ASSERT(_TAG_PRIMARY_MASK - TAG_PRIMARY_BOXED ==
                       (1 << bitNumber));
        a.andi_rc(LR_TMP, Src, imm(1 << bitNumber));
        a.bne(Fail);
    }

    void emit_is_not_boxed(Label Fail, ppc::Gp Src) {
        const int bitNumber = 0;
        ERTS_CT_ASSERT(_TAG_PRIMARY_MASK - TAG_PRIMARY_BOXED ==
                       (1 << bitNumber));
        a.andi_rc(LR_TMP, Src, imm(1 << bitNumber));
        a.beq(Fail);
    }

    ppc::Gp emit_ptr_val(ppc::Gp Dst, ppc::Gp Src) {
#if !defined(TAG_LITERAL_PTR)
        return Src;
#else
        /* We intentionally skip TAG_PTR_MASK__ here, as we want to use
         * plain `emit_boxed_val` when we know the argument can't be a literal,
         * such as in bit-syntax matching. */
        a.li(LR_TMP, imm(~TAG_LITERAL_PTR));
        a.and_(Dst, Src, LR_TMP);
        return Dst;
#endif
    }

    /* Converts the signed integer in `src` to a double.
     *
     * The value has to go through memory, as there is no instruction that
     * moves a general purpose register to a floating point register on all
     * the processors we support. Clobbers TMP_MEM5q. */
    void emit_int_to_double(ppc::Vec dst, ppc::Gp src) {
        a.std(src, TMP_MEM5q);
        a.lfd(dst, TMP_MEM5q);
        a.fcfid(dst, dst);
    }

    void emit_untag_ptr(ppc::Gp Dst, ppc::Gp Src) {
        ERTS_CT_ASSERT(TAG_PTR_MASK__ == 7);
        a.clrrdi(Dst, Src, 3);
    }

    /* Note that the displacement is not a multiple of 4, so the result cannot
     * be used directly by 64-bit loads and stores. */
    constexpr ppc::Mem emit_boxed_val(ppc::Gp Src, int32_t bytes = 0) const {
        ASSERT(bytes % sizeof(Eterm) == 0);
        return ppc::Mem(Src, bytes - TAG_PRIMARY_BOXED);
    }

    void emit_branch_if_not_value(ppc::Gp reg, Label lbl) {
        emit_branch_if_eq(reg, THE_NON_VALUE, lbl);
    }

    void emit_branch_if_value(ppc::Gp reg, Label lbl) {
        emit_branch_if_ne(reg, THE_NON_VALUE, lbl);
    }

    void emit_branch_if_eq(ppc::Gp reg, Uint value, Label lbl) {
        cmp(reg, value);
        a.beq(lbl);
    }

    void emit_branch_if_ne(ppc::Gp reg, Uint value, Label lbl) {
        cmp(reg, value);
        a.bne(lbl);
    }

    /* Conditional branches that can reach as far as an unconditional one.
     * Use them when the target may be more than 32KB away and there are no
     * veneers to rely on, as in the shared fragments. */
    void far_beq(Label target, ppc::Cr cr = ppc::cr0) {
        Label next = a.new_label();

        a.bne(cr, next);
        a.b(target);
        a.bind(next);
    }

    void far_bne(Label target, ppc::Cr cr = ppc::cr0) {
        Label next = a.new_label();

        a.beq(cr, next);
        a.b(target);
        a.bind(next);
    }

    /* Set the EQ bit of CR0 if Reg1 and Reg2 are definitely not equal based
     * on their tags alone. (They may still be equal if both are
     * immediates and all other bits are equal too.) */
    void emit_is_unequal_based_on_tags(ppc::Gp Reg1, ppc::Gp Reg2) {
        ERTS_CT_ASSERT(TAG_PRIMARY_IMMED1 == _TAG_PRIMARY_MASK);
        ERTS_CT_ASSERT((TAG_PRIMARY_LIST | TAG_PRIMARY_BOXED) ==
                       TAG_PRIMARY_IMMED1);
        a.or_(SUPER_TMP, Reg1, Reg2);
        a.andi_rc(SUPER_TMP, SUPER_TMP, imm(_TAG_PRIMARY_MASK));

        /*
         * SUPER_TMP will be now be TAG_PRIMARY_IMMED1 if either
         * one or both registers are immediates, or if one register
         * is a list and the other a boxed.
         */
        a.cmpdi(SUPER_TMP, imm(TAG_PRIMARY_IMMED1));
    }

    /* Loads a 64-bit constant, which takes from one to five instructions
     * depending on the value. */
    void mov_imm64(ppc::Gp to, Sint64 value) {
        if (Support::is_int_n<16>(value)) {
            a.li(to, imm(value));
        } else if (Support::is_int_n<32>(value)) {
            a.lis(to, imm((Sint16)(value >> 16)));

            if (value & 0xFFFF) {
                a.ori(to, to, imm(value & 0xFFFF));
            }
        } else {
            Sint64 high = value >> 32;
            Uint32 low = (Uint32)value;

            mov_imm64(to, high);

            if (high != 0) {
                a.sldi(to, to, 32);
            }

            if (low >> 16) {
                a.oris(to, to, imm(low >> 16));
            }

            if (low & 0xFFFF) {
                a.ori(to, to, imm(low & 0xFFFF));
            }
        }
    }

    template<typename T>
    void mov_imm(ppc::Gp to, T value) {
        static_assert(std::is_integral<T>::value || std::is_pointer<T>::value);
        mov_imm64(to, (Sint64)value);
    }

    void mov_imm(ppc::Gp to, std::nullptr_t value) {
        (void)value;
        mov_imm64(to, 0);
    }

    void sub(ppc::Gp to, ppc::Gp src, int64_t val) {
        add(to, src, -val);
    }

    void add(ppc::Gp to, ppc::Gp src, int64_t val) {
        /* The source would be read as zero if it was r0. */
        ASSERT(src != LR_TMP);

        if (val == 0) {
            if (to != src) {
                a.mr(to, src);
            }
        } else if (Support::is_int_n<16>(val)) {
            a.addi(to, src, imm(val));
        } else if (to != LR_TMP &&
                   Support::is_int_n<16>((val + 0x8000) >> 16)) {
            /* The low half is sign-extended when it is added, which the high
             * half has to compensate for. */
            Sint16 low = (Sint16)val;

            a.addis(to, src, imm((val + 0x8000) >> 16));

            if (low != 0) {
                a.addi(to, to, imm(low));
            }
        } else {
            ASSERT(src != SUPER_TMP);
            mov_imm(SUPER_TMP, val);
            a.add(to, src, SUPER_TMP);
        }
    }

    /* Signed compare of `src` and `val`, with the result in `cr`. */
    void cmp(ppc::Gp src, int64_t val, ppc::Cr cr = ppc::cr0) {
        if (Support::is_int_n<16>(val)) {
            a.cmpdi(cr, src, imm(val));
        } else {
            ASSERT(src != SUPER_TMP);
            mov_imm(SUPER_TMP, val);
            a.cmpd(cr, src, SUPER_TMP);
        }
    }

    /* Unsigned compare of `src` and `val`, with the result in `cr`. */
    void cmpl(ppc::Gp src, uint64_t val, ppc::Cr cr = ppc::cr0) {
        if (Support::is_uint_n<16>(val)) {
            a.cmpldi(cr, src, imm(val));
        } else {
            ASSERT(src != SUPER_TMP);
            mov_imm(SUPER_TMP, val);
            a.cmpld(cr, src, SUPER_TMP);
        }
    }

    /* Emits a word that is not an instruction, which raises SIGILL should it
     * ever be executed. `code` tells the places that use this apart. */
    void udf(Uint16 code) {
        /* Primary opcode 0 is not assigned to any instruction. */
        a.embed_uint32(code);
    }

    /* Loads the address of `label`.
     *
     * The first instruction provides the address of the one that follows it.
     * When the label is bound we know how far away it is and can add the
     * distance, whatever it is. Otherwise the second instruction adds its own
     * distance to the label, which must then be bound within 32KB of it. */
    void emit_adr(ppc::Gp to, const Label &label) {
        ASSERT(to != LR_TMP);

        a.lnia(to);

        if (code.is_label_bound(label)) {
            Sint64 distance = (Sint64)code.label_offset_from_base(label) -
                              (Sint64)a.offset();

            if (distance != 0) {
                add(to, to, distance);
            }
        } else {
            a.addi(to, to, label);
        }
    }

    /*
     * PPC has no LEA instruction. Implement our own to enable us
     * to use helpers based on getSchedulerRegRef() that return a
     * ppc::Mem class.
     */
    void lea(ppc::Gp to, ppc::Mem mem) {
        ASSERT(mem.has_base_reg() && !mem.has_index());

        add(to, ppc::Gp::make_r64(mem.base_id()), mem.offset());
    }

    template<size_t N>
    class ImmedRegCache {
        static_assert(N > 0, "ImmedRegCache requires at least one register");

    private:
        std::unordered_map<Eterm, ppc::Gp> values;
        std::array<Eterm, N> reg_values;
        std::array<ppc::Gp, N> regs;
        size_t next_reg = 0;
        BeamAssembler &ba;

    public:
        template<typename... Regs>
        ImmedRegCache(BeamAssembler &ba, Regs... regs) : regs{regs...}, ba(ba) {
            static_assert(sizeof...(Regs) == N,
                          "ImmedRegCache register count must match N");
            reg_values.fill(THE_NON_VALUE);
        }

        ppc::Gp load_value(Eterm value) {
            auto search = values.find(value);
            if (search != values.end()) {
                return search->second;
            }

            auto new_reg = regs[next_reg];

            if (is_value(reg_values[next_reg])) {
                values.erase(reg_values[next_reg]);
            }

            values.emplace(value, new_reg);
            reg_values[next_reg] = value;
            ba.mov_imm(new_reg, value);

            next_reg = (next_reg + 1) % N;

            return new_reg;
        }
    };
};

#include "beam_asm_global.hpp"

class BeamModuleAssembler : public BeamAssembler,
                            public BeamModuleAssemblerCommon {
    BeamGlobalAssembler *ga;

    /* Sequence number used to create unique named labels by
     * resolve_label(). Only used when assembly output has been
     * requested. */
    long labelSeq = 0;

    /* Save the last PC for an error. */
    size_t last_error_offset = 0;

    static constexpr ptrdiff_t STUB_CHECK_INTERVAL = 4 << 10;
    static constexpr ptrdiff_t STUB_CHECK_INTERVAL_UNREACHABLE =
            (4 << 10) - 128;
    size_t last_stub_check_offset = 0;

    /* Save the last known unreachable position. */
    size_t last_unreachable_offset = 0;

    /* Mark this point unreachable. This must be placed at the very end when
     * used in a BEAM instruction, and should not be used in helper
     * functions. */
    void mark_unreachable() {
        last_unreachable_offset = a.offset();
    }

    /* Use within BEAM instructions. */
    void mark_unreachable_check_pending_stubs() {
        mark_unreachable();
        check_pending_stubs();
    }

    bool is_unreachable() {
        return a.offset() == last_unreachable_offset;
    }

    enum Displacement : size_t {
        /* Pessimistic estimate for helper functions, where we don't know the
         * branch displacement or whether it will be used near label
         * resolution.
         *
         * Note that we subtract the size of one instruction to handle
         * backward displacements. */
        dispUnknown = (32 << 10) - sizeof(Uint32) - STUB_CHECK_INTERVAL,

        /* +- 32KB: conditional branches (`bc`), and everything that addresses
         * a label relative to itself, such as loads of 8-byte literals. */
        disp32K = (32 << 10) - sizeof(Uint32),

        /* +- 32MB: `b`, `bl` */
        disp32MB = (32 << 20) - sizeof(Uint32),

        dispMin = dispUnknown,
        dispMax = disp32MB
    };

    static_assert(dispMin <= dispUnknown && dispMax >= disp32MB);
    static_assert(STUB_CHECK_INTERVAL < dispMin / 2);

    struct Veneer {
        ssize_t latestOffset;
        Label anchor;

        Label target;

        constexpr bool operator>(const Veneer &other) const {
            return latestOffset > other.latestOffset;
        }
    };

    struct Constant {
        ssize_t latestOffset;
        Label anchor;

        ArgVal value;

        constexpr bool operator>(const Constant &other) const {
            return latestOffset > other.latestOffset;
        }
    };

    struct EmbeddedLabel {
        ssize_t latestOffset;
        Label anchor;

        Label label;

        constexpr bool operator>(const EmbeddedLabel &other) const {
            return latestOffset > other.latestOffset;
        }
    };

    /* ArgVal -> Constant
     *
     * `_pending_constants` points directly into this container, which is
     * documented to be safe as long as we only insert elements. */
    std::unordered_multimap<ArgVal, const Constant, ArgVal::Hash> _constants;

    /* Label::id() -> Veneer
     *
     * `_pending_veneers` points directly into this container. */
    std::unordered_multimap<uint32_t, const Veneer> _veneers;

    template<typename T>
    using PendingStubs =
            std::priority_queue<std::reference_wrapper<const T>,
                                std::deque<std::reference_wrapper<const T>>,
                                std::greater<const T &>>;

    /* Index of Label -> EmbeddedLabel
     *
     * `_pending_labels` points directly into this container. */
    std::unordered_map<uint32_t, EmbeddedLabel> _embedded_labels;

    /* All pending stubs, segregated by type and sorted by `latestOffset` in
     * ascending order.
     *
     * We use separate queues to avoid interleaving them, as they have
     * different sizes and alignment requirements. */
    PendingStubs<Constant> _pending_constants;
    PendingStubs<Veneer> _pending_veneers;
    PendingStubs<EmbeddedLabel> _pending_labels;

    /* Maps code pointers to thunks that jump to them, letting us treat global
     * fragments as if they were local. */
    std::unordered_map<void (*)(), Label> _dispatchTable;

    /* Size of an entry in the dispatch table: four instructions followed by
     * the address of the fragment. */
    static constexpr size_t DISPATCH_THUNK_SIZE =
            sizeof(Uint32[4]) + sizeof(UWord);

    RegisterCache<16, ppc::Mem, ppc::Gp> reg_cache =
            RegisterCache<16, ppc::Mem, ppc::Gp>(scheduler_registers, E, {});

    void reg_cache_put(ppc::Mem mem, ppc::Gp src) {
        if (src != SUPER_TMP) {
            reg_cache.put(mem, src);
        } else {
            reg_cache.invalidate(mem);
        }
    }

    ppc::Gp find_cache(ppc::Mem mem) {
        return reg_cache.find(a.offset(), mem);
    }

    /* Works as the STD instruction, but also updates the cache. */
    void std_cache(ppc::Gp src, ppc::Mem mem_dst) {
        reg_cache.consolidate(a.offset());
        reg_cache.invalidate(src);

        a.std(src, mem_dst);

        reg_cache_put(mem_dst, src);
        reg_cache.update(a.offset());
    }

    /* Works like LD, but looks in the cache first. */
    void ld_cached(ppc::Gp dst, ppc::Mem mem) {
        ppc::Gp cached_reg = find_cache(mem);

        if (cached_reg.is_valid()) {
            /* This memory location is cached. */
            if (cached_reg == dst) {
                comment("skipped fetching of BEAM register");
            } else {
                comment("simplified fetching of BEAM register");
                a.mr(dst, cached_reg);
                reg_cache.invalidate(dst);
                reg_cache.update(a.offset());
            }
        } else {
            /* Not cached. Load and update cache. */
            a.ld(dst, mem);
            reg_cache.invalidate(dst);
            reg_cache_put(mem, dst);
            reg_cache.update(a.offset());
        }
    }

    template<typename L, typename... Any>
    void preserve_cache(L generate, Any... clobber) {
        bool valid = reg_cache.validAt(a.offset());

        generate();

        if (valid) {
            if (sizeof...(clobber) > 0) {
                reg_cache.invalidate(clobber...);
            }

            reg_cache.update(a.offset());
        }
    }

    void trim_preserve_cache(const ArgWord &Words) {
        if (Words.get() > 0) {
            ASSERT(Words.get() <= 1023);

            preserve_cache([&]() {
                auto offset = Words.get() * sizeof(Eterm);
                add(E, E, offset);
                reg_cache.trim_yregs(-offset);
            });
        }
    }

    void mov_preserve_cache(ppc::Gp dst, ppc::Gp src) {
        preserve_cache(
                [&]() {
                    a.mr(dst, src);
                },
                dst);
    }

    void untag_ptr_preserve_cache(ppc::Gp dst, ppc::Gp src) {
        preserve_cache(
                [&]() {
                    emit_untag_ptr(dst, src);
                },
                dst);
    }

    /* Embeds the address of `label` and returns a label for where it is
     * stored.
     *
     * This must be used _IMMEDIATELY BEFORE_ the pair of instructions that
     * loads the address, see `embed_constant` for details. */
    Label embed_label(const Label &label, enum Displacement disp);

public:
    BeamModuleAssembler(BeamGlobalAssembler *ga,
                        Eterm mod,
                        int num_labels,
                        const BeamFile *file = NULL);
    BeamModuleAssembler(BeamGlobalAssembler *ga,
                        Eterm mod,
                        int num_labels,
                        int num_functions,
                        const BeamFile *file = NULL);

    bool emit(unsigned op, const Span<const ArgVal> &args);

    void emit_coverage(void *coverage, Uint index, Uint size);

    void codegen(JitAllocator *allocator,
                 const void **executable_ptr,
                 void **writable_ptr,
                 const BeamCodeHeader *in_hdr,
                 const BeamCodeHeader **out_exec_hdr,
                 BeamCodeHeader **out_rw_hdr);

    void codegen(JitAllocator *allocator,
                 const void **executable_ptr,
                 void **writable_ptr);

    void codegen(char *buff, size_t len);

    AsmMetadata prepared_metadata;
    bool metadata_prepared = false;
    void prepare_metadata(const BeamCodeHeader *header);
    void *register_metadata(const BeamCodeHeader *header);

    ErtsCodePtr getCode(unsigned label);
    ErtsCodePtr getLambda(unsigned index);

    void *getCode(Label label) {
        return BeamAssembler::getCode(label);
    }

    byte *getCode(char *labelName) {
        return BeamAssembler::getCode(labelName);
    }

    void embed_vararg_rodata(const Span<const ArgVal> &args, ppc::Gp reg);

    unsigned getCodeSize() {
        ASSERT(code.has_base_address());
        return code.code_size();
    }

    void copyCodeHeader(BeamCodeHeader *hdr);
    BeamCodeHeader *getCodeHeader(void);
    const ErtsCodeInfo *getOnLoad(void);

    unsigned patchCatches(char *rw_base);
    void patchLambda(char *rw_base, unsigned index, const ErlFunEntry *fe);
    void patchLiteral(char *rw_base, unsigned index, Eterm lit);
    void patchImport(char *rw_base, unsigned index, const Export *import);
    void patchStrings(char *rw_base, const byte *string);

protected:
    void emit_is_cons(Label Fail, ppc::Gp Src) {
        preserve_cache([&]() {
            BeamAssembler::emit_is_cons(Fail, Src);
        });
    }

    void emit_is_not_cons(Label Fail, ppc::Gp Src) {
        preserve_cache([&]() {
            BeamAssembler::emit_is_not_cons(Fail, Src);
        });
    }

    void emit_is_list(Label Fail, ppc::Gp Src) {
        preserve_cache([&]() {
            /* The value is a list if it is a cons cell or NIL. Both tests
             * are done before branching on their combined result. */
            a.andi_rc(LR_TMP, Src, imm(_TAG_PRIMARY_MASK - TAG_PRIMARY_LIST));
            cmp(Src, NIL, ppc::cr1);
            a.cror(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
            a.bne(Fail);
        });
    }

    void emit_is_boxed(Label Fail, ppc::Gp Src) {
        preserve_cache([&]() {
            BeamAssembler::emit_is_boxed(Fail, Src);
        });
    }

    void emit_is_boxed(Label Fail, const ArgVal &Arg, ppc::Gp Src) {
        if (always_one_of<BeamTypeId::AlwaysBoxed>(Arg)) {
            comment("skipped box test since argument is always boxed");
            return;
        }

        preserve_cache([&]() {
            BeamAssembler::emit_is_boxed(Fail, Src);
        });
    }

#ifdef DEBUG
    void emit_tuple_assertion(const ArgSource &Src, ppc::Gp tuple_reg);
#endif

    void emit_gc_test(const ArgWord &Stack,
                      const ArgWord &Heap,
                      const ArgWord &Live);
    void emit_gc_test_preserve(const ArgWord &Need,
                               const ArgWord &Live,
                               const ArgSource &Preserve,
                               ppc::Gp preserve_reg);

    void emit_i_bif(const ArgLabel &Fail,
                    const ArgWord &Bif,
                    const ArgRegister &Dst);

    void ubif_comment(const ArgWord &Bif);

    /* Returns a vector of the untagged and rebased `args`. The adjusted
     * `comparand` is stored in ARG1. */
    const std::vector<ArgVal> emit_select_untag(const ArgSource &Src,
                                                const Span<const ArgVal> &args,
                                                ppc::Gp comparand,
                                                Label fail,
                                                UWord base,
                                                int shift);

    /* The number of values at which a select instruction stops using
     * conditional branches to reach its labels, see
     * emit_select_branch(). */
    static constexpr size_t SELECT_FAR_BRANCH_COUNT = 64;

    void emit_select_branch(const Imm &cr_bit, const ArgVal &label, bool far);

    void emit_linear_search(ppc::Gp val,
                            Label fail,
                            const Span<const ArgVal> &args,
                            bool far = false);

    void emit_binsearch_nodes(ppc::Gp reg,
                              size_t Left,
                              size_t Right,
                              Label fail,
                              const Span<const ArgVal> &args);

    void emit_optimized_two_way_select(ppc::Gp reg,
                                       const ArgVal &value1,
                                       const ArgVal &value2,
                                       const ArgVal &label,
                                       bool far = false);

    void emit_error(int code);
    void emit_error(int reason, const ArgSource &Src);

    int emit_bs_get_field_size(const ArgSource &Size,
                               int unit,
                               Label Fail,
                               const ppc::Gp &out);

    void emit_bs_get_small(const Label &fail,
                           const ArgRegister &Ctx,
                           const ArgWord &Live,
                           const ArgSource &Sz,
                           Uint unit,
                           Uint flags);

    void emit_bs_get_any_int(const Label &fail,
                             const ArgRegister &Ctx,
                             const ArgWord &Live,
                             const ArgSource &Sz,
                             Uint unit,
                             Uint flags);

    void emit_bs_get_binary(const ArgWord heap_need,
                            const ArgRegister &Ctx,
                            const ArgLabel &Fail,
                            const ArgWord &Live,
                            const ArgSource &Size,
                            const ArgWord &Unit,
                            const ArgRegister &Dst);

    void emit_bs_skip_bits(const ArgLabel &Fail, const ArgRegister &Ctx);

    void emit_add_sub_types(bool is_small_result,
                            bool is_sub,
                            const ArgSource &LHS,
                            const ppc::Gp lhs_reg,
                            const ArgSource &RHS,
                            const ppc::Gp rhs_reg,
                            const Label next);

    void emit_cmp_immed_to_bool(const Imm &cr_bit,
                                bool value_if_set,
                                const ArgSource &LHS,
                                const ArgSource &RHS,
                                const ArgRegister &Dst);

    void emit_cond_to_bool(const Imm &cr_bit,
                           bool value_if_set,
                           const ArgRegister &Dst);
    void emit_bif_is_ge_lt(bool is_lt,
                           const ArgSource &LHS,
                           const ArgSource &RHS,
                           const ArgRegister &Dst);
    void emit_bif_min_max(bool is_max,
                          const ArgSource &LHS,
                          const ArgSource &RHS,
                          const ArgRegister &Dst);
    void emit_are_both_booleans(const ArgSource &Src1,
                                const ppc::Gp src1_reg,
                                const ArgSource &Src2,
                                const ppc::Gp src2_reg);

    void emit_cmp_branch_if_smalls(const ArgSource &LHS,
                                   const ppc::Gp lhs_reg,
                                   const ArgSource &RHS,
                                   const ppc::Gp rhs_reg,
                                   const Label smalls);

    void emit_bif_bit_size_inline(const ArgLabel &Fail,
                                  const ArgSource &Src,
                                  const ppc::Gp src_reg);

    void emit_div_rem_literal(Sint divisor,
                              const ArgSource &Dividend,
                              ppc::Gp dividend,
                              ppc::Gp quotient,
                              ppc::Gp remainder,
                              const Label &generic,
                              bool need_div,
                              bool need_rem);

    void emit_div_rem(const ArgLabel &Fail,
                      const ArgSource &LHS,
                      const ArgSource &RHS,
                      const ErtsCodeMFA *error_mfa,
                      const ArgRegister &Quotient,
                      const ArgRegister &Remainder,
                      bool need_div,
                      bool need_rem);

    void emit_float_instr(uint32_t instId,
                          const ArgFRegister &LHS,
                          const ArgFRegister &RHS,
                          const ArgFRegister &Dst);

    void emit_are_both_small(const ArgSource &LHS,
                             const ppc::Gp lhs_reg,
                             const ArgSource &RHS,
                             const ppc::Gp rhs_reg,
                             const Label next);

    void emit_copy_words(ppc::Gp from,
                         ssize_t from_offset,
                         ppc::Gp to,
                         ssize_t to_offset,
                         size_t count);

    void emit_update_any_record(const ArgAtom &Hint,
                                const size_t size_on_heap,
                                const ArgSource &Src,
                                const ArgRegister &Dst,
                                const ArgWord &UpdateCount,
                                const Span<const ArgVal> &updates);

    void emit_read_bits(Uint bits,
                        const ppc::Gp bin_base,
                        const ppc::Gp bin_offset,
                        const ppc::Gp bitdata);

    void emit_bs_get_utf8(const ArgRegister &Ctx, const ArgLabel &Fail);
    void emit_bs_get_utf16(const ArgRegister &Ctx,
                           const ArgLabel &Fail,
                           const ArgWord &Flags);
    void emit_validate_unicode(Label next, Label fail, ppc::Gp value);
    void update_bin_state(ppc::Gp bin_offset,
                          Sint bit_offset,
                          Sint size,
                          ppc::Gp size_reg);
    void set_zero(Sint effectiveSize);
    void emit_construct_utf8(const ArgVal &Src,
                             Sint bit_offset,
                             bool is_byte_aligned);
    void emit_store_bytes(ppc::Gp data,
                          bool big_endian,
                          ppc::Gp base,
                          int first_byte,
                          int num_bytes);

    void emit_reverse_bytes(const ppc::Gp &dst, const ppc::Gp &src, Uint bits);

    void emit_extract_integer(const ppc::Gp &bitdata,
                              Uint flags,
                              Uint position,
                              Uint bits,
                              const ArgRegister &Dst);

    void emit_extract_bitstring(const ppc::Gp bitdata,
                                Uint position,
                                Uint bits,
                                const ArgRegister &Dst);

    void emit_validate(const ArgWord &Arity);

    ppc::Mem emit_variable_apply(bool includeI);
    ppc::Mem emit_fixed_apply(const ArgWord &arity, bool includeI);

    ppc::Gp emit_call_fun(bool skip_box_test = false,
                          bool skip_header_test = false);

    void emit_return_do(bool set_I);
    void emit_dispatch_return(bool set_I);

    void emit_raise_exception();
    void emit_raise_exception(const ErtsCodeMFA *exp);
    void emit_raise_exception(Label I, const ErtsCodeMFA *exp);

    void emit_proc_lc_unrequire(void);
    void emit_proc_lc_require(void);

    void emit_nyi(const char *msg);
    void emit_nyi(void);

#include "beamasm_protos.h"

    /* Resolves a BEAM label.
     *
     * When the branch type is not `dispUnknown`, this must be used
     * _IMMEDIATELY BEFORE_ the instruction that the label is used in. */
    const Label &resolve_beam_label(const ArgLabel &Label,
                                    enum Displacement disp);
    const Label &resolve_label(const Label &target,
                               enum Displacement disp,
                               const char *name = nullptr);

    /* Resolves a shared fragment, creating a trampoline that loads the
     * appropriate address before jumping there.
     *
     * When the branch type is not `dispUnknown`, this must be used
     * _IMMEDIATELY BEFORE_ the instruction that the label is used in. */
    const Label &resolve_fragment(void (*fragment)(), enum Displacement disp);

    /* Embeds a constant argument and returns a label for where it is stored.
     * All kinds of constants are accepted, including labels and export
     * entries.
     *
     * The constant is addressed relative to the instruction that loads it,
     * and that instruction needs the address of itself in a register. The
     * label must therefore be used by an `lnia` instruction followed by a
     * load, as `load_constant` does, and this must be used _IMMEDIATELY
     * BEFORE_ that pair of instructions. */
    Label embed_constant(const ArgVal &value, enum Displacement disp);

    /* Convenience wrapper for embedding raw pointers or immediates. */
    template<typename T,
             std::enable_if_t<std::is_integral<T>::value ||
                                      std::is_pointer<T>::value,
                              bool> = true>
    Label embed_constant(T data, enum Displacement disp) {
        return embed_constant(ArgWord((UWord)data), disp);
    }

    /* Loads an embedded constant into `to`. */
    void load_constant(ppc::Gp to, const ArgVal &value) {
        ASSERT(to != LR_TMP);

        Label anchor = embed_constant(value, disp32K);

        a.lnia(to);
        a.ld(to, to, anchor);
    }

    template<typename T,
             std::enable_if_t<std::is_integral<T>::value ||
                                      std::is_pointer<T>::value,
                              bool> = true>
    void load_constant(ppc::Gp to, T data) {
        load_constant(to, ArgWord((UWord)data));
    }

    /* Binds a label and all related veneers that are within reach of it. */
    void bind_veneer_target(const Label &target);

    void emit_constant(const Constant &constant);
    void emit_veneer(const Veneer &veneer);

    /* Unconditionally emits all veneers and constants that are due within
     * `range` bytes. */
    void flush_pending_stubs(size_t range);

    /* Emits pending veneers when appropriate. Must be called at least once
     * every `STUB_CHECK_INTERVAL` bytes for veneers and constants to work. */
    void check_pending_stubs();

    /* Unconditionally emits all pending labels. Must only be called when
     * the current code position is unreachable. */
    void flush_pending_labels();

    /* Move past the `last_error_offset` if necessary for the next instruction
     * to be properly aligned (e.g. for line mappings). */
    void flush_last_error();

    /* Calls the given shared fragment, ensuring that the redzone is unused and
     * that the return address forms a valid CP. */
    template<typename Any>
    void fragment_call(Any target) {
        emit_assert_redzone_unused();

        a.bl(resolve_fragment(reinterpret_cast<void (*)()>(target), disp32MB));
    }

    /* Calls a C function through the dispatch table, which loads the address
     * of the function into SUPER_TMP as the callee expects. */
    template<typename T, T Func>
    void runtime_call() {
        a.bl(resolve_fragment(reinterpret_cast<void (*)()>(Func), disp32MB));
    }

    bool isRegisterBacked(const ArgVal &arg) {
        if (arg.isXRegister()) {
            return arg.as<ArgXRegister>().get() < num_register_backed_xregs;
        } else if (arg.isFRegister()) {
            return arg.as<ArgFRegister>().get() < num_register_backed_fregs;
        }

        return false;
    }

    template<typename RegType = ppc::Gp>
    struct Variable {
        RegType reg;
        ppc::Mem mem;

        Variable(RegType _r) : Variable(_r, ppc::Mem()) {
        }
        Variable(RegType _r, ppc::Mem _mem) : reg(_r), mem(_mem) {
        }
    };

    Variable<ppc::Gp> init_destination(const ArgVal &arg, ppc::Gp tmp) {
        if (isRegisterBacked(arg)) {
            auto index = arg.as<ArgXRegister>().get();
            return Variable(register_backed_xregs[index]);
        } else {
            return Variable(tmp, getArgRef(arg));
        }
    }

    Variable<ppc::Vec> init_destination(const ArgVal &arg, ppc::Vec tmp) {
        if (isRegisterBacked(arg)) {
            auto index = arg.as<ArgFRegister>().get();
            return Variable(register_backed_fregs[index]);
        } else {
            return Variable(tmp, getArgRef(arg));
        }
    }

    Variable<ppc::Gp> load_source(const ArgVal &arg, ppc::Gp tmp) {
        /* `tmp` is used as a base register when loading constants. */
        ASSERT(tmp != LR_TMP);

        if (arg.isLiteral()) {
            preserve_cache(
                    [&]() {
                        load_constant(tmp, arg);
                    },
                    tmp);
            return Variable(tmp);
        } else if (arg.isRegister()) {
            if (isRegisterBacked(arg)) {
                auto index = arg.as<ArgXRegister>().get();
                ppc::Gp reg = register_backed_xregs[index];
                reg_cache.invalidate(reg);
                return Variable(reg);
            }

            auto ref = getArgRef(arg);
            ld_cached(tmp, ref);
            return Variable(tmp, ref);
        } else {
            if (arg.isImmed() || arg.isWord()) {
                auto val = arg.isImmed() ? arg.as<ArgImmed>().get()
                                         : arg.as<ArgWord>().get();

                /* Values that fit in 32 bits take at most two instructions,
                 * which is no more than loading them from memory takes. */
                if (Support::is_int_n<32>((Sint64)val)) {
                    preserve_cache(
                            [&]() {
                                mov_imm(tmp, val);
                            },
                            tmp);
                    return Variable(tmp);
                }
            }

            preserve_cache(
                    [&]() {
                        load_constant(tmp, arg);
                    },
                    tmp);
            return Variable(tmp);
        }
    }

    /*
     * Load the argument into ANY register, using the
     * cache to avoid reloading the value.
     *
     * Because it is not possible to predict into which register
     * the value will end up, the only safe way to use this function
     * is when the register is used immediately and only once.
     */
    Variable<ppc::Gp> load_source(const ArgVal &arg) {
        if (!arg.isRegister()) {
            return load_source(arg, TMP1);
        } else {
            ppc::Gp cached_reg = find_cache(getArgRef(arg));

            if (cached_reg.is_valid()) {
                return load_source(arg, cached_reg);
            } else {
                return load_source(arg, TMP1);
            }
        }
    }

    auto load_sources(const ArgVal &Src1,
                      ppc::Gp tmp1,
                      const ArgVal &Src2,
                      ppc::Gp tmp2) {
        return std::make_pair(load_source(Src1, tmp1), load_source(Src2, tmp2));
    }

    Variable<ppc::Vec> load_source(const ArgVal &arg, ppc::Vec tmp) {
        if (isRegisterBacked(arg)) {
            auto index = arg.as<ArgFRegister>().get();
            return Variable<ppc::Vec>(register_backed_fregs[index]);
        }

        a.lfd(tmp, getArgRef(arg));
        return Variable<ppc::Vec>(tmp);
    }

    template<typename Reg>
    void mov_var(const Variable<Reg> &to, const Variable<Reg> &from) {
        mov_var(to.reg, from);
    }

    template<typename Reg>
    void mov_var(const Variable<Reg> &to, Reg from) {
        if (to.reg != from) {
            mov_preserve_cache(to.reg, from);
        }
    }

    template<typename Reg>
    void mov_var(Reg to, const Variable<Reg> &from) {
        if (to != from.reg) {
            mov_preserve_cache(to, from.reg);
        }
    }

    void flush_var(const Variable<ppc::Gp> &to) {
        if (to.mem.has_base()) {
            std_cache(to.reg, to.mem);
        } else {
            reg_cache.invalidate(to.reg);
        }
    }

    void flush_var(const Variable<ppc::Vec> &to) {
        if (to.mem.has_base()) {
            a.stfd(to.reg, to.mem);
        }
    }

    void flush_vars(const Variable<ppc::Gp> &to1,
                    const Variable<ppc::Gp> &to2) {
        flush_var(to1);
        flush_var(to2);
    }

    void flush_vars(const Variable<ppc::Gp> &to1,
                    const Variable<ppc::Gp> &to2,
                    const Variable<ppc::Gp> &to3) {
        flush_var(to1);
        flush_var(to2);
        flush_var(to3);
    }

    void mov_arg(const ArgRegister &To, const ArgVal &From) {
        if (isRegisterBacked(To)) {
            auto to = init_destination(To, SUPER_TMP);
            auto from = load_source(From, to.reg);
            mov_var(to, from);
            flush_var(to);
        } else {
            auto from = load_source(From, SUPER_TMP);
            auto to = init_destination(To, from.reg);
            mov_var(to, from);
            flush_var(to);
        }
    }

    void mov_arg(const ArgRegister &To, ppc::Mem From) {
        auto to = init_destination(To, SUPER_TMP);
        a.ld(to.reg, From);
        flush_var(to);
    }

    void mov_arg(ppc::Mem To, const ArgVal &From) {
        auto from = load_source(From, SUPER_TMP);
        auto to = Variable(from.reg, To);
        flush_var(to);
    }

    void mov_arg(ppc::Gp to, const ArgVal &from) {
        auto r = load_source(from, to);
        if (r.reg != to) {
            mov_preserve_cache(to, r.reg);
        }
    }

    void mov_arg(const ArgVal &to, ppc::Gp from) {
        auto r = init_destination(to, from);
        if (r.reg != from) {
            mov_preserve_cache(r.reg, from);
        }
        flush_var(r);
    }

    /* Unsigned compare of `gp` and `arg`, with the result in `cr`. */
    void cmpl_arg(ppc::Gp gp, const ArgVal &arg, ppc::Cr cr = ppc::cr0) {
        if (arg.isImmed() || arg.isWord()) {
            UWord val = arg.isImmed() ? arg.as<ArgImmed>().get()
                                      : arg.as<ArgWord>().get();
            cmpl(gp, val, cr);
            return;
        }

        auto tmp = load_source(arg, SUPER_TMP);
        a.cmpld(cr, gp, tmp.reg);
    }

    /* Branches to `Unequal` if Reg1 and Reg2 are definitely not equal based
     * on their tags alone. (They may still be equal if both are
     * immediates and all other bits are equal too.) */
    void emit_is_unequal_based_on_tags(Label Unequal,
                                       const ArgVal &Src1,
                                       ppc::Gp Reg1,
                                       const ArgVal &Src2,
                                       ppc::Gp Reg2) {
        ERTS_CT_ASSERT(TAG_PRIMARY_IMMED1 == _TAG_PRIMARY_MASK);
        ERTS_CT_ASSERT((TAG_PRIMARY_LIST | TAG_PRIMARY_BOXED) ==
                       TAG_PRIMARY_IMMED1);

        if (always_one_of<BeamTypeId::AlwaysBoxed>(Src1)) {
            emit_is_boxed(Unequal, Reg2);
        } else if (always_one_of<BeamTypeId::AlwaysBoxed>(Src2)) {
            emit_is_boxed(Unequal, Reg1);
        } else if (exact_type<BeamTypeId::Cons>(Src1)) {
            emit_is_cons(Unequal, Reg2);
        } else if (exact_type<BeamTypeId::Cons>(Src2)) {
            emit_is_cons(Unequal, Reg1);
        } else {
            a.or_(SUPER_TMP, Reg1, Reg2);

            if (never_one_of<BeamTypeId::Cons>(Src1) ||
                never_one_of<BeamTypeId::Cons>(Src2)) {
                emit_is_boxed(Unequal, SUPER_TMP);
            } else if (never_one_of<BeamTypeId::AlwaysBoxed>(Src1) ||
                       never_one_of<BeamTypeId::AlwaysBoxed>(Src2)) {
                emit_is_cons(Unequal, SUPER_TMP);
            } else {
                a.andi_rc(SUPER_TMP, SUPER_TMP, imm(_TAG_PRIMARY_MASK));
                cmp(SUPER_TMP, TAG_PRIMARY_IMMED1);

                /*
                 * SUPER_TMP will now be TAG_PRIMARY_IMMED1 if either
                 * one or both registers are immediates, or if one
                 * register is a list and the other a boxed.
                 */
                a.beq(Unequal);
            }
        }
    }

    /* Sets the EQ bit of CR0 if both sources are immediates. Clobbers
     * SUPER_TMP. */
    void emit_are_both_immediate(const ArgVal &Src1,
                                 ppc::Gp Reg1,
                                 const ArgVal &Src2,
                                 ppc::Gp Reg2) {
        ERTS_CT_ASSERT(TAG_PRIMARY_IMMED1 == _TAG_PRIMARY_MASK);
        if (always_immediate(Src1)) {
            a.andi_rc(SUPER_TMP, Reg2, imm(_TAG_PRIMARY_MASK));
        } else if (always_immediate(Src2)) {
            a.andi_rc(SUPER_TMP, Reg1, imm(_TAG_PRIMARY_MASK));
        } else {
            a.and_(SUPER_TMP, Reg1, Reg2);
            a.andi_rc(SUPER_TMP, SUPER_TMP, imm(_TAG_PRIMARY_MASK));
        }
        a.cmpdi(SUPER_TMP, imm(TAG_PRIMARY_IMMED1));
    }

    /* Signed compare of `gp` and `arg`, with the result in `cr`. */
    void cmp_arg(ppc::Gp gp, const ArgVal &arg, ppc::Cr cr = ppc::cr0) {
        if (arg.isImmed() || arg.isWord()) {
            Sint val = arg.isImmed() ? arg.as<ArgImmed>().get()
                                     : arg.as<ArgWord>().get();
            cmp(gp, val, cr);
            return;
        }

        auto tmp = load_source(arg, SUPER_TMP);
        a.cmpd(cr, gp, tmp.reg);
    }
};

void *beamasm_metadata_insert(std::string module_name,
                              ErtsCodePtr base_address,
                              size_t code_size,
                              const AsmMetadata &metadata);
void beamasm_metadata_early_init();
void beamasm_metadata_late_init();
