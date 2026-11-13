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

    /* Loads a 64-bit value from `mem`, which is either a base register with a
     * displacement that is a multiple of 4, or a base and an index
     * register. */
    void load_mem(ppc::Gp to, ppc::Mem mem) {
        if (mem.has_index()) {
            a.ldx(to, mem);
        } else {
            a.ld(to, mem);
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
