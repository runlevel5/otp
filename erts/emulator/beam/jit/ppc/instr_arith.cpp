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

#include "beam_asm.hpp"

extern "C"
{
#include "erl_bif_table.h"
#include "big.h"
}

/* Branches to `next` if both operands are small. Clobbers TMP1. */
void BeamModuleAssembler::emit_are_both_small(const ArgSource &LHS,
                                              const ppc::Gp lhs_reg,
                                              const ArgSource &RHS,
                                              const ppc::Gp rhs_reg,
                                              const Label next) {
    if (RHS.isLiteral()) {
        comment("skipped test for small because one operand is never small");
    } else if (always_small(RHS) &&
               always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       LHS)) {
        comment("simplified test for small operand since other types are "
                "boxed");
        emit_is_boxed(next, lhs_reg);
    } else if (always_small(LHS) &&
               always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       RHS)) {
        comment("simplified test for small operand since other types are "
                "boxed");
        emit_is_boxed(next, rhs_reg);
    } else if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       LHS) &&
               always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                       RHS)) {
        comment("simplified test for small operands since other types are "
                "boxed");
        ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
        a.and_(TMP1, lhs_reg, rhs_reg);
        emit_is_boxed(next, TMP1);
    } else {
        if (always_small(RHS)) {
            a.andi_rc(TMP1, lhs_reg, imm(_TAG_IMMED1_MASK));
        } else if (always_small(LHS)) {
            a.andi_rc(TMP1, rhs_reg, imm(_TAG_IMMED1_MASK));
        } else {
            ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
            a.and_(TMP1, lhs_reg, rhs_reg);
            a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
        }
        a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
        a.beq(next);
    }
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
template<typename T, T Func>
void BeamGlobalAssembler::emit_bitwise_fallback_body(const ErtsCodeMFA *mfa) {
    Label error = a.new_label();

    emit_enter_runtime_frame();

    /* Save original arguments for the error path. */
    a.std(ARG2, TMP_MEM1q);
    a.std(ARG3, TMP_MEM2q);

    a.mr(ARG1, c_p);
    runtime_call<T, Func>();

    emit_leave_runtime_frame();

    emit_branch_if_not_value(ARG1, error);

    a.blr();

    a.bind(error);
    {
        /* emit_enter_runtime() was done in the module code. */
        emit_leave_runtime(0);

        /* Place the original arguments in X registers. */
        a.ld(XREG0, TMP_MEM1q);
        a.ld(XREG1, TMP_MEM2q);
        mov_imm(ARG4, mfa);
        a.b(labels[raise_exception]);
    }
}

/*
 * Branches to `next` if both operands are small and the sum or
 * difference in ARG1 did not overflow.
 *
 * There is no cheap way to read a non-sticky overflow flag on all the
 * processors we support, so the overflow is derived from the signs of
 * the operands and the result instead.
 *
 * Clobbers TMP1.
 */
void BeamModuleAssembler::emit_add_sub_types(bool is_small_result,
                                             bool is_sub,
                                             const ArgSource &LHS,
                                             const ppc::Gp lhs_reg,
                                             const ArgSource &RHS,
                                             const ppc::Gp rhs_reg,
                                             const Label next) {
    if (is_small_result) {
        comment("skipped overflow test because the result is always small");
        emit_are_both_small(LHS, lhs_reg, RHS, rhs_reg, next);
    } else if (RHS.isLiteral()) {
        /* Skipping test for small */
    } else {
        const bool both_small = always_small(LHS) && always_small(RHS);
        bool overflow_is_lt = true;

        /* andi. always sets CR0, so the test for small operands must
         * come before the test for overflow. */
        if (both_small) {
            comment("skipped test for small operands since they are always "
                    "small");
        } else {
            if (always_small(RHS)) {
                a.andi_rc(TMP1, lhs_reg, imm(_TAG_IMMED1_MASK));
            } else if (always_small(LHS)) {
                a.andi_rc(TMP1, rhs_reg, imm(_TAG_IMMED1_MASK));
            } else {
                ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
                a.and_(TMP1, lhs_reg, rhs_reg);
                a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
            }
            a.cmpdi(ppc::cr1, TMP1, imm(_TAG_IMMED1_SMALL));
        }

        if (RHS.isSmall()) {
            /* Adding a non-negative constant overflows if and only if
             * the result is less than the left operand, and so on. */
            bool is_negative = RHS.as<ArgSmall>().getSigned() < 0;

            overflow_is_lt = is_negative == is_sub;
            a.cmpd(ARG1, lhs_reg);
        } else if (is_sub) {
            /* The operands have different signs, and the sign of the
             * result differs from the sign of the left operand. */
            a.xor_(LR_TMP, lhs_reg, rhs_reg);
            a.xor_(TMP1, lhs_reg, ARG1);
            a.and_rc(LR_TMP, LR_TMP, TMP1);
        } else {
            /* The sign of the result differs from the signs of both
             * operands. */
            a.xor_(LR_TMP, ARG1, lhs_reg);
            a.xor_(TMP1, ARG1, rhs_reg);
            a.and_rc(LR_TMP, LR_TMP, TMP1);
        }

        comment("test for not overflow and small operands");
        if (both_small) {
            if (overflow_is_lt) {
                a.bge(next);
            } else {
                a.ble(next);
            }
        } else {
            if (overflow_is_lt) {
                a.crandc(ppc::cr0.eq(), ppc::cr1.eq(), ppc::cr0.lt());
            } else {
                a.crandc(ppc::cr0.eq(), ppc::cr1.eq(), ppc::cr0.gt());
            }
            a.beq(next);
        }
    }
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_plus_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_Plus, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm),
                               erts_mixed_plus>(&bif_mfa);
}

void BeamModuleAssembler::emit_i_plus(const ArgLabel &Fail,
                                      const ArgWord &Live,
                                      const ArgSource &LHS,
                                      const ArgSource &RHS,
                                      const ArgRegister &Dst) {
    bool is_small_result = is_sum_small_if_args_are_small(LHS, RHS);

    if (always_small(LHS) && always_small(RHS) && is_small_result) {
        auto dst = init_destination(Dst, ARG1);
        if (RHS.isSmall()) {
            auto lhs = load_source(LHS);
            Sint cleared_tag = RHS.as<ArgSmall>().get() & ~_TAG_IMMED1_MASK;
            comment("add small constant without overflow check");
            add(dst.reg, lhs.reg, cleared_tag);
        } else {
            auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
            comment("addition without overflow check");
            a.clrrdi(TMP1, rhs.reg, _TAG_IMMED1_SIZE);
            a.add(dst.reg, lhs.reg, TMP1);
        }
        flush_var(dst);
        return;
    }

    Label next = a.new_label();

    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);

    if (RHS.isLiteral()) {
        comment("skipped test for small because one operand is never small");
    } else if (RHS.isSmall()) {
        Sint cleared_tag = RHS.as<ArgSmall>().get() & ~_TAG_IMMED1_MASK;
        add(ARG1, lhs.reg, cleared_tag);
    } else {
        a.clrrdi(TMP1, rhs.reg, _TAG_IMMED1_SIZE);
        a.add(ARG1, lhs.reg, TMP1);
    }

    emit_add_sub_types(is_small_result,
                       false,
                       LHS,
                       lhs.reg,
                       RHS,
                       rhs.reg,
                       next);

    mov_var(ARG2, lhs);
    mov_var(ARG3, rhs);

    if (Fail.get() != 0) {
        emit_enter_runtime<Update::eReductions>(Live.get());
        a.mr(ARG1, c_p);
        runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_plus>();
        emit_leave_runtime<Update::eReductions>(Live.get());

        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        emit_enter_runtime<Update::eReductions>(Live.get());
        fragment_call(ga->get_plus_body_shared());
        emit_leave_runtime<Update::eReductions>(Live.get());
    }

    a.bind(next);
    mov_arg(Dst, ARG1);
}

