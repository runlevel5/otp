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

/*
 * Guard BIF calls using the generic bif1, bif2, and bif3 instructions
 * are expensive. Not only are there two indirect calls (one to the
 * fragment, one to the BIF itself), but the caller-saved X registers
 * must also be saved and restored, and the BIF operands that are
 * usually in CPU registers must be written out to memory.
 *
 * Therefore, guard BIFs that are used fairly frequently and can
 * be implemented entirely in assembly language without any calls to
 * C function are implemented in this source file.
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
#include "erl_map.h"
}

using namespace asmjit;

/* Raise a badarg exception for the given MFA. */
void BeamGlobalAssembler::emit_raise_badarg(const ErtsCodeMFA *mfa) {
    mov_imm(TMP1, BADARG);
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
    mov_imm(ARG4, mfa);
    a.b(labels[raise_exception]);
}

/* ================================================================
 *  '=:='/2
 *  '=/='/2
 *  '>='/2
 *  '<'/2
 * ================================================================
 */

void BeamGlobalAssembler::emit_bif_is_eq_exact_shared() {
    Label succ = a.new_label(), fail = a.new_label();

    a.cmpd(ARG1, ARG2);
    a.beq(succ);

    /* The terms could still be equal if both operands are pointers
     * having the same tag. */
    emit_is_unequal_based_on_tags(ARG1, ARG2);
    a.beq(fail);

    emit_enter_runtime_frame();
    emit_enter_runtime();

    runtime_call<int (*)(Eterm, Eterm), eq>();

    emit_leave_runtime();
    emit_leave_runtime_frame();

    /* Note: eq() returns int, not Sint. */
    a.cmpwi(ARG1, imm(0));
    a.beq(fail);

    a.bind(succ);
    {
        mov_imm(ARG1, am_true);
        a.blr();
    }

    a.bind(fail);
    {
        mov_imm(ARG1, am_false);
        a.blr();
    }
}

void BeamGlobalAssembler::emit_bif_is_ne_exact_shared() {
    Label succ = a.new_label(), fail = a.new_label();

    a.cmpd(ARG1, ARG2);
    a.beq(fail);

    emit_is_unequal_based_on_tags(ARG1, ARG2);
    a.beq(succ);

    emit_enter_runtime_frame();
    emit_enter_runtime();

    runtime_call<int (*)(Eterm, Eterm), eq>();

    emit_leave_runtime();
    emit_leave_runtime_frame();

    /* Note: eq() returns int, not Sint. */
    a.cmpwi(ARG1, imm(0));
    a.bne(fail);

    a.bind(succ);
    {
        mov_imm(ARG1, am_true);
        a.blr();
    }

    a.bind(fail);
    {
        mov_imm(ARG1, am_false);
        a.blr();
    }
}

/* Stores `value_if_set` in Dst if the given condition register bit is
 * set, and its negation otherwise. */
void BeamModuleAssembler::emit_cond_to_bool(const Imm &cr_bit,
                                            bool value_if_set,
                                            const ArgRegister &Dst) {
    auto dst = init_destination(Dst, TMP2);

    mov_imm(TMP3, am_true);
    mov_imm(TMP4, am_false);
    if (value_if_set) {
        a.isel(dst.reg, TMP3, TMP4, cr_bit);
    } else {
        a.isel(dst.reg, TMP4, TMP3, cr_bit);
    }
    flush_var(dst);
}

void BeamModuleAssembler::emit_cmp_immed_to_bool(const Imm &cr_bit,
                                                 bool value_if_set,
                                                 const ArgSource &LHS,
                                                 const ArgSource &RHS,
                                                 const ArgRegister &Dst) {
    if (RHS.isImmed()) {
        auto lhs = load_source(LHS);
        cmp_arg(lhs.reg, RHS);
    } else {
        auto [lhs, rhs] = load_sources(LHS, TMP1, RHS, TMP2);
        a.cmpd(lhs.reg, rhs.reg);
    }
    emit_cond_to_bool(cr_bit, value_if_set, Dst);
}

void BeamModuleAssembler::emit_bif_is_eq_exact(const ArgRegister &LHS,
                                               const ArgSource &RHS,
                                               const ArgRegister &Dst) {
    if (always_immediate(LHS) || always_immediate(RHS)) {
        if (!LHS.isImmed() && !RHS.isImmed()) {
            comment("simplified check since one argument is an immediate");
        }
        emit_cmp_immed_to_bool(ppc::cr0.eq(), true, LHS, RHS, Dst);
    } else {
        auto [lhs, rhs] = load_sources(LHS, ARG1, RHS, ARG2);
        auto dst = init_destination(Dst, ARG1);

        mov_var(ARG1, lhs);
        mov_var(ARG2, rhs);
        fragment_call(ga->get_bif_is_eq_exact_shared());
        mov_var(dst, ARG1);
        flush_var(dst);
    }
}

