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

#include <algorithm>
#include <numeric>
#include "beam_asm.hpp"

extern "C"
{
#include "erl_bif_table.h"
#include "big.h"
#include "beam_catches.h"
#include "beam_common.h"
#include "code_ix.h"
#include "erl_binary.h"
#include "erl_map.h"
}

using namespace asmjit;

/* Helpers */

void BeamModuleAssembler::emit_error(int reason) {
    mov_imm(TMP1, reason);
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
    emit_raise_exception();
}

void BeamModuleAssembler::emit_error(int reason, const ArgSource &Src) {
    auto src = load_source(Src, TMP2);

    mov_imm(TMP1, reason);
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
    a.std(src.reg, ppc::Mem(c_p, offsetof(Process, fvalue)));
    emit_raise_exception();
}

void BeamModuleAssembler::emit_gc_test_preserve(const ArgWord &Need,
                                                const ArgWord &Live,
                                                const ArgSource &Preserve,
                                                ppc::Gp preserve_reg) {
    const int32_t bytes_needed = (Need.get() + S_RESERVED) * sizeof(Eterm);
    Label after_gc_check = a.new_label();

#ifdef DEBUG
    comment("(debug: fill dead X registers with garbage)");
    const ppc::Gp garbage_reg = preserve_reg == ARG4 ? ARG3 : ARG4;
    mov_imm(garbage_reg, ERTS_HOLE_MARKER);
    if (!(Preserve.isXRegister() &&
          Preserve.as<ArgXRegister>().get() >= Live.get())) {
        mov_arg(ArgXRegister(Live.get()), garbage_reg);
        mov_arg(ArgXRegister(Live.get() + 1), garbage_reg);
    } else {
        mov_imm(garbage_reg, ERTS_HOLE_MARKER);
        mov_arg(ArgXRegister(Live.get() + 1), garbage_reg);
        mov_arg(ArgXRegister(Live.get() + 2), garbage_reg);
    }
#endif

    add(ARG3, HTOP, bytes_needed);
    a.cmpld(ARG3, E);
    a.ble(after_gc_check);

    ASSERT(Live.get() < ERTS_X_REGS_ALLOCATED);

    /* We don't need to stash the preserved term if it's currently live, making
     * the code slightly shorter. */
    if (!(Preserve.isXRegister() &&
          Preserve.as<ArgXRegister>().get() >= Live.get())) {
        mov_imm(ARG4, Live.get());
        fragment_call(ga->get_garbage_collect());
        mov_arg(preserve_reg, Preserve);
    } else {
        mov_arg(ArgXRegister(Live.get()), preserve_reg);

        mov_imm(ARG4, Live.get() + 1);
        fragment_call(ga->get_garbage_collect());

        mov_arg(preserve_reg, ArgXRegister(Live.get()));
    }

    a.bind(after_gc_check);
}

void BeamModuleAssembler::emit_gc_test(const ArgWord &Ns,
                                       const ArgWord &Nh,
                                       const ArgWord &Live) {
    int32_t bytes_needed = (Ns.get() + Nh.get() + S_RESERVED) * sizeof(Eterm);
    Label after_gc_check = a.new_label();

#ifdef DEBUG
    comment("(debug: fill dead X registers with garbage)");
    mov_imm(ARG4, ERTS_HOLE_MARKER);
    mov_arg(ArgXRegister(Live.get()), ARG4);
    mov_arg(ArgXRegister(Live.get() + 1), ARG4);
#endif

    add(ARG3, HTOP, bytes_needed);
    a.cmpld(ARG3, E);
    a.ble(after_gc_check);

    mov_imm(ARG4, Live.get());
    fragment_call(ga->get_garbage_collect());

    a.bind(after_gc_check);
}

void BeamModuleAssembler::emit_validate(const ArgWord &Arity) {
#ifdef DEBUG
    Label next = a.new_label(), crash = a.new_label();

    /* Crash if the Erlang heap is not word-aligned */
    a.andi_rc(LR_TMP, HTOP, imm(sizeof(Eterm) - 1));
    a.bne(crash);

    /* Crash if the Erlang stack is not word-aligned */
    a.andi_rc(LR_TMP, E, imm(sizeof(Eterm) - 1));
    a.bne(crash);

    /* Crash if we've overrun the stack */
    lea(TMP1, ppc::Mem(E, -(int32_t)(S_REDZONE * sizeof(Eterm))));
    a.cmpld(HTOP, TMP1);
    a.bgt(crash);

    a.b(next);

    a.bind(crash);
    udf(0xbad);
    a.bind(next);
#endif
}

/* Instrs */

void BeamModuleAssembler::emit_i_validate(const ArgWord &Arity) {
    emit_validate(Arity);
}