/*
 * ARG2 = Src
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_unary_minus_body_shared() {
    Label error = a.new_label();

    emit_enter_runtime_frame();

    /* Save original argument for the error path. */
    a.std(ARG2, TMP_MEM1q);

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm), erts_unary_minus>();

    emit_leave_runtime_frame();

    emit_branch_if_not_value(ARG1, error);

    a.blr();

    a.bind(error);
    {
        static const ErtsCodeMFA bif_mfa = {am_erlang, am_Minus, 1};

        /* emit_enter_runtime() was done in the module code. */
        emit_leave_runtime(0);

        /* Place the original argument in an X register. */
        a.ld(XREG0, TMP_MEM1q);
        mov_imm(ARG4, &bif_mfa);
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_i_unary_minus(const ArgLabel &Fail,
                                             const ArgWord &Live,
                                             const ArgSource &Src,
                                             const ArgRegister &Dst) {
    auto src = load_source(Src, ARG2);
    auto zero = ArgImmed(make_small(0));
    bool is_small_result = is_diff_small_if_args_are_small(zero, Src);

    a.clrrdi(TMP2, src.reg, _TAG_IMMED1_SIZE);

    if (always_small(Src) && is_small_result) {
        auto dst = init_destination(Dst, ARG1);
        comment("no overflow test because result is always small");
        a.neg(dst.reg, TMP2);
        a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));
        flush_var(dst);
        return;
    }

    Label next = a.new_label();

    if (!always_small(Src)) {
        a.andi_rc(TMP1, src.reg, imm(_TAG_IMMED1_MASK));
        a.cmpdi(ppc::cr1, TMP1, imm(_TAG_IMMED1_SMALL));
    }

    /* The negation overflows if and only if the operand and the result
     * are both negative. */
    a.neg(ARG1, TMP2);
    a.and_rc(LR_TMP, TMP2, ARG1);
    a.ori(ARG1, ARG1, imm(_TAG_IMMED1_SMALL));

    /* Test for not overflow AND small operands. */
    if (always_small(Src)) {
        a.bge(next);
    } else {
        a.crandc(ppc::cr0.eq(), ppc::cr1.eq(), ppc::cr0.lt());
        a.beq(next);
    }

    mov_var(ARG2, src);
    if (Fail.get() != 0) {
        emit_enter_runtime<Update::eReductions>(Live.get());
        a.mr(ARG1, c_p);
        runtime_call<Eterm (*)(Process *, Eterm), erts_unary_minus>();
        emit_leave_runtime<Update::eReductions>(Live.get());

        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        emit_enter_runtime<Update::eReductions>(Live.get());
        fragment_call(ga->get_unary_minus_body_shared());
        emit_leave_runtime<Update::eReductions>(Live.get());
    }

    a.bind(next);
    mov_arg(Dst, ARG1);
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_minus_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_Minus, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm),
                               erts_mixed_minus>(&bif_mfa);
}

void BeamModuleAssembler::emit_i_minus(const ArgLabel &Fail,
                                       const ArgWord &Live,
                                       const ArgSource &LHS,
                                       const ArgSource &RHS,
                                       const ArgRegister &Dst) {
    bool is_small_result = is_diff_small_if_args_are_small(LHS, RHS);

    if (always_small(LHS) && always_small(RHS) && is_small_result) {
        auto dst = init_destination(Dst, ARG1);
        if (RHS.isSmall()) {
            auto lhs = load_source(LHS);
            Sint cleared_tag = RHS.as<ArgSmall>().get() & ~_TAG_IMMED1_MASK;
            comment("subtract small constant without overflow check");
            sub(dst.reg, lhs.reg, cleared_tag);
        } else {
            auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
            comment("subtraction without overflow check");
            a.clrrdi(TMP1, rhs.reg, _TAG_IMMED1_SIZE);
            a.sub(dst.reg, lhs.reg, TMP1);
        }
        flush_var(dst);
        return;
    }

    Label next = a.new_label();
    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);

    if (RHS.isLiteral()) {
        comment("skipped test for small because one operand is never small");
    } else if (RHS.isSmall()) {
        Sint cleared_tag = RHS.as<ArgSmall>().get() & ~_TAG_IMMED1_MASK;
        sub(ARG1, lhs.reg, cleared_tag);
    } else {
        a.clrrdi(TMP1, rhs.reg, _TAG_IMMED1_SIZE);
        a.sub(ARG1, lhs.reg, TMP1);
    }

    emit_add_sub_types(is_small_result, true, LHS, lhs.reg, RHS, rhs.reg, next);

    mov_var(ARG2, lhs);
    mov_var(ARG3, rhs);

    if (Fail.get() != 0) {
        emit_enter_runtime<Update::eReductions>(Live.get());
        a.mr(ARG1, c_p);
        runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_minus>();
        emit_leave_runtime<Update::eReductions>(Live.get());
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        emit_enter_runtime<Update::eReductions>(Live.get());
        fragment_call(ga->get_minus_body_shared());
        emit_leave_runtime<Update::eReductions>(Live.get());
    }

    a.bind(next);
    mov_arg(Dst, ARG1);
}

/*
 * Create a bignum from a the 128-bit product of two smalls shifted
 * left _TAG_IMMED1_SIZE bits.
 *
 * ARG1 = low 64 bits
 * TMP2 = high 64 bits
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_int128_to_big_shared() {
    Label positive = a.new_label();

    /* Shift the 128-bit value right to remove the tag bits. */
    a.srdi(ARG3, ARG1, _TAG_IMMED1_SIZE);
    a.rldimi(ARG3, TMP2, imm(64 - _TAG_IMMED1_SIZE), imm(0));
    a.sradi(ARG4, TMP2, imm(_TAG_IMMED1_SIZE));

    a.mr(ARG1, c_p);

    /* ARG2 is the sign: 1 if negative, 0 otherwise. */
    a.srdi(ARG2, ARG4, 63);

    a.cmpdi(ARG4, imm(0));
    a.bge(positive);

    /* Negate the 128-bit value. */
    a.subfic(ARG3, ARG3, imm(0));
    a.subfze(ARG4, ARG4);

    a.bind(positive);
    emit_enter_runtime_frame();
    emit_enter_runtime();

    runtime_call<Eterm (*)(Process *, Uint, Uint, Uint),
                 beam_jit_int128_to_big>();

    emit_leave_runtime();
    emit_leave_runtime_frame();

    a.blr();
}

/* ARG2 = Src1
 * ARG3 = Src2
 * ARG4 = Src4
 *
 * If ARG4 is THE_NON_VALUE, we should not do the addition.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_mul_add_body_shared() {
    Label mul_only = a.new_label(), error = a.new_label(),
          mul_error = a.new_label(), do_error = a.new_label();

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eReductions>();

    /* Save original arguments. */
    a.std(ARG2, TMP_MEM1q);
    a.std(ARG3, TMP_MEM2q);
    a.mr(ARG1, c_p);
    emit_branch_if_not_value(ARG4, mul_only);
    a.std(ARG4, TMP_MEM4q);

    lea(ARG5, TMP_MEM3q);
    runtime_call<Eterm (*)(Process *, Eterm, Eterm, Eterm, Eterm *),
                 erts_mul_add>();

    emit_leave_runtime<Update::eReductions>();
    emit_leave_runtime_frame();

    emit_branch_if_not_value(ARG1, error);
    a.blr();

    a.bind(mul_only);
    {
        runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_times>();

        emit_leave_runtime<Update::eReductions>();
        emit_leave_runtime_frame();

        emit_branch_if_not_value(ARG1, mul_error);
        a.blr();
    }

    a.bind(error);
    {
        static const ErtsCodeMFA mul_mfa = {am_erlang, am_Times, 2};
        static const ErtsCodeMFA add_mfa = {am_erlang, am_Plus, 2};

        a.ld(XREG0, TMP_MEM3q);
        a.ld(XREG1, TMP_MEM4q);
        mov_imm(ARG4, &add_mfa);
        emit_branch_if_value(XREG0, do_error);

        a.bind(mul_error);
        a.ld(XREG0, TMP_MEM1q);
        a.ld(XREG1, TMP_MEM2q);
        mov_imm(ARG4, &mul_mfa);

        a.bind(do_error);
        a.b(labels[raise_exception]);
    }
}