void BeamModuleAssembler::emit_bif_is_ne_exact(const ArgRegister &LHS,
                                               const ArgSource &RHS,
                                               const ArgRegister &Dst) {
    if (always_immediate(LHS) || always_immediate(RHS)) {
        if (!LHS.isImmed() && !RHS.isImmed()) {
            comment("simplified check since one argument is an immediate");
        }
        emit_cmp_immed_to_bool(ppc::cr0.eq(), false, LHS, RHS, Dst);
    } else {
        auto [lhs, rhs] = load_sources(LHS, ARG1, RHS, ARG2);
        auto dst = init_destination(Dst, ARG1);

        mov_var(ARG1, lhs);
        mov_var(ARG2, rhs);
        fragment_call(ga->get_bif_is_ne_exact_shared());
        mov_var(dst, ARG1);
        flush_var(dst);
    }
}

void BeamModuleAssembler::emit_bif_is_ge_lt(bool is_lt,
                                            const ArgSource &LHS,
                                            const ArgSource &RHS,
                                            const ArgRegister &Dst) {
    auto [lhs, rhs] = load_sources(LHS, ARG1, RHS, ARG2);

    Label generic = a.new_label(), next = a.new_label();

    if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(LHS) &&
        always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(RHS)) {
        /* The only possible kind of immediate is a small and all
         * other values are boxed, so we can test for smalls by
         * testing boxed. */
        comment("simplified small test since all other types are boxed");
        if (always_small(LHS)) {
            emit_is_not_boxed(generic, rhs.reg);
        } else if (always_small(RHS)) {
            emit_is_not_boxed(generic, lhs.reg);
        } else {
            a.and_(TMP1, lhs.reg, rhs.reg);
            emit_is_not_boxed(generic, TMP1);
        }
    } else {
        /* Relative comparisons are overwhelmingly likely to be used
         * on smalls, so we'll specialize those and keep the rest in a
         * shared fragment. */
        if (always_small(RHS)) {
            a.andi_rc(TMP1, lhs.reg, imm(_TAG_IMMED1_MASK));
        } else if (always_small(LHS)) {
            a.andi_rc(TMP1, rhs.reg, imm(_TAG_IMMED1_MASK));
        } else {
            ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
            a.and_(TMP1, lhs.reg, rhs.reg);
            a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
        }

        a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
        a.bne(generic);
    }

    a.cmpd(lhs.reg, rhs.reg);
    a.b(next);

    a.bind(generic);
    {
        a.cmpd(lhs.reg, rhs.reg);
        a.beq(next);

        mov_var(ARG1, lhs);
        mov_var(ARG2, rhs);
        fragment_call(ga->get_arith_compare_shared());
    }

    a.bind(next);
    emit_cond_to_bool(ppc::cr0.lt(), is_lt, Dst);
}

void BeamModuleAssembler::emit_bif_is_ge(const ArgSource &LHS,
                                         const ArgSource &RHS,
                                         const ArgRegister &Dst) {
    if (always_small(LHS) && RHS.isSmall() && RHS.isImmed()) {
        auto lhs = load_source(LHS, ARG1);

        comment("simplified compare because one operand is an immediate small");
        cmp(lhs.reg, RHS.as<ArgImmed>().get());
        emit_cond_to_bool(ppc::cr0.lt(), false, Dst);

        return;
    } else if (LHS.isSmall() && LHS.isImmed() && always_small(RHS)) {
        auto rhs = load_source(RHS, ARG1);

        comment("simplified compare because one operand is an immediate small");
        cmp(rhs.reg, LHS.as<ArgImmed>().get());
        emit_cond_to_bool(ppc::cr0.gt(), false, Dst);

        return;
    }

    emit_bif_is_ge_lt(false, LHS, RHS, Dst);
}

void BeamModuleAssembler::emit_bif_is_lt(const ArgSource &LHS,
                                         const ArgSource &RHS,
                                         const ArgRegister &Dst) {
    emit_bif_is_ge_lt(true, LHS, RHS, Dst);
}

/* ================================================================
 *  and/2
 * ================================================================
 */

void BeamGlobalAssembler::emit_handle_and_error() {
    static ErtsCodeMFA mfa = {am_erlang, am_and, 2};
    emit_raise_badarg(&mfa);
}

/* Sets the EQ bit of CR0 if both sources are booleans.
 *
 * Clobbers TMP3 and TMP4. */