void BeamModuleAssembler::emit_allocate_heap(const ArgWord &NeedStack,
                                             const ArgWord &NeedHeap,
                                             const ArgWord &Live) {
    ASSERT(NeedStack.get() <= MAX_REG);

    emit_gc_test(NeedStack, NeedHeap, Live);

    if (NeedStack.get() > 0) {
        sub(E, E, NeedStack.get() * sizeof(Eterm));
    }
}

void BeamModuleAssembler::emit_allocate(const ArgWord &NeedStack,
                                        const ArgWord &Live) {
    emit_allocate_heap(NeedStack, ArgWord(0), Live);
}

void BeamModuleAssembler::emit_deallocate(const ArgWord &Deallocate) {
    ASSERT(Deallocate.get() <= 1023);
    ASSERT(erts_frame_layout == ERTS_FRAME_LAYOUT_RA);

    if (Deallocate.get() > 0) {
        add(E, E, Deallocate.get() * sizeof(Eterm));
    }
}

void BeamModuleAssembler::emit_test_heap(const ArgWord &Nh,
                                         const ArgWord &Live) {
    emit_gc_test(ArgWord(0), Nh, Live);
}

void BeamModuleAssembler::emit_normal_exit() {
    /* This is implicitly global; it does not normally appear in modules and
     * doesn't require size optimization. */

    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();
    emit_proc_lc_unrequire();

    mov_imm(TMP1, EXC_NORMAL);
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
    a.li(LR_TMP, imm(0));
    a.stb(LR_TMP, ppc::Mem(c_p, offsetof(Process, arity)));
    a.mr(ARG1, c_p);
    mov_imm(ARG2, am_normal);
    runtime_call<void (*)(Process *, Eterm), erts_do_exit_process>();

    emit_proc_lc_require();
    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    a.b(resolve_fragment(ga->get_do_schedule(), disp32MB));
}

void BeamModuleAssembler::emit_continue_exit() {
    /* This is implicitly global; it does not normally appear in modules and
     * doesn't require size optimization. */

    emit_enter_runtime<Update::eReductions | Update::eHeapAlloc>(0);
    emit_proc_lc_unrequire();

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *), erts_continue_exit_process>();

    emit_proc_lc_require();
    emit_leave_runtime<Update::eReductions | Update::eHeapAlloc>(0);

    a.b(resolve_fragment(ga->get_do_schedule(), disp32MB));
}

/* The reduction test of a function consists of the two instructions that
 * load the address of the function, the store that only exists when
 * allocation tags are enabled, the decrement, and the branch. */
#define TEST_YIELD_RETURN_OFFSET                                               \
    (BEAM_ASM_FUNC_PROLOGUE_SIZE + sizeof(Uint32[4]) +                         \
     (erts_alcu_enable_code_atags ? sizeof(Uint32) : 0))

/* ARG3 = current_label */
void BeamGlobalAssembler::emit_i_test_yield_shared() {
    sub(ARG2, ARG3, sizeof(ErtsCodeMFA));
    add(ARG3, ARG3, TEST_YIELD_RETURN_OFFSET);

    a.std(ARG2, ppc::Mem(c_p, offsetof(Process, current)));
    a.ld(ARG2, ppc::Mem(ARG2, offsetof(ErtsCodeMFA, arity)));
    a.stb(ARG2, ppc::Mem(c_p, offsetof(Process, arity)));

    a.b(labels[context_switch_simplified]);
}

void BeamModuleAssembler::emit_i_test_yield() {
    /* When present, this is guaranteed to be the first instruction after the
     * breakpoint trampoline. */
    ASSERT((a.offset() - code.label_offset_from_base(current_label)) ==
           BEAM_ASM_FUNC_PROLOGUE_SIZE);

    emit_adr(ARG3, current_label);

    if (erts_alcu_enable_code_atags) {
        /* The point-of-origin allocation tags are vastly improved when the
         * instruction pointer is updated frequently. This has a relatively low
         * impact on performance but there's little point in doing this unless
         * the user has requested it -- it's an undocumented feature for
         * now. */
        a.std(ARG3, ppc::Mem(c_p, offsetof(Process, i)));
    }

    /* The record form of the instruction compares the result with zero. */
    a.addic_rc(FCALLS, FCALLS, imm(-1));
    a.ble(resolve_fragment(ga->get_i_test_yield_shared(), disp32K));

    ASSERT((a.offset() - code.label_offset_from_base(current_label)) ==
           TEST_YIELD_RETURN_OFFSET);
}

void BeamModuleAssembler::emit_i_yield() {
    mov_imm(XREG0, am_true);
    fragment_call(ga->get_dispatch_return());
}