/* ARG2 = Src1
 * ARG3 = Src2
 * ARG4 = Src4
 *
 * The result is returned in ARG1 (set to THE_NON_VALUE if
 * the call failed).
 */
void BeamGlobalAssembler::emit_mul_add_guard_shared() {
    Label done = a.new_label();

    a.std(ARG4, TMP_MEM1q);

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eReductions>();

    a.mr(ARG1, c_p);

    runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_times>();
    emit_branch_if_not_value(ARG1, done); /* Multiplication failed */

    a.ld(ARG3, TMP_MEM1q);
    a.mr(ARG2, ARG1);
    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_plus>();

    a.bind(done);
    emit_leave_runtime<Update::eReductions>();
    emit_leave_runtime_frame();

    a.blr();
}

/* ARG2 = Src1
 * ARG3 = Src2
 *
 * The result is returned in ARG1 (set to THE_NON_VALUE if
 * the call failed).
 */
void BeamGlobalAssembler::emit_mul_guard_shared() {
    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eReductions>();

    a.mr(ARG1, c_p);

    runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_times>();

    emit_leave_runtime<Update::eReductions>();
    emit_leave_runtime_frame();

    a.blr();
}

/*
 * Src1 * Src2 = Src3
 * Src3 + Src4 = Dst
 *
 * Src3 is only included to carry type information about Src1 * Src2 that the
 * compiler has produced. It is not used in the actual instruction.
 *
 * Src4 is optional. If it is not present, the instruction will perform a
 * multiplication only. We signal that it is not to be used by setting it to
 * a Word in ops.tab
 */
void BeamModuleAssembler::emit_i_mul_add(const ArgLabel &Fail,
                                         const ArgSource &Src1,
                                         const ArgSource &Src2,
                                         const ArgSource &Src3,
                                         const ArgVal &Src4,
                                         const ArgRegister &Dst) {
    bool has_increment = !Src4.isWord();
    bool is_product_small = is_product_small_if_args_are_small(Src1, Src2);
    bool is_sum_small =
            !has_increment || is_sum_small_if_args_are_small(Src3, Src4);
    bool sometimes_small = !(Src2.isLiteral() || Src4.isLiteral());
    bool is_increment_zero =
            Src4.isSmall() && Src4.as<ArgSmall>().getSigned() == 0;
    Sint factor = 0;
    int left_shift = -1;

    if (!has_increment) {
        comment("(no increment)");
    } else {
        comment("(with increment)");
    }

    if (Src2.isSmall()) {
        factor = Src2.as<ArgSmall>().getSigned();
        if (Support::is_power_of_2(factor)) {
            left_shift = Support::ctz<Eterm>(factor);
        }
    }

    if (always_small(Src1) && Src2.isSmall() && !has_increment &&
        is_product_small) {
        /* if Src2 is a small literal and we have no increment, just do the
         * simplest possible */
        auto dst = init_destination(Dst, ARG1);
        auto src1 = load_source(Src1, ARG2);

        comment("multiplication without overflow check");
        if (left_shift > 0 &&
            Support::is_int_n<16>(_TAG_IMMED1_SMALL * (factor - 1))) {
            comment("optimized multiplication by replacing with left "
                    "shift and sub");
            /* Only a 16-bit immediate can be used by addi,
               so only then can we use this trick. */
            a.sldi(dst.reg, src1.reg, left_shift);
            a.addi(dst.reg, dst.reg, imm(-_TAG_IMMED1_SMALL * (factor - 1)));
        } else {
            a.clrrdi(TMP1, src1.reg, _TAG_IMMED1_SIZE);
            if (left_shift > 0) {
                comment("optimized multiplication by replacing with left "
                        "shift");
                a.sldi(dst.reg, TMP1, left_shift);
            } else if (Support::is_int_n<16>(factor)) {
                a.mulli(dst.reg, TMP1, imm(factor));
            } else {
                mov_imm(TMP2, factor);
                a.mulld(dst.reg, TMP1, TMP2);
            }
            a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));
        }
        flush_var(dst);
        return;
    }

    if (always_small(Src1) && Src2.isSmall() && has_increment &&
        always_small(Src4) && is_product_small && is_sum_small) {
        /* if Src2 is a small literal and we have an increment */
        auto dst = init_destination(Dst, ARG1);
        auto [src1, src4] = load_sources(Src1, ARG2, Src4, ARG3);

        comment("multiplication and addition without overflow check");
        a.clrrdi(TMP1, src1.reg, _TAG_IMMED1_SIZE);
        if (left_shift > 0) {
            comment("optimized multiplication by replacing with left "
                    "shift");
            a.sldi(TMP1, TMP1, left_shift);
        } else if (Support::is_int_n<16>(factor)) {
            a.mulli(TMP1, TMP1, imm(factor));
        } else {
            mov_imm(TMP2, factor);
            a.mulld(TMP1, TMP1, TMP2);
        }
        a.add(dst.reg, TMP1, src4.reg);
        flush_var(dst);
        return;
    }

    Label small = a.new_label();
    Label store_result = a.new_label();
    auto [src1, src2] = load_sources(Src1, ARG2, Src2, ARG3);
    Variable<ppc::Gp> src4(ARG4);

    if (has_increment) {
        src4 = load_source(Src4, ARG4);
    }

    if (always_small(Src1) && always_small(Src2) &&
        (!has_increment || always_small(Src4))) {
        comment("skipped test for small operands since they are always "
                "small");
    } else {
        comment("test for small operands");
        if (has_increment && always_small(Src4)) {
            emit_are_both_small(Src1, src1.reg, Src2, src2.reg, small);
        } else if (always_small(Src2) && has_increment) {
            emit_are_both_small(Src1, src1.reg, Src4, src4.reg, small);
        } else if (sometimes_small) {
            /* if neither of Src2 nor Src4 are literals, the above
                always_small branches are not taken because we don't know
                enough about the arguments. So we emit a test to see if Src1,
                Src2 and Src4 are all small. If they are, we branch to
                `small` and do the multiplication in JIT.
                */
            ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
            a.and_(TMP1, src1.reg, src2.reg);
            if (has_increment) {
                a.and_(TMP1, TMP1, src4.reg);
            }
            if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                        Src1) &&
                always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                        Src2) &&
                (!has_increment ||
                 always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                         Src4))) {
                emit_is_boxed(small, TMP1);
            } else {
                a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
                a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
                a.beq(small);
            }
        } else {
            comment("skipped test for small because one operand is never "
                    "small");
        }

        comment("overflow or non-small operands, call runtime");
        mov_var(ARG2, src1);
        mov_var(ARG3, src2);

        if (Fail.get() != 0) {
            if (!has_increment) {
                fragment_call(ga->get_mul_guard_shared());
            } else {
                mov_var(ARG4, src4);
                fragment_call(ga->get_mul_add_guard_shared());
            }
            emit_branch_if_not_value(ARG1,
                                     resolve_beam_label(Fail, dispUnknown));
        } else {
            if (!has_increment) {
                mov_imm(ARG4, THE_NON_VALUE);
            } else {
                mov_var(ARG4, src4);
            }
            fragment_call(ga->get_mul_add_body_shared());
        }

        if (sometimes_small) {
            /* Skip the small path after returning from the runtime path. */
            a.b(store_result);
        }
    }

    a.bind(small);

    if (is_product_small && is_sum_small) {
        ASSERT(sometimes_small);

        comment("optimized multiplication and addition without overflow check");

        a.clrrdi(TMP3, src1.reg, _TAG_IMMED1_SIZE);

        if (left_shift > 0) {
            comment("optimized multiplication by replacing with left "
                    "shift");
            a.sldi(TMP3, TMP3, left_shift);
        } else {
            a.sradi(TMP4, src2.reg, imm(_TAG_IMMED1_SIZE));
            a.mulld(TMP3, TMP3, TMP4);
        }

        if (!has_increment) {
            a.ori(ARG1, TMP3, imm(_TAG_IMMED1_SMALL));
        } else {
            a.add(ARG1, TMP3, src4.reg);
        }

        comment("skipped test for small result");
    } else if (sometimes_small) {
        auto min_increment =
                has_increment ? std::get<0>(getClampedRange(Src4)) : 0;

        comment("optimized multiplication and addition with overflow check");

        /* The 128-bit result is kept in TMP2 (high) and ARG1 (low). */
        a.clrrdi(TMP3, src1.reg, _TAG_IMMED1_SIZE);
        if (left_shift == 0) {
            comment("optimized multiplication by one");
            a.mr(ARG1, TMP3);
            a.sradi(TMP2, TMP3, imm(63));
        } else if (left_shift > 0) {
            comment("optimized multiplication by replacing with left "
                    "shift");
            a.sldi(ARG1, TMP3, left_shift);
            a.sradi(TMP2, TMP3, imm(64 - left_shift));
        } else {
            ASSERT(left_shift == -1);
            a.sradi(TMP4, src2.reg, imm(_TAG_IMMED1_SIZE));
            a.mulld(ARG1, TMP3, TMP4);
            a.mulhd(TMP2, TMP3, TMP4);
        }

        if (is_increment_zero || !has_increment) {
            a.ori(ARG1, ARG1, imm(_TAG_IMMED1_SMALL));
        } else if (min_increment > 0) {
            a.addc(ARG1, ARG1, src4.reg);
            a.addze(TMP2, TMP2);
        } else {
            /* sradi changes the carry, so the sign extension of the
             * increment must be calculated before the addition. */
            a.sradi(TMP3, src4.reg, imm(63));
            a.addc(ARG1, ARG1, src4.reg);
            a.adde(TMP2, TMP2, TMP3);
        }

        comment("test whether the result fits in a small");
        /* The high 65 bits of result will all be the same if no
         * overflow occurred. Another way to say that is that the
         * sign bit of the low 64 bits repeated 64 times must be
         * equal to the high 64 bits of the result. */
        a.sradi(TMP3, ARG1, imm(SMALL_BITS + _TAG_IMMED1_SIZE - 1));
        a.cmpd(TMP2, TMP3);
        a.beq(store_result);

        fragment_call(ga->get_int128_to_big_shared());
    }

    a.bind(store_result);

    mov_arg(Dst, ARG1);
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * Quotient is returned in ARG1, remainder in ARG2.
 * Error is indicated by the EQ bit of CR0.
 */