void BeamModuleAssembler::emit_are_both_booleans(const ArgSource &Src1,
                                                 const ppc::Gp src1_reg,
                                                 const ArgSource &Src2,
                                                 const ppc::Gp src2_reg) {
    static const Uint diff_bit = am_true - am_false;
    const int diff_bit_number = Support::ctz(diff_bit);

    ERTS_CT_ASSERT(am_false == make_atom(0));
    ERTS_CT_ASSERT(am_true == make_atom(1));

    if (exact_type<BeamTypeId::Atom>(Src1) &&
        exact_type<BeamTypeId::Atom>(Src2)) {
        comment("simplified type check because operands are atoms");
        a.or_(TMP3, src1_reg, src2_reg);
        a.rldicl_rc(LR_TMP,
                    TMP3,
                    imm(64 - (_TAG_IMMED2_SIZE + 1)),
                    imm(_TAG_IMMED2_SIZE + 1));
    } else {
        /* Exclusive or with the tag of an atom leaves nothing but
         * possibly `diff_bit` if the term is a boolean. Rotate that
         * bit to the top and test all the other bits. */
        a.xori(TMP3, src1_reg, imm(_TAG_IMMED2_ATOM));
        a.xori(TMP4, src2_reg, imm(_TAG_IMMED2_ATOM));
        a.or_(TMP3, TMP3, TMP4);
        a.rldicl_rc(LR_TMP, TMP3, imm(63 - diff_bit_number), imm(1));
    }
}

void BeamModuleAssembler::emit_bif_and(const ArgLabel &Fail,
                                       const ArgSource &Src1,
                                       const ArgSource &Src2,
                                       const ArgRegister &Dst) {
    Label next = a.new_label();

    auto [src1, src2] = load_sources(Src1, TMP1, Src2, TMP2);
    auto dst = init_destination(Dst, TMP3);

    emit_are_both_booleans(Src1, src1.reg, Src2, src2.reg);

    if (Fail.get()) {
        a.bne(resolve_beam_label(Fail, disp32K));
    } else {
        a.beq(next);
        mov_var(XREG0, src1);
        mov_var(XREG1, src2);
        fragment_call(ga->get_handle_and_error());
    }

    a.bind(next);
    {
        a.and_(dst.reg, src1.reg, src2.reg);
        flush_var(dst);
    }
}

/* ================================================================
 *  hd/1
 * ================================================================
 */

void BeamGlobalAssembler::emit_handle_hd_error() {
    static ErtsCodeMFA mfa = {am_erlang, am_hd, 1};
    emit_raise_badarg(&mfa);
}

void BeamModuleAssembler::emit_bif_hd(const ArgSource &Src,
                                      const ArgRegister &Hd) {
    Label good_cons = a.new_label();
    auto src = load_source(Src, TMP1);
    auto hd = init_destination(Hd, TMP2);
    const int bitNumber = 1;

    ERTS_CT_ASSERT(_TAG_PRIMARY_MASK - TAG_PRIMARY_LIST == (1 << bitNumber));

    a.andi_rc(LR_TMP, src.reg, imm(1 << bitNumber));
    a.beq(good_cons);
    mov_var(XREG0, src);
    fragment_call(ga->get_handle_hd_error());

    a.bind(good_cons);
    {
        ppc::Gp cons_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(hd.reg, getCARRef(cons_ptr));
        flush_var(hd);
    }
}

/* ================================================================
 *  min/2
 *  max/2
 * ================================================================
 */

void BeamModuleAssembler::emit_bif_min_max(bool is_max,
                                           const ArgSource &LHS,
                                           const ArgSource &RHS,
                                           const ArgRegister &Dst) {
    auto [lhs, rhs] = load_sources(LHS, ARG1, RHS, ARG2);
    auto dst = init_destination(Dst, ARG1);
    bool both_small = always_small(LHS) && always_small(RHS);
    bool need_generic = !both_small;
    Label generic = a.new_label(), next = a.new_label();

    if (both_small) {
        comment("skipped test for small operands since they are always small");
    } else if (always_small(RHS) &&
               always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       LHS)) {
        comment("simplified test for small operand");
        emit_is_not_boxed(generic, lhs.reg);
    } else if (always_small(LHS) &&
               always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       RHS)) {
        comment("simplified test for small operand");
        emit_is_not_boxed(generic, rhs.reg);
    } else if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       LHS) &&
               always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       RHS)) {
        comment("simplified test for small operands");
        ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
        a.and_(TMP1, lhs.reg, rhs.reg);
        emit_is_not_boxed(generic, TMP1);
    } else {
        if (RHS.isSmall()) {
            a.andi_rc(TMP1, lhs.reg, imm(_TAG_IMMED1_MASK));
        } else if (LHS.isSmall()) {
            a.andi_rc(TMP1, rhs.reg, imm(_TAG_IMMED1_MASK));
        } else {
            /* Avoid the expensive generic comparison for equal terms. */
            a.cmpd(lhs.reg, rhs.reg);
            a.beq(next);

            ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
            a.and_(TMP1, lhs.reg, rhs.reg);
            a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
        }

        a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
        a.bne(generic);
    }

    /* Both arguments are smalls. */
    a.cmpd(lhs.reg, rhs.reg);
    if (need_generic) {
        a.b(next);
    }

    a.bind(generic);
    if (need_generic) {
        mov_var(ARG1, lhs);
        mov_var(ARG2, rhs);
        fragment_call(ga->get_arith_compare_shared());

        /* Reloading the operands leaves CR0 alone. */
        load_sources(LHS, ARG1, RHS, ARG2);
    }

    a.bind(next);
    if (is_max) {
        a.isel(dst.reg, rhs.reg, lhs.reg, ppc::cr0.lt());
    } else {
        a.isel(dst.reg, rhs.reg, lhs.reg, ppc::cr0.gt());
    }
    flush_var(dst);
}

