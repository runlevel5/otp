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