void BeamGlobalAssembler::emit_int_div_rem_guard_shared() {
    Label exit = a.new_label(), generic = a.new_label();

    /* Speculatively go ahead with the division. Division by zero does
     * not trap, it merely gives an undefined result. */
    a.sradi(TMP1, ARG2, imm(_TAG_IMMED1_SIZE));
    a.sradi(TMP2, ARG3, imm(_TAG_IMMED1_SIZE));
    a.divd(TMP3, TMP1, TMP2);
    a.mulld(TMP4, TMP3, TMP2);
    a.sub(TMP4, TMP1, TMP4);

    a.cmpdi(ARG3, imm(make_small(0)));
    a.beq(exit);

    /* Check whether both operands are small integers. */
    ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
    a.and_(TMP1, ARG2, ARG3);
    a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
    a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
    a.bne(generic);

    /* MIN_SMALL divided by -1 will overflow, and we'll need to fall
     * back to the generic handler in that case. */
    a.sradi(TMP1, TMP3, imm(SMALL_BITS - 1));
    a.cmpdi(TMP1, imm(1));
    a.bge(generic);

    /* The EQ bit is now clear (meaning no error), and none of the
     * instructions below change it. */

    a.sldi(TMP3, TMP3, _TAG_IMMED1_SIZE);
    a.sldi(TMP4, TMP4, _TAG_IMMED1_SIZE);
    a.ori(ARG1, TMP3, imm(_TAG_IMMED1_SMALL));
    a.ori(ARG2, TMP4, imm(_TAG_IMMED1_SMALL));

    a.bind(exit);
    {
        a.blr();
    }

    a.bind(generic);
    {
        emit_enter_runtime_frame();
        emit_enter_runtime<Update::eReductions>();

        a.mr(ARG1, c_p);
        lea(ARG4, TMP_MEM4q);
        lea(ARG5, TMP_MEM5q);
        runtime_call<int (*)(Process *, Eterm, Eterm, Eterm *, Eterm *),
                     erts_int_div_rem>();

        emit_leave_runtime<Update::eReductions>();
        emit_leave_runtime_frame();

        /* Note: erts_int_div_rem() returns int, not Sint. */
        a.cmpwi(ARG1, imm(0));
        a.ld(ARG1, TMP_MEM4q);
        a.ld(ARG2, TMP_MEM5q);

        a.blr();
    }
}

/* ARG2 = LHS
 * ARG3 = RHS
 * ARG4 = error MFA
 *
 * Quotient is returned in ARG1, remainder in ARG2.
 */