void BeamModuleAssembler::emit_bif_max(const ArgSource &LHS,
                                       const ArgSource &RHS,
                                       const ArgRegister &Dst) {
    emit_bif_min_max(true, LHS, RHS, Dst);
}

void BeamModuleAssembler::emit_bif_min(const ArgSource &LHS,
                                       const ArgSource &RHS,
                                       const ArgRegister &Dst) {
    emit_bif_min_max(false, LHS, RHS, Dst);
}

/* ================================================================
 *  not/1
 * ================================================================
 */

void BeamGlobalAssembler::emit_handle_not_error() {
    static ErtsCodeMFA mfa = {am_erlang, am_not, 1};
    emit_raise_badarg(&mfa);
}

void BeamModuleAssembler::emit_bif_not(const ArgLabel &Fail,
                                       const ArgRegister &Src,
                                       const ArgRegister &Dst) {
    Label next = a.new_label();
    auto src = load_source(Src, TMP1);
    auto dst = init_destination(Dst, TMP2);
    ERTS_CT_ASSERT(am_false == make_atom(0));
    ERTS_CT_ASSERT(am_true == make_atom(1));
    Uint diff_bit = am_true - am_false;

    /* See emit_are_both_booleans(). */
    a.xori(TMP3, src.reg, imm(_TAG_IMMED2_ATOM));
    a.rldicl_rc(LR_TMP, TMP3, imm(63 - Support::ctz(diff_bit)), imm(1));

    if (Fail.get() == 0) {
        a.beq(next);
        mov_var(XREG0, src);
        fragment_call(ga->get_handle_not_error());
    } else {
        a.bne(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
    {
        a.xori(dst.reg, src.reg, imm(diff_bit));
        flush_var(dst);
    }
}

/* ================================================================
 *  or/2
 * ================================================================
 */

void BeamGlobalAssembler::emit_handle_or_error() {
    static ErtsCodeMFA mfa = {am_erlang, am_or, 2};
    emit_raise_badarg(&mfa);
}

void BeamModuleAssembler::emit_bif_or(const ArgLabel &Fail,
                                      const ArgSource &Src1,
                                      const ArgSource &Src2,
                                      const ArgRegister &Dst) {
    Label next = a.new_label();

    auto [src1, src2] = load_sources(Src1, TMP1, Src2, TMP2);
    auto dst = init_destination(Dst, TMP3);

    emit_are_both_booleans(Src1, src1.reg, Src2, src2.reg);

    if (Fail.get()) {
        a.bne(resolve_beam_label(Fail, disp32K));
    } else {
        a.beq(next);
        mov_var(XREG0, src1);
        mov_var(XREG1, src2);
        fragment_call(ga->get_handle_or_error());
    }

    a.bind(next);
    {
        a.or_(dst.reg, src1.reg, src2.reg);
        flush_var(dst);
    }
}

/* ================================================================
 *  tl/1
 * ================================================================
 */

void BeamGlobalAssembler::emit_handle_tl_error() {
    static ErtsCodeMFA mfa = {am_erlang, am_tl, 1};
    emit_raise_badarg(&mfa);
}

void BeamModuleAssembler::emit_bif_tl(const ArgSource &Src,
                                      const ArgRegister &Tl) {
    Label good_cons = a.new_label();
    auto src = load_source(Src, TMP1);
    auto tl = init_destination(Tl, TMP2);
    const int bitNumber = 1;

    ERTS_CT_ASSERT(_TAG_PRIMARY_MASK - TAG_PRIMARY_LIST == (1 << bitNumber));

    a.andi_rc(LR_TMP, src.reg, imm(1 << bitNumber));
    a.beq(good_cons);
    mov_var(XREG0, src);
    fragment_call(ga->get_handle_tl_error());

    a.bind(good_cons);
    {
        ppc::Gp cons_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(tl.reg, getCDRRef(cons_ptr));
        flush_var(tl);
    }
}
