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
#include "big.h"
}

/* Checks whether f0 contains a finite value.
 *
 * Clobbers f1. */
void BeamGlobalAssembler::emit_check_float_error() {
    Label error = a.new_label();

    /* Subtracting a value from itself gives zero if it is finite, and NaN
     * if it is an infinity or NaN. Comparing a NaN is unordered. */
    a.fsub(ppc::f1, ppc::f0, ppc::f0);
    a.fcmpu(ppc::cr0, ppc::f1, ppc::f1);
    a.bso(error);
    a.blr();

    a.bind(error);
    {
        mov_imm(ARG4, 0);
        mov_imm(TMP1, EXC_BADARITH);
        a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_float_instr(uint32_t instId,
                                           const ArgFRegister &LHS,
                                           const ArgFRegister &RHS,
                                           const ArgFRegister &Dst) {
    auto lhs = load_source(LHS, ppc::f2);
    auto rhs = load_source(RHS, ppc::f3);
    auto dst = init_destination(Dst, ppc::f4);

    a.emit(instId, ppc::f0, lhs.reg, rhs.reg);
    fragment_call(ga->get_check_float_error());
    a.fmr(dst.reg, ppc::f0);
    flush_var(dst);
}

/* * * * */

void BeamModuleAssembler::emit_fload(const ArgSource &Src,
                                     const ArgFRegister &Dst) {
    auto src = load_source(Src, TMP1);
    auto dst = init_destination(Dst, ppc::f0);
    ppc::Gp float_ptr = emit_ptr_val(TMP1, src.reg);

    a.lfd(dst.reg, emit_boxed_val(float_ptr, sizeof(Eterm)));
    flush_var(dst);
}

void BeamModuleAssembler::emit_fstore(const ArgFRegister &Src,
                                      const ArgRegister &Dst) {
    auto src = load_source(Src, ppc::f0);
    auto dst = init_destination(Dst, TMP1);

    a.addi(dst.reg, HTOP, imm(TAG_PRIMARY_BOXED));

    mov_imm(TMP2, HEADER_FLONUM);
    a.std(TMP2, ppc::Mem(HTOP));
    a.stfd(src.reg, ppc::Mem(HTOP, sizeof(Eterm)));
    a.addi(HTOP, HTOP, imm(2 * sizeof(Eterm)));

    flush_var(dst);
}

/* ARG1 = source term
 *
 * The result is returned in f0. */
void BeamGlobalAssembler::emit_fconv_shared() {
    Label error = a.new_label();

    /* Is the source a bignum? */
    {
        emit_is_boxed(error, ARG1);

        emit_untag_ptr(TMP1, ARG1);
        a.ld(TMP1, ppc::Mem(TMP1));

        a.andi_rc(TMP2, TMP1, imm(_TAG_HEADER_MASK - _BIG_SIGN_BIT));
        a.cmpdi(TMP2, imm(_TAG_HEADER_POS_BIG));
        a.bne(error);
    }

    emit_enter_runtime_frame();
    emit_enter_runtime();

    /* ARG1 already contains the source term. */
    lea(ARG2, TMP_MEM1q);
    runtime_call<int (*)(Eterm, double *), big_to_double>();

    emit_leave_runtime();
    emit_leave_runtime_frame();

    /* Note: big_to_double() returns int, not Sint. */
    a.cmpwi(ARG1, imm(0));
    a.blt(error);

    a.lfd(ppc::f0, TMP_MEM1q);
    a.blr();

    a.bind(error);
    {
        mov_imm(ARG4, 0);
        mov_imm(TMP1, EXC_BADARITH);
        a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_fconv(const ArgSource &Src,
                                     const ArgFRegister &Dst) {
    auto dst = init_destination(Dst, ppc::f0);
    auto src = load_source(Src, ARG1);

    if (always_small(Src)) {
        comment("skipped test for small operand since it is always small");
        a.sradi(TMP1, src.reg, imm(_TAG_IMMED1_SIZE));
        emit_int_to_double(dst.reg, TMP1);
        flush_var(dst);
        return;
    }

    Label next = a.new_label(), not_small = a.new_label(),
          fallback = a.new_label();

    a.andi_rc(TMP1, src.reg, imm(_TAG_IMMED1_MASK));
    a.cmpdi(TMP1, imm(_TAG_IMMED1_MASK));
    a.bne(not_small);

    a.sradi(TMP1, src.reg, imm(_TAG_IMMED1_SIZE));
    emit_int_to_double(dst.reg, TMP1);
    a.b(next);

    a.bind(not_small);
    {
        if (never_one_of<BeamTypeId::Float>(Src)) {
            comment("skipped float path since source cannot be a float");
        } else {
            /* If the source is always a number, we can skip the box test when
             * it's not a small. */
            if (always_one_of<BeamTypeId::Number>(Src)) {
                comment("skipped box test since source is always a number");
            } else {
                emit_is_boxed(fallback, Src, src.reg);
            }

            emit_untag_ptr(TMP1, src.reg);

            /* Speculatively load the float value, this is safe since all boxed
             * terms are at least two words long. */
            a.lfd(dst.reg, ppc::Mem(TMP1, sizeof(Eterm)));

            a.ld(TMP1, ppc::Mem(TMP1));
            cmp(TMP1, HEADER_FLONUM);
            a.beq(next);
        }

        a.bind(fallback);
        {
            mov_var(ARG1, src);
            fragment_call(ga->get_fconv_shared());
            if (dst.reg != ppc::f0) {
                a.fmr(dst.reg, ppc::f0);
            }
        }
    }

    a.bind(next);
    flush_var(dst);
}

void BeamModuleAssembler::emit_i_fadd(const ArgFRegister &LHS,
                                      const ArgFRegister &RHS,
                                      const ArgFRegister &Dst) {
    emit_float_instr(ppc::Inst::kIdFadd, LHS, RHS, Dst);
}

void BeamModuleAssembler::emit_i_fsub(const ArgFRegister &LHS,
                                      const ArgFRegister &RHS,
                                      const ArgFRegister &Dst) {
    emit_float_instr(ppc::Inst::kIdFsub, LHS, RHS, Dst);
}

void BeamModuleAssembler::emit_i_fmul(const ArgFRegister &LHS,
                                      const ArgFRegister &RHS,
                                      const ArgFRegister &Dst) {
    emit_float_instr(ppc::Inst::kIdFmul, LHS, RHS, Dst);
}

void BeamModuleAssembler::emit_i_fdiv(const ArgFRegister &LHS,
                                      const ArgFRegister &RHS,
                                      const ArgFRegister &Dst) {
    emit_float_instr(ppc::Inst::kIdFdiv, LHS, RHS, Dst);
}

void BeamModuleAssembler::emit_i_fnegate(const ArgFRegister &Src,
                                         const ArgFRegister &Dst) {
    auto src = load_source(Src, ppc::f0);
    auto dst = init_destination(Dst, ppc::f1);

    /* Note that there is no need to check for errors since flipping the sign
     * of a finite float is guaranteed to produce a finite float. */
    a.fneg(dst.reg, src.reg);
    flush_var(dst);
}