void BeamGlobalAssembler::emit_int_div_rem_body_shared() {
    Label div_zero = a.new_label(), generic_div = a.new_label(),
          generic_error = a.new_label();

    /* Speculatively go ahead with the division. Division by zero does
     * not trap, it merely gives an undefined result. */
    a.sradi(TMP1, ARG2, imm(_TAG_IMMED1_SIZE));
    a.sradi(TMP2, ARG3, imm(_TAG_IMMED1_SIZE));
    a.divd(TMP3, TMP1, TMP2);
    a.mulld(TMP4, TMP3, TMP2);
    a.sub(TMP4, TMP1, TMP4);

    a.cmpdi(ARG3, imm(make_small(0)));
    a.beq(div_zero);

    /* Check whether both operands are small integers. */
    ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
    a.and_(TMP1, ARG2, ARG3);
    a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
    a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
    a.bne(generic_div);

    /* MIN_SMALL divided by -1 will overflow, and we'll need to fall
     * back to the generic handler in that case. */
    a.sradi(TMP1, TMP3, imm(SMALL_BITS - 1));
    a.cmpdi(TMP1, imm(1));
    a.bge(generic_div);

    a.sldi(TMP3, TMP3, _TAG_IMMED1_SIZE);
    a.sldi(TMP4, TMP4, _TAG_IMMED1_SIZE);
    a.ori(ARG1, TMP3, imm(_TAG_IMMED1_SMALL));
    a.ori(ARG2, TMP4, imm(_TAG_IMMED1_SMALL));

    a.blr();

    a.bind(generic_div);
    {
        emit_enter_runtime_frame();
        emit_enter_runtime<Update::eReductions>();

        /* Save MFA and original arguments for the error path. */
        a.std(ARG2, TMP_MEM1q);
        a.std(ARG3, TMP_MEM2q);
        a.std(ARG4, TMP_MEM3q);

        a.mr(ARG1, c_p);
        lea(ARG4, TMP_MEM4q);
        lea(ARG5, TMP_MEM5q);
        runtime_call<int (*)(Process *, Eterm, Eterm, Eterm *, Eterm *),
                     erts_int_div_rem>();

        emit_leave_runtime<Update::eReductions>();
        emit_leave_runtime_frame();

        /* Note: erts_int_div_rem() returns int, not Sint. */
        a.cmpwi(ARG1, imm(0));
        a.ld(ARG1, TMP_MEM4q);
        a.ld(ARG2, TMP_MEM5q);
        a.beq(generic_error);

        a.blr();
    }

    a.bind(div_zero);
    {
        mov_imm(TMP1, EXC_BADARITH);
        a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
        a.mr(XREG0, ARG2);
        a.mr(XREG1, ARG3);
        a.b(labels[raise_exception]);
    }

    a.bind(generic_error);
    {
        a.ld(XREG0, TMP_MEM1q);
        a.ld(XREG1, TMP_MEM2q);
        a.ld(ARG4, TMP_MEM3q); /* MFA */
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_div_rem_literal(Sint divisor,
                                               const ArgSource &Dividend,
                                               ppc::Gp dividend,
                                               ppc::Gp quotient,
                                               ppc::Gp remainder,
                                               const Label &generic,
                                               bool need_div,
                                               bool need_rem) {
    bool small_dividend = !generic.is_valid();

    ASSERT(divisor != (Sint)0);

    if (!small_dividend) {
        a.andi_rc(TMP1, dividend, imm(_TAG_IMMED1_MASK));
        a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
        a.bne(generic);
    }

    if (Support::is_power_of_2(divisor)) {
        ppc::Gp original_dividend = dividend;
        int shift = Support::ctz<Eterm>(divisor);

        ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
        if (std::get<0>(getClampedRange(Dividend)) >= 0) {
            /* Positive dividend. */
            if (need_div) {
                comment("optimized div by replacing with right shift");
                if (need_rem && quotient == dividend) {
                    original_dividend = TMP5;
                    a.mr(original_dividend, dividend);
                }
                a.srdi(quotient, dividend, shift);
                a.ori(quotient, quotient, imm(_TAG_IMMED1_SMALL));
            }
            if (need_rem) {
                comment("optimized rem by replacing with masking");
                a.clrldi(remainder,
                         original_dividend,
                         64 - (shift + _TAG_IMMED1_SIZE));
            }
        } else {
            /* Negative dividend. */
            if (need_div) {
                comment("optimized div by replacing with right shift");
            }
            if (divisor == 2) {
                ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
                a.srdi(TMP1, dividend, 63);
                a.add(TMP3, dividend, TMP1);
            } else {
                add(TMP1, dividend, (divisor - 1) << _TAG_IMMED1_SIZE);
                a.cmpdi(dividend, imm(0));
                a.isel(TMP3, TMP1, dividend, ppc::cr0.lt());
            }
            if (need_div) {
                if (need_rem && quotient == dividend) {
                    original_dividend = TMP5;
                    a.mr(original_dividend, dividend);
                }
                a.sradi(quotient, TMP3, imm(shift));
                a.ori(quotient, quotient, imm(_TAG_IMMED1_SMALL));
            }
            if (need_rem) {
                comment("optimized rem by replacing with subtraction");
                a.clrrdi(TMP1, TMP3, shift + _TAG_IMMED1_SIZE);
                a.sub(remainder, original_dividend, TMP1);
            }
        }
    } else {
        a.sradi(TMP1, dividend, imm(_TAG_IMMED1_SIZE));
        if (divisor > 0) {
            unsigned shift;
            Uint reciprocal = beam_jit_div_reciprocal(divisor, &shift);

            comment("optimized div by multiplying with the reciprocal");
            mov_imm(TMP3, reciprocal);
            a.mulhd(quotient, TMP1, TMP3);
            if (shift != 0) {
                a.sradi(quotient, quotient, imm(shift));
            }
            if (std::get<0>(getClampedRange(Dividend)) < 0) {
                a.srdi(LR_TMP, TMP1, 63);
                a.add(quotient, quotient, LR_TMP);
            }
        } else {
            mov_imm(TMP2, divisor);
            a.divd(quotient, TMP1, TMP2);
        }
        if (need_rem) {
            if (Support::is_int_n<16>(divisor)) {
                a.mulli(TMP3, quotient, imm(divisor));
            } else {
                mov_imm(TMP2, divisor);
                a.mulld(TMP3, quotient, TMP2);
            }
            a.sub(remainder, TMP1, TMP3);
        }

        if (need_div) {
            a.sldi(quotient, quotient, _TAG_IMMED1_SIZE);
            a.ori(quotient, quotient, imm(_TAG_IMMED1_SMALL));
        }
        if (need_rem) {
            a.sldi(remainder, remainder, _TAG_IMMED1_SIZE);
            a.ori(remainder, remainder, imm(_TAG_IMMED1_SMALL));
        }
    }
}

void BeamModuleAssembler::emit_div_rem(const ArgLabel &Fail,
                                       const ArgSource &LHS,
                                       const ArgSource &RHS,
                                       const ErtsCodeMFA *error_mfa,
                                       const ArgRegister &Quotient,
                                       const ArgRegister &Remainder,
                                       bool need_div,
                                       bool need_rem) {
    Sint divisor = 0;

    if (RHS.isSmall()) {
        divisor = RHS.as<ArgSmall>().getSigned();
        if (divisor == -1) {
            divisor = 0;
        }
    }

    if (always_small(LHS) && divisor != 0) {
        auto lhs = load_source(LHS, ARG3);
        auto quotient = init_destination(Quotient, ARG1);
        auto remainder = init_destination(Remainder, ARG2);
        Label invalidLabel; /* Intentionally not initialized */

        comment("skipped test for smalls operands and overflow");
        emit_div_rem_literal(divisor,
                             LHS,
                             lhs.reg,
                             quotient.reg,
                             remainder.reg,
                             invalidLabel,
                             need_div,
                             need_rem);
        if (need_div) {
            flush_var(quotient);
        }
        if (need_rem) {
            flush_var(remainder);
        }
    } else {
        Label generic = a.new_label(), done = a.new_label();
        auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);

        if (divisor != (Sint)0) {
            emit_div_rem_literal(divisor,
                                 LHS,
                                 lhs.reg,
                                 ARG1,
                                 ARG2,
                                 generic,
                                 need_div,
                                 need_rem);
            a.b(done);
        }

        a.bind(generic);
        mov_var(ARG2, lhs);
        mov_var(ARG3, rhs);
        if (Fail.get() != 0) {
            fragment_call(ga->get_int_div_rem_guard_shared());
            a.beq(resolve_beam_label(Fail, disp32K));
        } else {
            mov_imm(ARG4, error_mfa);
            fragment_call(ga->get_int_div_rem_body_shared());
        }

        a.bind(done);
        if (need_div) {
            mov_arg(Quotient, ARG1);
        }
        if (need_rem) {
            mov_arg(Remainder, ARG2);
        }
    }
}

void BeamModuleAssembler::emit_i_rem_div(const ArgLabel &Fail,
                                         const ArgWord &Live,
                                         const ArgSource &LHS,
                                         const ArgSource &RHS,
                                         const ArgRegister &Remainder,
                                         const ArgRegister &Quotient) {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_rem, 2};
    bool need_rem = Quotient != Remainder;

    emit_div_rem(Fail, LHS, RHS, &bif_mfa, Quotient, Remainder, true, need_rem);
}

void BeamModuleAssembler::emit_i_div_rem(const ArgLabel &Fail,
                                         const ArgWord &Live,
                                         const ArgSource &LHS,
                                         const ArgSource &RHS,
                                         const ArgRegister &Quotient,
                                         const ArgRegister &Remainder) {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_div, 2};
    bool need_div = Quotient != Remainder;

    emit_div_rem(Fail, LHS, RHS, &bif_mfa, Quotient, Remainder, need_div, true);
}

void BeamModuleAssembler::emit_i_int_div(const ArgLabel &Fail,
                                         const ArgWord &Live,
                                         const ArgSource &LHS,
                                         const ArgSource &RHS,
                                         const ArgRegister &Quotient) {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_div, 2};
    ArgYRegister Dummy(0);

    emit_div_rem(Fail, LHS, RHS, &bif_mfa, Quotient, Dummy, true, false);
}

void BeamModuleAssembler::emit_i_rem(const ArgLabel &Fail,
                                     const ArgWord &Live,
                                     const ArgSource &LHS,
                                     const ArgSource &RHS,
                                     const ArgRegister &Remainder) {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_rem, 2};
    ArgYRegister Dummy(0);

    emit_div_rem(Fail, LHS, RHS, &bif_mfa, Dummy, Remainder, false, true);
}

void BeamModuleAssembler::emit_i_m_div(const ArgLabel &Fail,
                                       const ArgWord &Live,
                                       const ArgSource &LHS,
                                       const ArgSource &RHS,
                                       const ArgRegister &Dst) {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_Div, 2};

    Label next = a.new_label();

    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);

    mov_var(ARG2, lhs);
    mov_var(ARG3, rhs);

    emit_enter_runtime(Live.get());

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_mixed_div>();

    emit_leave_runtime(Live.get());

    cmp(ARG1, THE_NON_VALUE);

    if (Fail.get() != 0) {
        a.beq(resolve_beam_label(Fail, disp32K));
    } else {
        a.bne(next);

        mov_arg(XREG0, LHS);
        mov_arg(XREG1, RHS);

        emit_raise_exception(&bif_mfa);
    }

    a.bind(next);
    mov_arg(Dst, ARG1);
}

void BeamGlobalAssembler::emit_i_band_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_band, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm), erts_band>(
            &bif_mfa);
}

void BeamModuleAssembler::emit_i_band(const ArgLabel &Fail,
                                      const ArgWord &Live,
                                      const ArgSource &LHS,
                                      const ArgSource &RHS,
                                      const ArgRegister &Dst) {
    if (always_small(LHS) && RHS.isSmall()) {
        /* Only masks that fit in 16 bits, or that keep a number of the
         * least significant bits, can be used without loading them into a
         * register first. */
        const Uint64 mask = RHS.as<ArgSmall>().get();
        const bool is_low_bits = mask != ~0ull && ((mask + 1) & mask) == 0;

        if (Support::is_uint_n<16>(mask) || is_low_bits) {
            comment("skipped test for small operands since they are always "
                    "small");
            auto lhs = load_source(LHS);
            auto dst = init_destination(Dst, ARG1);

            /* TAG & TAG = TAG, so we don't need to tag it again. */
            if (is_low_bits) {
                a.clrldi(dst.reg, lhs.reg, Support::clz(mask));
            } else {
                a.andi_rc(dst.reg, lhs.reg, imm(mask));
            }
            flush_var(dst);
            return;
        }
    }

    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
    auto dst = init_destination(Dst, ARG1);

    if (always_small(LHS) && always_small(RHS)) {
        comment("skipped test for small operands since they are always small");

        /* TAG & TAG = TAG, so we don't need to tag it again. */
        a.and_(dst.reg, lhs.reg, rhs.reg);
        flush_var(dst);
    } else {
        Label next = a.new_label();

        if (RHS.isLiteral()) {
            comment("skipped test for small because one operand is never "
                    "small");
        } else {
            /* TAG & TAG = TAG, so we don't need to tag it again. */
            a.and_(ARG1, lhs.reg, rhs.reg);

            ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
            if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                        LHS) &&
                always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(
                        RHS)) {
                comment("simplified test for small operands since other types "
                        "are boxed");
                emit_is_boxed(next, ARG1);
            } else {
                /* All other term types has at least one zero in the low 4
                 * bits. Therefore, the result will be a small iff both
                 * operands are small. */
                a.andi_rc(TMP1, ARG1, imm(_TAG_IMMED1_MASK));
                a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
                a.beq(next);
            }
        }

        mov_var(ARG2, lhs);
        mov_var(ARG3, rhs);

        if (Fail.get() != 0) {
            emit_enter_runtime<Update::eReductions>(Live.get());
            a.mr(ARG1, c_p);
            runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_band>();

            emit_leave_runtime<Update::eReductions>(Live.get());
            emit_branch_if_not_value(ARG1,
                                     resolve_beam_label(Fail, dispUnknown));
        } else {
            emit_enter_runtime<Update::eReductions>(Live.get());
            fragment_call(ga->get_i_band_body_shared());
            emit_leave_runtime<Update::eReductions>(Live.get());
        }

        a.bind(next);
        mov_var(dst, ARG1);
        flush_var(dst);
    }
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * Result is returned in RET.
 */
void BeamGlobalAssembler::emit_i_bor_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_bor, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm), erts_bor>(
            &bif_mfa);
}

void BeamModuleAssembler::emit_i_bor(const ArgLabel &Fail,
                                     const ArgWord &Live,
                                     const ArgSource &LHS,
                                     const ArgSource &RHS,
                                     const ArgRegister &Dst) {
    if (always_small(LHS) && RHS.isSmall()) {
        Uint64 rhs = RHS.as<ArgSmall>().get() & ~_TAG_IMMED1_SMALL;

        /* The immediate forms can only set bits in the low 32 bits. */
        if (Support::is_uint_n<32>(rhs)) {
            comment("skipped test for small operands since they are always "
                    "small");
            auto lhs = load_source(LHS);
            auto dst = init_destination(Dst, ARG1);
            ppc::Gp src_reg = lhs.reg;

            if (rhs & 0xFFFF) {
                a.ori(dst.reg, src_reg, imm(rhs & 0xFFFF));
                src_reg = dst.reg;
            }

            if (rhs >> 16) {
                a.oris(dst.reg, src_reg, imm(rhs >> 16));
                src_reg = dst.reg;
            }

            if (src_reg != dst.reg) {
                a.mr(dst.reg, src_reg);
            }

            flush_var(dst);
            return;
        }
    }

    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
    auto dst = init_destination(Dst, ARG1);

    if (always_small(LHS) && always_small(RHS)) {
        comment("skipped test for small operands since they are always small");

        /* TAG | TAG = TAG, so we don't need to tag it again. */
        a.or_(dst.reg, lhs.reg, rhs.reg);
        flush_var(dst);
    } else {
        Label next = a.new_label();

        /* TAG | TAG = TAG, so we don't need to tag it again. */
        a.or_(ARG1, lhs.reg, rhs.reg);

        emit_are_both_small(LHS, lhs.reg, RHS, rhs.reg, next);

        mov_var(ARG2, lhs);
        mov_var(ARG3, rhs);

        if (Fail.get() != 0) {
            emit_enter_runtime<Update::eReductions>(Live.get());
            a.mr(ARG1, c_p);
            runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_bor>();
            emit_leave_runtime<Update::eReductions>(Live.get());
            emit_branch_if_not_value(ARG1,
                                     resolve_beam_label(Fail, dispUnknown));
        } else {
            emit_enter_runtime<Update::eReductions>(Live.get());
            fragment_call(ga->get_i_bor_body_shared());
            emit_leave_runtime<Update::eReductions>(Live.get());
        }

        a.bind(next);
        mov_var(dst, ARG1);
        flush_var(dst);
    }
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_i_bxor_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_bxor, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm), erts_bxor>(
            &bif_mfa);
}

void BeamModuleAssembler::emit_i_bxor(const ArgLabel &Fail,
                                      const ArgWord &Live,
                                      const ArgSource &LHS,
                                      const ArgSource &RHS,
                                      const ArgRegister &Dst) {
    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
    auto dst = init_destination(Dst, ARG1);

    if (always_small(LHS) && always_small(RHS)) {
        comment("skipped test for small operands because they are always "
                "small");

        /* TAG ^ TAG = 0, so we'll need to tag it again. */
        a.xor_(dst.reg, lhs.reg, rhs.reg);
        a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));
        flush_var(dst);
        return;
    }

    Label next = a.new_label();

    /* TAG ^ TAG = 0, so we'll need to tag it again. */
    a.xor_(ARG1, lhs.reg, rhs.reg);
    a.ori(ARG1, ARG1, imm(_TAG_IMMED1_SMALL));

    emit_are_both_small(LHS, lhs.reg, RHS, rhs.reg, next);

    mov_var(ARG2, lhs);
    mov_var(ARG3, rhs);

    if (Fail.get() != 0) {
        emit_enter_runtime<Update::eReductions>(Live.get());
        a.mr(ARG1, c_p);
        runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_bxor>();
        emit_leave_runtime<Update::eReductions>(Live.get());
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        emit_enter_runtime<Update::eReductions>(Live.get());
        fragment_call(ga->get_i_bxor_body_shared());
        emit_leave_runtime<Update::eReductions>(Live.get());
    }

    a.bind(next);
    {
        mov_var(dst, ARG1);
        flush_var(dst);
    }
}

/*
 * ARG1 = Src
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1. Error is indicated by
 * THE_NON_VALUE.
 */
void BeamGlobalAssembler::emit_i_bnot_guard_shared() {
    emit_enter_runtime_frame();

    /* Undo the speculative inversion in module code. */
    a.li(LR_TMP, imm(~(Sint)_TAG_IMMED1_MASK));
    a.xor_(ARG2, ARG1, LR_TMP);

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm), erts_bnot>();

    emit_leave_runtime_frame();

    a.blr();
}

/*
 * ARG1 = Src
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_i_bnot_body_shared() {
    Label error = a.new_label();

    emit_enter_runtime_frame();

    /* Undo the speculative inversion in module code. */
    a.li(LR_TMP, imm(~(Sint)_TAG_IMMED1_MASK));
    a.xor_(ARG2, ARG1, LR_TMP);

    /* Save original arguments for the error path. */
    a.std(ARG2, TMP_MEM1q);

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm), erts_bnot>();

    emit_leave_runtime_frame();

    emit_branch_if_not_value(ARG1, error);
    a.blr();

    a.bind(error);
    {
        static const ErtsCodeMFA bif_mfa = {am_erlang, am_bnot, 1};

        /* emit_enter_runtime() was done in the module code. */
        emit_leave_runtime(0);

        /* Place the original arguments in X registers. */
        a.ld(XREG0, TMP_MEM1q);
        mov_imm(ARG4, &bif_mfa);
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_i_bnot(const ArgLabel &Fail,
                                      const ArgWord &Live,
                                      const ArgSource &Src,
                                      const ArgRegister &Dst) {
    if (always_small(Src)) {
        comment("skipped test for small operand since it is always small");
        auto src = load_source(Src, TMP2);
        auto dst = init_destination(Dst, ARG1);

        /* The complement of a small is always a small. Invert everything
         * except the tag so we don't have to tag it again. */
        a.li(LR_TMP, imm(~(Sint)_TAG_IMMED1_MASK));
        a.xor_(dst.reg, src.reg, LR_TMP);
        flush_var(dst);
        return;
    }

    Label next = a.new_label();
    auto src = load_source(Src, TMP2);
    auto dst = init_destination(Dst, ARG1);

    /* Invert everything except the tag so we don't have to tag it again. */
    a.li(LR_TMP, imm(~(Sint)_TAG_IMMED1_MASK));
    a.xor_(ARG1, src.reg, LR_TMP);

    if (always_one_of<BeamTypeId::Number>(Src)) {
        comment("simplified test for small operand since it is a number");
        emit_is_boxed(next, Src, ARG1);
    } else {
        a.andi_rc(TMP1, src.reg, imm(_TAG_IMMED1_MASK));
        a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
        a.beq(next);
    }

    if (Fail.get() != 0) {
        emit_enter_runtime<Update::eReductions>(Live.get());
        fragment_call(ga->get_i_bnot_guard_shared());
        emit_leave_runtime<Update::eReductions>(Live.get());
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        emit_enter_runtime<Update::eReductions>(Live.get());
        fragment_call(ga->get_i_bnot_body_shared());
        emit_leave_runtime<Update::eReductions>(Live.get());
    }

    a.bind(next);
    mov_var(dst, ARG1);
    flush_var(dst);
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_i_bsr_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_bsr, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm), erts_bsr>(
            &bif_mfa);
}

void BeamModuleAssembler::emit_i_bsr(const ArgLabel &Fail,
                                     const ArgWord &Live,
                                     const ArgSource &LHS,
                                     const ArgSource &RHS,
                                     const ArgRegister &Dst) {
    Label generic = a.new_label(), next = a.new_label();
    auto lhs = load_source(LHS, ARG2);
    auto dst = init_destination(Dst, ARG1);
    bool need_generic = true;

    if (RHS.isSmall()) {
        Sint shift = RHS.as<ArgSmall>().getSigned();

        if (shift >= 0) {
            if (always_small(LHS)) {
                comment("skipped test for small left operand because it is "
                        "always small");
                need_generic = false;
            } else if (always_one_of<BeamTypeId::Number>(LHS)) {
                comment("simplified test for small operand since it is a "
                        "number");
                emit_is_not_boxed(generic, lhs.reg);
            } else {
                a.andi_rc(TMP1, lhs.reg, imm(_TAG_IMMED1_MASK));
                a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
                a.bne(generic);
            }

            /* We don't need to clear the mask after shifting because
             * _TAG_IMMED1_SMALL will set all the bits anyway. */
            ERTS_CT_ASSERT(_TAG_IMMED1_MASK == _TAG_IMMED1_SMALL);
            shift = std::min<Sint>(shift, 63);
            a.sradi(dst.reg, lhs.reg, imm(shift));
            a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));

            if (need_generic) {
                a.b(next);
            }
        } else {
            /* Constant shift is negative; fall back to the generic
             * path. */
        }
    } else {
        auto rhs = load_source(RHS, ARG3);

        /* Ensure that both operands are small and that the shift
         * count is positive. */
        ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
        a.and_(TMP1, lhs.reg, rhs.reg);
        a.andi_rc(TMP1, TMP1, imm(_TAG_IMMED1_MASK));
        a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
        a.cmpdi(ppc::cr1, rhs.reg, imm(0));
        a.crandc(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.lt());
        a.bne(generic);

        /* Calculate shift count. A count above 63 must be clamped, as only
         * its seven lowest bits are used by the shift. */
        a.sradi(TMP1, rhs.reg, imm(_TAG_IMMED1_SIZE));
        a.li(TMP2, imm(63));
        a.cmpdi(TMP1, imm(63));
        a.isel(TMP1, TMP2, TMP1, ppc::cr0.gt());

        /* Shift right. */
        ERTS_CT_ASSERT(_TAG_IMMED1_MASK == _TAG_IMMED1_SMALL);
        a.srad(dst.reg, lhs.reg, TMP1);
        a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));
        a.b(next);
    }

    a.bind(generic);
    if (need_generic) {
        mov_var(ARG2, lhs);
        mov_arg(ARG3, RHS);

        if (Fail.get() != 0) {
            emit_enter_runtime<Update::eReductions>(Live.get());
            a.mr(ARG1, c_p);
            runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_bsr>();
            emit_leave_runtime<Update::eReductions>(Live.get());
            emit_branch_if_not_value(ARG1,
                                     resolve_beam_label(Fail, dispUnknown));
        } else {
            emit_enter_runtime<Update::eReductions>(Live.get());
            fragment_call(ga->get_i_bsr_body_shared());
            emit_leave_runtime<Update::eReductions>(Live.get());
        }

        mov_var(dst, ARG1);
    }

    a.bind(next);
    flush_var(dst);
}

/*
 * ARG2 = LHS
 * ARG3 = RHS
 *
 * The module code must have executed emit_enter_runtime()
 * before calling this function.
 *
 * The result is returned in ARG1.
 */
void BeamGlobalAssembler::emit_i_bsl_body_shared() {
    static const ErtsCodeMFA bif_mfa = {am_erlang, am_bsl, 2};
    emit_bitwise_fallback_body<Eterm (*)(Process *, Eterm, Eterm), erts_bsl>(
            &bif_mfa);
}

static int count_leading_zeroes(UWord value) {
    const int word_bits = sizeof(value) * CHAR_BIT;

    if (value == 0) {
        return word_bits;
    }

    return Support::clz(value);
}

void BeamModuleAssembler::emit_i_bsl(const ArgLabel &Fail,
                                     const ArgWord &Live,
                                     const ArgSource &LHS,
                                     const ArgSource &RHS,
                                     const ArgRegister &Dst) {
    auto dst = init_destination(Dst, ARG1);

    if (is_bsl_small(LHS, RHS)) {
        comment("skipped tests because operands and result are always small");
        if (RHS.isSmall()) {
            auto lhs = load_source(LHS);
            a.clrrdi(TMP1, lhs.reg, _TAG_IMMED1_SIZE);
            a.sldi(TMP1, TMP1, RHS.as<ArgSmall>().getSigned());
        } else {
            auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
            a.clrrdi(TMP1, lhs.reg, _TAG_IMMED1_SIZE);
            a.srdi(TMP2, rhs.reg, _TAG_IMMED1_SIZE);
            a.sld(TMP1, TMP1, TMP2);
        }
        a.ori(dst.reg, TMP1, imm(_TAG_IMMED1_SMALL));
        flush_var(dst);
        return;
    }

    auto [lhs, rhs] = load_sources(LHS, ARG2, RHS, ARG3);
    Label generic = a.new_label(), next = a.new_label();
    bool inline_shift = true;

    if (LHS.isImmed() && RHS.isImmed()) {
        /* The compiler should've optimized this away, so we'll fall
         * back to the generic path to simplify the inline
         * implementation. */
        inline_shift = false;
    } else if (LHS.isLiteral() || RHS.isLiteral()) {
        /* At least one argument is not a small. */
        inline_shift = false;
    } else if (LHS.isImmed() && !LHS.isSmall()) {
        /* Invalid constant. */
        inline_shift = false;
    } else if (RHS.isImmed() &&
               (!RHS.isSmall() || RHS.as<ArgSmall>().getSigned() < 0 ||
                RHS.as<ArgSmall>().getSigned() >= SMALL_BITS - 1)) {
        /* Constant shift is invalid or always produces a bignum. */
        inline_shift = false;
    }

    if (inline_shift) {
        /* The shift limit is the number of leading sign bits of the small,
         * not counting the sign bit itself. It is either a constant or in
         * ARG4. */
        const ppc::Gp limit_reg = ARG4;
        const ppc::Gp count_reg = ARG5;
        Sint limit_imm = -1;

        ASSERT(!(LHS.isImmed() && RHS.isImmed()));
        if (LHS.isRegister()) {
            /* There is no instruction that counts leading sign bits.
             * Every bit that differs from the one above it is set in the
             * exclusive or of the untagged value and itself shifted right
             * by one, so its number of leading zeros is the number of
             * leading bits that are equal to the sign bit.
             *
             * That is five more than the limit: the four copies of the
             * sign bit that untagging added, and the sign bit itself. */
            a.sradi(TMP1, lhs.reg, imm(_TAG_IMMED1_SIZE));
            a.sradi(LR_TMP, lhs.reg, imm(_TAG_IMMED1_SIZE + 1));
            a.xor_(LR_TMP, TMP1, LR_TMP);
            a.cntlzd(limit_reg, LR_TMP);

            if (always_small(LHS)) {
                comment("skipped test for small operand since it is always "
                        "small");
            } else if (always_one_of<BeamTypeId::Number>(LHS)) {
                comment("simplified test for small operand since it is a "
                        "number");
                emit_is_not_boxed(generic, lhs.reg);
            } else {
                a.andi_rc(TMP1, lhs.reg, imm(_TAG_IMMED1_MASK));
                a.cmpdi(TMP1, imm(_TAG_IMMED1_SMALL));
                a.bne(generic);
            }
        } else {
            UWord value = LHS.as<ArgSmall>().get();

            if (signed_val(value) < 0) {
                value ^= ~(UWord)_TAG_IMMED1_MASK;
            }

            limit_imm = count_leading_zeroes(value) - 1;
        }

        a.clrrdi(TMP1, lhs.reg, _TAG_IMMED1_SIZE);

        if (RHS.isRegister()) {
            /* Negate the tag bits and then rotate them out, forcing the
             * comparison below to fail for non-smalls. */
            ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
            a.xori(count_reg, rhs.reg, imm(_TAG_IMMED1_SMALL));
            a.rotldi(count_reg, count_reg, 64 - _TAG_IMMED1_SIZE);

            /* Fall back to generic path when the shift magnitude is negative
             * or greater than the leading zero count. */
            if (limit_imm >= 0) {
                cmpl(count_reg, limit_imm);
            } else {
                a.addi(limit_reg, limit_reg, imm(-(_TAG_IMMED1_SIZE + 1)));
                a.cmpld(count_reg, limit_reg);
            }
            a.bgt(generic);

            a.sld(TMP1, TMP1, count_reg);
        } else {
            const Sint count = RHS.as<ArgSmall>().getSigned();

            ASSERT(limit_imm < 0);

            a.cmpdi(limit_reg, imm(count + _TAG_IMMED1_SIZE + 1));
            a.blt(generic);

            a.sldi(TMP1, TMP1, count);
        }

        a.ori(dst.reg, TMP1, imm(_TAG_IMMED1_SMALL));

        flush_var(dst);
        a.b(next);
    }

    a.bind(generic);
    {
        mov_var(ARG2, lhs);
        mov_var(ARG3, rhs);

        if (Fail.get() != 0) {
            emit_enter_runtime<Update::eReductions>(Live.get());
            a.mr(ARG1, c_p);
            runtime_call<Eterm (*)(Process *, Eterm, Eterm), erts_bsl>();
            emit_leave_runtime<Update::eReductions>(Live.get());
            emit_branch_if_not_value(ARG1,
                                     resolve_beam_label(Fail, dispUnknown));
        } else {
            emit_enter_runtime<Update::eReductions>(Live.get());
            fragment_call(ga->get_i_bsl_body_shared());
            emit_leave_runtime<Update::eReductions>(Live.get());
        }

        mov_var(dst, ARG1);
        flush_var(dst);
    }

    a.bind(next);
}
