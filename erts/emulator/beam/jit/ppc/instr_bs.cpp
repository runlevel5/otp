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
#include "beam_jit_bs.hpp"
#include <numeric>

extern "C"
{
#include "erl_binary.h"
#include "erl_bits.h"
#include "beam_common.h"
}

/* Clobbers TMP2
 *
 * Returns -1 when the field check always fails, 1 if it may fail, and 0 if it
 * never fails. */
int BeamModuleAssembler::emit_bs_get_field_size(const ArgSource &Size,
                                                int unit,
                                                Label fail,
                                                const ppc::Gp &out) {
    if (Size.isImmed()) {
        if (Size.isSmall()) {
            Sint sval = Size.as<ArgSmall>().getSigned();

            if (sval < 0) {
                /* badarg */
            } else if (sval > (MAX_SMALL / unit)) {
                /* system_limit */
            } else {
                mov_imm(out, sval * unit);
                return 0;
            }
        }

        a.b(fail);
        return -1;
    } else {
        auto size_reg = load_source(Size, TMP2);
        bool can_fail = true;

        if (always_small(Size)) {
            auto [min, max] = getClampedRange(Size);
            can_fail =
                    !(0 <= min && (max >> (SMALL_BITS - ERL_UNIT_BITS)) == 0);
        }

        /* Negating the tag bits lets us guard against non-smalls, negative
         * numbers, and overflow with a single test. */
        ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);
        ASSERT(unit <= 1024);

        if (!can_fail) {
            comment("simplified segment size checks because "
                    "the types are known");
        }

        if (unit == 1 && !can_fail) {
            a.srdi(out, size_reg.reg, _TAG_IMMED1_SIZE);
        } else {
            a.xori(out, size_reg.reg, imm(_TAG_IMMED1_SMALL));
        }

        if (can_fail) {
            /* Test the mask 0xFFF0000000000000 | _TAG_IMMED1_MASK. Rotating
             * left by 12 bits brings both parts of it together in the low 16
             * bits. The result is left in CR0, which none of the instructions
             * below change. */
            ERTS_CT_ASSERT(_TAG_IMMED1_MASK == 0xF);
            a.rldicl_rc(LR_TMP, out, imm(12), imm(48));
        }

        if (unit == 0) {
            /* Silly but legal.*/
            mov_imm(out, 0);
        } else if (unit == 1 && !can_fail) {
            /* The result is already in the out register. */
            ;
        } else if (Support::is_power_of_2(unit)) {
            int trailing_bits = Support::ctz<Eterm>(unit);

            /* The tag bits were cleared out by the argument check, so all we
             * need to do is shift the result into place. */
            if (trailing_bits < _TAG_IMMED1_SIZE) {
                a.srdi(out, out, _TAG_IMMED1_SIZE - trailing_bits);
            } else if (trailing_bits > _TAG_IMMED1_SIZE) {
                a.sldi(out, out, trailing_bits - _TAG_IMMED1_SIZE);
            }
        } else {
            a.srdi(out, out, _TAG_IMMED1_SIZE);
            a.mulli(out, out, imm(unit));
        }

        if (can_fail) {
            a.bne(fail);
        }

        return can_fail;
    }
}

/* Set the error reason when a size check has failed. */
void BeamGlobalAssembler::emit_bs_size_check_shared() {
    emit_enter_runtime_frame();
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *, Eterm),
                 beam_jit_bs_field_size_argument_error>();

    emit_leave_runtime(0);
    emit_leave_runtime_frame();

    mov_imm(ARG4, 0);
    a.b(labels[raise_exception]);
}

void BeamModuleAssembler::emit_i_bs_start_match3(const ArgRegister &Src,
                                                 const ArgWord &Live,
                                                 const ArgLabel &Fail,
                                                 const ArgRegister &Dst) {
    Label next = a.new_label();

    mov_arg(ARG2, Src);

    if (Fail.get() != 0) {
        emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, ARG2);
    } else {
        /* bs_start_match3 may not throw, and the compiler will only emit {f,0}
         * when it knows that the source is a match state or binary, so we're
         * free to skip the binary tests. */
    }

    emit_untag_ptr(TMP1, ARG2);

    a.ld(TMP2, ppc::Mem(TMP1, offsetof(ErlSubBits, thing_word)));
    a.ld(TMP3, ppc::Mem(TMP1, offsetof(ErlSubBits, base_flags)));

    /* Insert the flags into the two lowest bits of the header word. */
    ERTS_CT_ASSERT((HEADER_SUB_BITS & _TAG_PRIMARY_MASK) == 0 &&
                   (ERL_SUB_BITS_FLAG_MASK == _TAG_PRIMARY_MASK));
    a.rldimi(TMP2, TMP3, imm(0), imm(62));
    cmp(TMP2, HEADER_SUB_BITS | ERL_SUB_BITS_FLAGS_MATCH_CONTEXT);
    a.beq(next);

    {
        if (Fail.get() != 0) {
            const auto mask = _BITSTRING_TAG_MASK & ~_TAG_PRIMARY_MASK;
            ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
            ERTS_CT_ASSERT(_TAG_HEADER_HEAP_BITS ==
                           (_TAG_HEADER_HEAP_BITS & mask));

            a.andi_rc(TMP2, TMP2, imm(mask));
            cmp(TMP2, _TAG_HEADER_HEAP_BITS);
            a.bne(resolve_beam_label(Fail, disp32K));
        }

        emit_gc_test_preserve(ArgWord(ERL_SUB_BITS_SIZE), Live, Src, ARG2);

        emit_enter_runtime<Update::eHeapOnlyAlloc>(Live.get());

        a.mr(ARG1, c_p);
        /* ARG2 was set above */
        runtime_call<ErlSubBits *(*)(Process *, Eterm),
                     erts_bs_start_match_3>();

        emit_leave_runtime<Update::eHeapOnlyAlloc>(Live.get());

        a.addi(ARG2, ARG1, imm(TAG_PRIMARY_BOXED));
    }

    a.bind(next);
    mov_arg(Dst, ARG2);
}

void BeamModuleAssembler::emit_i_bs_match_string(const ArgRegister &Ctx,
                                                 const ArgLabel &Fail,
                                                 const ArgWord &Bits,
                                                 const ArgBytePtr &Ptr) {
    const auto size = Bits.get();

    {
        auto ctx_reg = load_source(Ctx, TMP1);

        emit_untag_ptr(TMP5, ctx_reg.reg);
        a.ld(TMP2, ppc::Mem(TMP5, offsetof(ErlSubBits, start)));
        a.ld(TMP3, ppc::Mem(TMP5, offsetof(ErlSubBits, end)));
        add(TMP4, TMP2, size);
        a.cmpld(TMP4, TMP3);
        a.bgt(resolve_beam_label(Fail, disp32K));

        /* ARG3 = (sb->base_flags & ~mask) + (sb->start >> 3) */
        ERTS_CT_ASSERT(ERL_SUB_BITS_FLAG_MASK == 3);
        a.ld(TMP1, ppc::Mem(TMP5, offsetof(ErlSubBits, base_flags)));
        a.clrrdi(TMP1, TMP1, 2);
        a.srdi(ARG3, TMP2, 3);
        a.add(ARG3, ARG3, TMP1);

        /* ARG4 = sb->start & 7 */
        a.clrldi(ARG4, TMP2, 61);
    }

    emit_enter_runtime();

    mov_arg(ARG1, Ptr);
    mov_imm(ARG2, 0);
    mov_imm(ARG5, size);
    runtime_call<int (*)(const byte *, Uint, const byte *, Uint, Uint),
                 erts_cmp_bits>();

    emit_leave_runtime();
    a.cmpwi(ARG1, imm(0));
    a.bne(resolve_beam_label(Fail, disp32K));

    /* The call cannot move the match context, and it preserves both the
     * pointer to it in TMP5 and the new position in TMP4. */
    a.std(TMP4, ppc::Mem(TMP5, offsetof(ErlSubBits, start)));
}

void BeamModuleAssembler::emit_i_bs_get_position(const ArgRegister &Ctx,
                                                 const ArgRegister &Dst) {
    const int start_offset = offsetof(ErlSubBits, start);
    auto ctx = load_source(Ctx);
    auto dst = init_destination(Dst, TMP2);
    ppc::Gp ctx_reg = ctx.reg;
    ppc::Gp dst_reg = dst.reg;

    /* Match contexts can never be literals, so we can skip clearing literal
     * tags. */
    preserve_cache(
            [&]() {
                load_mem(dst_reg, emit_boxed_val(ctx_reg, start_offset));
                a.sldi(dst_reg, dst_reg, _TAG_IMMED1_SIZE);
                a.ori(dst_reg, dst_reg, imm(_TAG_IMMED1_SMALL));
            },
            dst_reg);

    flush_var(dst);
}

void BeamModuleAssembler::emit_bs_get_small(const Label &fail,
                                            const ArgRegister &Ctx,
                                            const ArgWord &Live,
                                            const ArgSource &Sz,
                                            Uint unit,
                                            Uint flags) {
    if (emit_bs_get_field_size(Sz, unit, fail, ARG2) >= 0) {
        auto ctx_reg = load_source(Ctx, TMP1);

        comment("simplified helper call because the result is a known small");

        /* We KNOW that the process argument is never actually used. */
#ifdef DEBUG
        mov_imm(ARG1, 0);
#endif
        mov_imm(ARG3, flags);
        emit_untag_ptr(ARG4, ctx_reg.reg);

        emit_enter_runtime(Live.get());
        runtime_call<Eterm (*)(Process *, Uint, unsigned, ErlSubBits *),
                     erts_bs_get_integer_2>();

        emit_leave_runtime(Live.get());

        emit_branch_if_not_value(ARG1, fail);
    }
}

void BeamModuleAssembler::emit_bs_get_any_int(const Label &fail,
                                              const ArgRegister &Ctx,
                                              const ArgWord &Live,
                                              const ArgSource &Sz,
                                              Uint unit,
                                              Uint flags) {
    if (emit_bs_get_field_size(Sz, unit, fail, ARG5) >= 0) {
        a.mr(ARG1, c_p);
        load_x_reg_array(ARG2);
        mov_arg(ARG3, Ctx);
        mov_imm(ARG4, flags);
        mov_arg(ARG6, Live);

        emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                           Update::eReductions>(Live.get());
        runtime_call<Eterm (*)(Process *, Eterm *, Eterm, Uint, Uint, Uint),
                     beam_jit_bs_get_integer>();
        emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                           Update::eReductions>(Live.get());

        emit_branch_if_not_value(ARG1, fail);
        /* Test for max heap size exceeded. */
        emit_is_not_cons(resolve_fragment(ga->get_do_schedule(), dispUnknown),
                         ARG1);
    }
}

void BeamModuleAssembler::emit_bs_get_integer2(const ArgLabel &Fail,
                                               const ArgRegister &Ctx,
                                               const ArgWord &Live,
                                               const ArgSource &Sz,
                                               const ArgWord &Unit,
                                               const ArgWord &Flags,
                                               const ArgRegister &Dst) {
    Uint size;
    Uint flags = Flags.get();

    if (flags & BSF_NATIVE) {
        flags &= ~BSF_NATIVE;
#if !defined(WORDS_BIGENDIAN)
        flags |= BSF_LITTLE;
#endif
    }

    if (Sz.isSmall() && Sz.as<ArgSmall>().getUnsigned() < 8 * sizeof(Uint) &&
        (size = Sz.as<ArgSmall>().getUnsigned() * Unit.get()) <
                8 * sizeof(Uint)) {
        /* Segment of a fixed size supported by bs_match. */
        const ArgVal match[] = {ArgAtom(am_ensure_at_least),
                                ArgWord(size),
                                ArgWord(1),
                                ArgAtom(am_integer),
                                Live,
                                ArgWord(flags),
                                ArgWord(size),
                                ArgWord(1),
                                Dst};

        const Span<const ArgVal> args(match, sizeof(match) / sizeof(match[0]));
        emit_i_bs_match(Fail, Ctx, args);
    } else {
        Label fail = resolve_beam_label(Fail, dispUnknown);
        int unit = Unit.get();
        auto max = std::get<1>(getClampedRange(Sz));
        bool potential_gc =
                max >= SMALL_BITS || (max * Unit.get()) >= SMALL_BITS;

        if (potential_gc) {
            emit_bs_get_any_int(fail, Ctx, Live, Sz, unit, flags);
        } else {
            emit_bs_get_small(fail, Ctx, Live, Sz, unit, flags);
        }

        mov_arg(Dst, ARG1);
    }
}

void BeamModuleAssembler::emit_bs_test_tail2(const ArgLabel &Fail,
                                             const ArgRegister &Ctx,
                                             const ArgWord &Offset) {
    /* This instruction is only found in unoptimized code and in code
     * compiled for Erlang/OTP 25 and earlier. */
    const ArgVal match[] = {ArgAtom(am_ensure_exactly), Offset};
    const Span<const ArgVal> args(match, sizeof(match) / sizeof(match[0]));

    emit_i_bs_match(Fail, Ctx, args);
}

void BeamModuleAssembler::emit_bs_set_position(const ArgRegister &Ctx,
                                               const ArgRegister &Pos) {
    const int start_offset = offsetof(ErlSubBits, start);
    auto [ctx, pos] = load_sources(Ctx, TMP1, Pos, TMP2);
    ppc::Gp ctx_reg = ctx.reg;
    ppc::Gp pos_reg = pos.reg;

    preserve_cache(
            [&]() {
                a.srdi(TMP2, pos_reg, _TAG_IMMED1_SIZE);
                store_mem(TMP2, emit_boxed_val(ctx_reg, start_offset));
            },
            TMP2);
}

void BeamModuleAssembler::emit_i_bs_get_binary_all2(const ArgRegister &Ctx,
                                                    const ArgLabel &Fail,
                                                    const ArgWord &Live,
                                                    const ArgWord &Unit,
                                                    const ArgRegister &Dst) {
    /* This instruction is only found in unoptimized code and in code
     * compiled for Erlang/OTP 25 and earlier. */
    unsigned unit = Unit.get();
    const ArgVal match[] = {ArgAtom(am_ensure_at_least),
                            ArgWord(0),
                            ArgWord(unit),

                            ArgAtom(am_get_tail),
                            ArgWord(Live),
                            ArgWord(unit),
                            Dst};
    const Span<const ArgVal> args(match, sizeof(match) / sizeof(match[0]));

    emit_i_bs_match(Fail, Ctx, args);
}

void BeamGlobalAssembler::emit_bs_get_tail_shared() {
    emit_untag_ptr(TMP1, ARG1);

    a.ld(ARG4, ppc::Mem(TMP1, offsetof(ErlSubBits, base_flags)));
    a.ld(ARG2, ppc::Mem(TMP1, offsetof(ErlSubBits, orig)));

    ERTS_CT_ASSERT(ERL_SUB_BITS_FLAG_MASK == 3 && TAG_PTR_MASK__ == 7);
    a.clrrdi(ARG4, ARG4, 2);
    a.clrrdi(ARG3, ARG2, 3);
    a.clrldi(ARG2, ARG2, 61);

    /* Extracted size = sb->end - sb->start */
    a.ld(ARG5, ppc::Mem(TMP1, offsetof(ErlSubBits, start)));
    a.ld(TMP1, ppc::Mem(TMP1, offsetof(ErlSubBits, end)));
    a.sub(ARG6, TMP1, ARG5);

    lea(ARG1, ppc::Mem(c_p, offsetof(Process, htop)));

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapOnlyAlloc>();

    runtime_call<Eterm (*)(Eterm **,
                           Eterm,
                           const BinRef *,
                           const byte *,
                           Uint,
                           Uint),
                 erts_build_sub_bitstring>();

    emit_leave_runtime<Update::eHeapOnlyAlloc>();
    emit_leave_runtime_frame();

    a.blr();
}

void BeamModuleAssembler::emit_bs_get_tail(const ArgRegister &Ctx,
                                           const ArgRegister &Dst,
                                           const ArgWord &Live) {
    mov_arg(ARG1, Ctx);

    emit_gc_test_preserve(ArgWord(BUILD_SUB_BITSTRING_HEAP_NEED),
                          Live,
                          Ctx,
                          ARG1);

    fragment_call(ga->get_bs_get_tail_shared());

    mov_arg(Dst, ARG1);
}

/* Bits to skip are passed in ARG1 */
void BeamModuleAssembler::emit_bs_skip_bits(const ArgLabel &Fail,
                                            const ArgRegister &Ctx) {
    auto ctx_reg = load_source(Ctx, TMP1);

    emit_untag_ptr(TMP4, ctx_reg.reg);
    a.ld(TMP2, ppc::Mem(TMP4, offsetof(ErlSubBits, start)));
    a.ld(TMP3, ppc::Mem(TMP4, offsetof(ErlSubBits, end)));

    a.add(TMP2, TMP2, ARG1);
    a.cmpld(TMP2, TMP3);
    a.bgt(resolve_beam_label(Fail, disp32K));

    a.std(TMP2, ppc::Mem(TMP4, offsetof(ErlSubBits, start)));
}

void BeamModuleAssembler::emit_i_bs_skip_bits2(const ArgRegister &Ctx,
                                               const ArgSource &Size,
                                               const ArgLabel &Fail,
                                               const ArgWord &Unit) {
    Label fail = resolve_beam_label(Fail, dispUnknown);

    bool can_fail = true;

    if (always_small(Size)) {
        auto [min, max] = getClampedRange(Size);
        can_fail = !(0 <= min && (max >> (SMALL_BITS - ERL_UNIT_BITS)) == 0);
    }

    if (!can_fail && Unit.get() == 1) {
        comment("simplified skipping because the types are known");

        auto [ctx, size] = load_sources(Ctx, TMP1, Size, TMP2);

        emit_untag_ptr(TMP5, ctx.reg);
        a.ld(TMP3, ppc::Mem(TMP5, offsetof(ErlSubBits, start)));
        a.ld(TMP4, ppc::Mem(TMP5, offsetof(ErlSubBits, end)));

        a.srdi(TMP1, size.reg, _TAG_IMMED1_SIZE);
        a.add(TMP3, TMP3, TMP1);
        a.cmpld(TMP3, TMP4);
        a.bgt(resolve_beam_label(Fail, disp32K));

        a.std(TMP3, ppc::Mem(TMP5, offsetof(ErlSubBits, start)));
    } else if (emit_bs_get_field_size(Size, Unit.get(), fail, ARG1) >= 0) {
        emit_bs_skip_bits(Fail, Ctx);
    }
}

void BeamModuleAssembler::emit_bs_get_binary(const ArgWord heap_need,
                                             const ArgRegister &Ctx,
                                             const ArgLabel &Fail,
                                             const ArgWord &Live,
                                             const ArgSource &Size,
                                             const ArgWord &Unit,
                                             const ArgRegister &Dst) {
    Label fail;
    int unit;

    fail = resolve_beam_label(Fail, dispUnknown);
    unit = Unit.get();

    if (emit_bs_get_field_size(Size, unit, fail, ARG2) >= 0) {
        a.std(ARG2, TMP_MEM1q);

        mov_arg(ARG4, Ctx);

        emit_gc_test_preserve(heap_need, Live, Ctx, ARG4);

        emit_untag_ptr(ARG3, ARG4);

        emit_enter_runtime<Update::eHeapOnlyAlloc>(Live.get());

        a.mr(ARG1, c_p);
        a.ld(ARG2, TMP_MEM1q);
        runtime_call<Eterm (*)(Process *, Uint, ErlSubBits *),
                     erts_bs_get_binary_2>();

        emit_leave_runtime<Update::eHeapOnlyAlloc>(Live.get());

        emit_branch_if_not_value(ARG1, fail);

        mov_arg(Dst, ARG1);
    }
}

void BeamModuleAssembler::emit_i_bs_get_binary2(const ArgRegister &Ctx,
                                                const ArgLabel &Fail,
                                                const ArgWord &Live,
                                                const ArgSource &Size,
                                                const ArgWord &Unit,
                                                const ArgRegister &Dst) {
    emit_bs_get_binary(ArgWord(BUILD_SUB_BITSTRING_HEAP_NEED),
                       Ctx,
                       Fail,
                       Live,
                       Size,
                       Unit,
                       Dst);
}

void BeamModuleAssembler::emit_i_bs_get_bin_and_tail(const ArgRegister &Ctx,
                                                     const ArgLabel &Fail,
                                                     const ArgWord &Live,
                                                     const ArgRegister &Size,
                                                     const ArgWord &Unit,
                                                     const ArgRegister &Dst1,
                                                     const ArgRegister &Dst2) {
    emit_bs_get_binary(ArgWord(2 * BUILD_SUB_BITSTRING_HEAP_NEED),
                       Ctx,
                       Fail,
                       Live,
                       Size,
                       Unit,
                       Dst1);

    mov_arg(ARG1, Ctx);
    fragment_call(ga->get_bs_get_tail_shared());
    mov_arg(Dst2, ARG1);
}

void BeamModuleAssembler::emit_i_bs_get_float2(const ArgRegister &Ctx,
                                               const ArgLabel &Fail,
                                               const ArgWord &Live,
                                               const ArgSource &Sz,
                                               const ArgWord &Flags,
                                               const ArgRegister &Dst) {
    Label fail;
    Sint unit;

    fail = resolve_beam_label(Fail, dispUnknown);
    unit = Flags.get() >> 3;

    mov_arg(ARG4, Ctx);

    emit_gc_test_preserve(ArgWord(FLOAT_SIZE_OBJECT), Live, Ctx, ARG4);

    if (emit_bs_get_field_size(Sz, unit, fail, ARG2) >= 0) {
        emit_untag_ptr(ARG4, ARG4);

        emit_enter_runtime<Update::eHeapOnlyAlloc>(Live.get());

        a.mr(ARG1, c_p);
        mov_imm(ARG3, Flags.get());
        runtime_call<Eterm (*)(Process *, Uint, unsigned, ErlSubBits *),
                     erts_bs_get_float_2>();

        emit_leave_runtime<Update::eHeapOnlyAlloc>(Live.get());

        emit_branch_if_not_value(ARG1, fail);

        mov_arg(Dst, ARG1);
    }
}

void BeamModuleAssembler::emit_bs_init_writable() {
    ERTS_CT_ASSERT(ERTS_HIGHEST_CALLEE_SAVE_XREG >= 0);

    a.mr(ARG1, c_p);
    a.mr(ARG2, XREG0);

    /* We have an implicit liveness of 0, so we don't need to stash X
     * registers. */
    emit_enter_runtime<Update::eReductions | Update::eHeapAlloc>(0);

    runtime_call<Eterm (*)(Process *, Eterm), erts_bs_init_writable>();

    emit_leave_runtime<Update::eReductions | Update::eHeapAlloc>(0);

    a.mr(XREG0, ARG1);
}

void BeamGlobalAssembler::emit_bs_create_bin_error_shared() {
    a.mflr(XREG0);

    emit_enter_runtime<Update::eHeapAlloc>(0);

    /* ARG3 is already set by the caller */
    a.mr(ARG2, ARG4);
    a.mr(ARG4, ARG1);
    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *, Uint, Eterm, Eterm),
                 beam_jit_bs_construct_fail_info>();

    emit_leave_runtime<Update::eHeapAlloc>(0);

    mov_imm(ARG4, 0);
    a.mr(ARG2, XREG0);
    a.b(labels[raise_exception_shared]);
}

/*
 * ARG1 = tagged bignum term
 *
 * On success, the least significant 64 bits are returned in ARG1 and
 * the EQ bit of CR0 is cleared. If ARG1 is not a bignum, the EQ bit of
 * CR0 is set.
 */
void BeamGlobalAssembler::emit_get_sint64_shared() {
    Label success = a.new_label();
    Label fail = a.new_label();

    emit_is_boxed(fail, ARG1);
    ppc::Gp boxed_ptr = emit_ptr_val(TMP3, ARG1);
    load_mem(TMP1, emit_boxed_val(boxed_ptr));
    load_mem(TMP2, emit_boxed_val(boxed_ptr, sizeof(Eterm)));
    a.andi_rc(TMP1, TMP1, imm(_TAG_HEADER_MASK));
    a.cmpdi(TMP1, imm(POS_BIG_SUBTAG));
    a.beq(success);

    a.cmpdi(TMP1, imm(NEG_BIG_SUBTAG));
    a.bne(fail);

    a.neg(TMP2, TMP2);

    a.bind(success);
    {
        a.mr(ARG1, TMP2);
        /* Clear the EQ bit.
         *
         * TMP1 is known to be POS_BIG_SUBTAG or NEG_BIG_SUBTAG at this point.
         */
        ERTS_CT_ASSERT(POS_BIG_SUBTAG != 0 && NEG_BIG_SUBTAG != 0);
        a.cmpdi(TMP1, imm(0));
        a.blr();
    }

    a.bind(fail);
    {
        a.cmpd(ARG1, ARG1);
        a.blr();
    }
}

/*
 * In:
 *    bin_offset = register to store the bit offset into the binary
 *    bit_offset = current bit offset into binary, or -1 if unknown
 *    size = size of segment to be constructed
 *           (ignored if size_reg is valid register)
 *    size_reg = if a valid register, it contains the size of
 *               the segment to be constructed
 *
 * Out:
 *    bin_offset register = if bit_offset is not byte aligned, the bit
 *          offset into the binary
 *    TMP1 = pointer to the current byte in the binary
 *
 *    Preserves all other ARG* registers.
 */
void BeamModuleAssembler::update_bin_state(ppc::Gp bin_offset,
                                           Sint bit_offset,
                                           Sint size,
                                           ppc::Gp size_reg) {
    int cur_bin_offset = offsetof(ErtsSchedulerRegisters,
                                  aux_regs.d.erl_bits_state.erts_current_bin);
    ppc::Mem mem_bin_base = ppc::Mem(scheduler_registers, cur_bin_offset);
    ppc::Mem mem_bin_offset =
            ppc::Mem(scheduler_registers,
                     offsetof(ErtsSchedulerRegisters,
                              aux_regs.d.erl_bits_state.erts_bin_offset));

    if (bit_offset % 8 != 0) {
        /* The bit offset is unknown or not byte-aligned. */
        load_mem(TMP2, mem_bin_base);
        load_mem(bin_offset, mem_bin_offset);

        if (size_reg.is_valid()) {
            a.add(TMP1, bin_offset, size_reg);
        } else {
            add(TMP1, bin_offset, size);
        }
        store_mem(TMP1, mem_bin_offset);

        a.srdi(TMP1, bin_offset, 3);
        a.add(TMP1, TMP2, TMP1);
    } else {
        comment("optimized updating of binary construction state");
        ASSERT(size >= 0 || size_reg.is_valid());
        ASSERT(bit_offset % 8 == 0);
        load_mem(TMP1, mem_bin_base);
        if (size_reg.is_valid()) {
            if (bit_offset == 0) {
                store_mem(size_reg, mem_bin_offset);
            } else {
                add(TMP2, size_reg, bit_offset);
                store_mem(TMP2, mem_bin_offset);
            }
        } else {
            mov_imm(TMP2, bit_offset + size);
            store_mem(TMP2, mem_bin_offset);
        }
        if (bit_offset != 0) {
            add(TMP1, TMP1, bit_offset >> 3);
        }
    }
}

/*
 * The size of the segment is assumed to be in ARG3.
 */
void BeamModuleAssembler::set_zero(Sint effectiveSize) {
    Label store_units = a.new_label();
    Label less_than_a_store_unit = a.new_label();
    Sint store_unit = 1;
    const ppc::Gp zero = LR_TMP;

    update_bin_state(ARG2, -1, -1, ARG3);

    a.li(zero, imm(0));

    if (effectiveSize >= 256) {
        /* Store four 64-bit words machine words when the size is
         * known and at least 256 bits. */
        store_unit = 4;
    } else if (effectiveSize >= 128) {
        /* Store two 64-bit words machine words when the size is
         * known and at least 128 bits. */
        store_unit = 2;
    }

    if (effectiveSize < Sint(store_unit * 8 * sizeof(Eterm))) {
        /* The size is either not known or smaller than a word. */
        a.cmpdi(ARG3, imm(store_unit * 8 * sizeof(Eterm)));
        a.blt(less_than_a_store_unit);
    }

    a.bind(store_units);
    for (Sint i = 0; i < store_unit; i++) {
        a.std(zero, ppc::Mem(TMP1, i * sizeof(Eterm)));
    }
    a.addi(TMP1, TMP1, imm(store_unit * sizeof(Eterm)));
    a.addi(ARG3, ARG3, imm(-(store_unit * 8 * (Sint)sizeof(Eterm))));

    a.cmpdi(ARG3, imm(store_unit * 8 * sizeof(Eterm)));
    a.bge(store_units);

    a.bind(less_than_a_store_unit);
    if (effectiveSize < 0) {
        /* Unknown size. */
        Label byte_loop = a.new_label();
        Label done = a.new_label();

        ASSERT(store_unit == 1);

        a.cmpdi(ARG3, imm(0));
        a.beq(done);

        a.bind(byte_loop);
        a.stb(zero, ppc::Mem(TMP1));
        a.addi(TMP1, TMP1, imm(1));
        a.addi(ARG3, ARG3, imm(-8));
        a.cmpdi(ARG3, imm(0));
        a.bgt(byte_loop);

        a.bind(done);
    } else if (effectiveSize % (store_unit * 8 * sizeof(Eterm)) != 0) {
        /* The size is known, and we know that there are less than
         * 256 bits to initialize. */
        int offset = 0;

        if (store_unit == 4 && (effectiveSize & 255) >= 128) {
            a.std(zero, ppc::Mem(TMP1, offset));
            a.std(zero, ppc::Mem(TMP1, offset + 8));
            offset += 16;
        }

        if ((effectiveSize & 127) >= 64) {
            a.std(zero, ppc::Mem(TMP1, offset));
            offset += 8;
        }

        if ((effectiveSize & 63) >= 32) {
            a.stw(zero, ppc::Mem(TMP1, offset));
            offset += 4;
        }

        if ((effectiveSize & 31) >= 16) {
            a.sth(zero, ppc::Mem(TMP1, offset));
            offset += 2;
        }

        if ((effectiveSize & 15) >= 8) {
            a.stb(zero, ppc::Mem(TMP1, offset));
            offset += 1;
        }

        if ((effectiveSize & 7) > 0) {
            a.stb(zero, ppc::Mem(TMP1, offset));
        }
    }
}

/*
 * Stores bytes [first_byte, first_byte + num_bytes) of the segment data
 * at the same offsets from `base`.
 *
 * Big-endian data is a stream of bits that starts at the most significant
 * bit of `data`. Little-endian data is right-aligned in `data` and stored
 * with its least significant byte first.
 *
 * Clobbers TMP2 and LR_TMP.
 */
void BeamModuleAssembler::emit_store_bytes(ppc::Gp data,
                                           bool big_endian,
                                           ppc::Gp base,
                                           int first_byte,
                                           int num_bytes) {
#if defined(WORDS_BIGENDIAN)
    const bool is_native = big_endian;
#else
    const bool is_native = !big_endian;
#endif
    int index = first_byte;
    int left = num_bytes;

    ASSERT(base != LR_TMP && base != TMP2);

    while (left > 0) {
        int chunk = left >= 8 ? 8 : left >= 4 ? 4 : left >= 2 ? 2 : 1;
        int shift = big_endian ? 64 - 8 * (index + chunk) : 8 * index;
        ppc::Gp value = data;

        ASSERT(0 <= shift && shift < 64);
        if (shift != 0) {
            a.srdi(TMP2, data, shift);
            value = TMP2;
        }

        if (chunk == 1) {
            a.stb(value, ppc::Mem(base, index));
        } else if (is_native) {
            switch (chunk) {
            case 2:
                a.sth(value, ppc::Mem(base, index));
                break;
            case 4:
                a.stw(value, ppc::Mem(base, index));
                break;
            case 8:
                store_mem(value, ppc::Mem(base, index));
                break;
            }
        } else {
            mov_imm(LR_TMP, index);
            switch (chunk) {
            case 2:
                a.sthbrx(value, ppc::Mem(base, LR_TMP));
                break;
            case 4:
                a.stwbrx(value, ppc::Mem(base, LR_TMP));
                break;
            case 8:
                a.stdbrx(value, ppc::Mem(base, LR_TMP));
                break;
            }
        }

        index += chunk;
        left -= chunk;
    }
}

/*
 * In:
 *
 *   ARG1 = valid unicode code point (=> 0x80) to encode
 *
 * Out:
 *
 *   ARG1 = the code point encoded in UTF-8, right-aligned with the
 *          first byte being the most significant one.
 *   ARG4 = number of bits of result (16, 24, or 32)
 *
 *   Preserves other ARG* registers, clobbers TMP* registers
 */
void BeamGlobalAssembler::emit_construct_utf8_shared() {
    Label more_than_two_bytes = a.new_label();
    Label four_bytes = a.new_label();
    const ppc::Gp value = ARG1;
    const ppc::Gp num_bits = ARG4;

    a.cmpldi(value, imm(0x800));
    a.bge(more_than_two_bytes);

    /* Encode Unicode code point in two bytes. */
    a.rldicr(TMP1, value, imm(2), imm(55));
    a.clrldi(TMP2, value, 58);
    a.or_(TMP1, TMP1, TMP2);
    mov_imm(num_bits, 16);
    a.ori(value, TMP1, imm(0xc080));
    a.blr();

    /* Test whether the value should be encoded in four bytes. */
    a.bind(more_than_two_bytes);
    a.srdi(TMP1, value, 16);
    a.cmpdi(TMP1, imm(0));
    a.bne(four_bytes);

    /* Encode Unicode code point in three bytes. */
    a.rldicr(TMP1, value, imm(4), imm(47));
    a.rlwinm(TMP2, value, imm(2), imm(18), imm(23));
    a.clrldi(TMP3, value, 58);
    a.or_(TMP1, TMP1, TMP2);
    a.or_(TMP1, TMP1, TMP3);
    mov_imm(num_bits, 24);
    a.oris(TMP1, TMP1, imm(0xe0));
    a.ori(value, TMP1, imm(0x8080));
    a.blr();

    /* Encode Unicode code point in four bytes. */
    a.bind(four_bytes);
    a.rlwinm(TMP1, value, imm(6), imm(5), imm(7));
    a.rlwinm(TMP2, value, imm(4), imm(10), imm(15));
    a.rlwinm(TMP3, value, imm(2), imm(18), imm(23));
    a.clrldi(value, value, 58);
    a.or_(TMP1, TMP1, TMP2);
    a.or_(TMP3, TMP3, value);
    a.or_(TMP1, TMP1, TMP3);
    mov_imm(num_bits, 32);
    a.oris(TMP1, TMP1, imm(0xf080));
    a.ori(value, TMP1, imm(0x8080));
    a.blr();
}

void BeamModuleAssembler::emit_construct_utf8(const ArgVal &Src,
                                              Sint bit_offset,
                                              bool is_byte_aligned) {
    Label prepare_store = a.new_label();
    Label store = a.new_label();
    Label next = a.new_label();

    comment("construct utf8 segment");
    auto src = load_source(Src, ARG1);

    a.srdi(ARG1, src.reg, _TAG_IMMED1_SIZE);
    mov_imm(ARG4, 8);
    a.cmpldi(ARG1, imm(0x80));
    a.blt(prepare_store);

    fragment_call(ga->get_construct_utf8_shared());

    a.bind(prepare_store);
    ppc::Gp bin_offset = ARG3;
    update_bin_state(bin_offset, bit_offset, -1, ARG4);

    /* Turn the encoded code point into a stream of bits that starts
     * at the most significant bit of ARG1. */
    a.subfic(TMP3, ARG4, imm(64));
    a.sld(ARG1, ARG1, TMP3);

    if (!is_byte_aligned) {
        /* Not known to be byte-aligned. Must test alignment. */
        a.andi_rc(TMP2, bin_offset, imm(7));
        a.beq(store);

        /* We must combine the last partial byte with the UTF-8
         * encoded code point. */
        a.lbz(TMP5, ppc::Mem(TMP1));

        a.srd(ARG1, ARG1, TMP2);

        a.sld(TMP5, TMP5, TMP2);
        a.clrrdi(TMP5, TMP5, 8);
        a.srd(TMP5, TMP5, TMP2);
        a.sldi(TMP5, TMP5, 56);

        a.or_(ARG1, ARG1, TMP5);

        a.addi(ARG4, ARG4, imm(8));
    }

    a.bind(store);
    if (bit_offset % (4 * 8) == 0) {
        /* This segment is aligned on a 4-byte boundary. This implies
         * that a 4-byte write will be inside the allocated binary. */
        emit_store_bytes(ARG1, true, TMP1, 0, 4);
    } else {
        Label do_store_1 = a.new_label();
        Label do_store_2 = a.new_label();

        /* Unsuitable or unknown alignment. We must be careful not
         * to write beyound the allocated end of the binary. */
        a.cmpdi(ARG4, imm(8));
        a.bne(do_store_1);

        emit_store_bytes(ARG1, true, TMP1, 0, 1);
        a.b(next);

        a.bind(do_store_1);
        a.cmpdi(ARG4, imm(24));
        a.bgt(do_store_2);

        emit_store_bytes(ARG1, true, TMP1, 0, 2);
        a.cmpdi(ARG4, imm(16));
        a.beq(next);

        emit_store_bytes(ARG1, true, TMP1, 2, 1);
        a.b(next);

        a.bind(do_store_2);
        emit_store_bytes(ARG1, true, TMP1, 0, 4);

        if (!is_byte_aligned) {
            a.cmpdi(ARG4, imm(32));
            a.beq(next);

            emit_store_bytes(ARG1, true, TMP1, 4, 1);
        }
    }

    a.bind(next);
}

/*
 * In:
 *   TMP1 = pointer to current byte
 *   ARG3 = bit offset
 *   ARG4 = number of bits to write
 *   ARG8 = data to write, as a stream of bits that starts at the most
 *          significant bit
 */
void BeamGlobalAssembler::emit_store_unaligned() {
    Label loop = a.new_label();
    Label done = a.new_label();
    const ppc::Gp left_bit_offset = ARG3;
    const ppc::Gp right_bit_offset = TMP6;
    const ppc::Gp num_bits = ARG4;
    const ppc::Gp bitdata = ARG8;

    /* Combine the bits already present in the first byte with the
     * first bits of the data. */
    a.lbz(TMP5, ppc::Mem(TMP1));

    a.srdi(TMP4, bitdata, 56);
    a.srd(TMP4, TMP4, left_bit_offset);

    a.sld(TMP5, TMP5, left_bit_offset);
    a.clrrdi(TMP5, TMP5, 8);
    a.srd(TMP5, TMP5, left_bit_offset);

    a.or_(TMP5, TMP4, TMP5);

    a.stb(TMP5, ppc::Mem(TMP1));
    a.addi(TMP1, TMP1, imm(1));

    /* Discard the bits that were stored. */
    a.subfic(right_bit_offset, left_bit_offset, imm(8));
    a.sld(bitdata, bitdata, right_bit_offset);

    a.sub(num_bits, num_bits, right_bit_offset);
    a.cmpdi(num_bits, imm(0));
    a.ble(done);

    a.bind(loop);
    a.rotldi(bitdata, bitdata, 8);
    a.stb(bitdata, ppc::Mem(TMP1));
    a.addi(TMP1, TMP1, imm(1));
    a.addi(num_bits, num_bits, imm(-8));
    a.cmpdi(num_bits, imm(0));
    a.bgt(loop);

    a.bind(done);
    a.blr();
}

/*
 * In:
 *   ARG4 = Size of binary in bits.
 *   ARG5 = Extra words to allocate.
 *   ARG6 = Number of live X registers.
 *
 * Out:
 *   ARG1 = Allocated binary object.
 */

void BeamGlobalAssembler::emit_bs_init_bits_shared() {
    emit_enter_runtime_frame();

    load_erl_bits_state(ARG3);
    load_x_reg_array(ARG2);
    a.mr(ARG1, c_p);

    emit_enter_runtime<Update::eReductions | Update::eHeapAlloc |
                       Update::eXRegs>();

    runtime_call<Eterm (*)(Process *,
                           Eterm *,
                           struct erl_bits_state *,
                           Uint,
                           Uint,
                           unsigned),
                 beam_jit_bs_init_bits>();

    emit_leave_runtime<Update::eReductions | Update::eHeapAlloc |
                       Update::eXRegs>();

    emit_leave_runtime_frame();

    ERTS_CT_ASSERT(Support::is_uint_n<16>(ERTS_PSFLG_EXITING));
    a.lwz(TMP1, ppc::Mem(c_p, offsetof(Process, state.value)));
    a.andi_rc(LR_TMP, TMP1, imm(ERTS_PSFLG_EXITING));
    far_bne(labels[do_schedule]);

    a.blr();
}

/*
 * Here follows the bs_match instruction and friends.
 */

/* Reads `bits` bits, starting `bin_offset` bits into the data that `bin_base`
 * points to, into the most significant bits of `bitdata`. The bits below
 * them are undefined.
 *
 * Only the bytes that the segment spans are read, with the exception that
 * the byte before a segment of seven bytes may be read too.
 *
 * Clobbers LR_TMP, TMP2, TMP4, and TMP5. */
void BeamModuleAssembler::emit_read_bits(Uint bits,
                                         const ppc::Gp bin_base,
                                         const ppc::Gp bin_offset,
                                         const ppc::Gp bitdata) {
    Label shift = a.new_label();
    Label read_done = a.new_label();

    const ppc::Gp bin_byte_ptr = TMP2;
    const ppc::Gp bit_offset = TMP4;
    const ppc::Gp tmp = TMP5;

    const int num_partial = bits % 8;
    const int num_bytes = (bits + 7) / 8;

    ASSERT(1 <= bits && bits <= 64);

    /* Loads `size` bytes as a big-endian number. */
    auto load_bytes = [&](ppc::Gp to, int offset, int size) {
        if (size == 1) {
            a.lbz(to, ppc::Mem(bin_byte_ptr, offset));
            return;
        }

#if defined(WORDS_BIGENDIAN)
        switch (size) {
        case 2:
            a.lhz(to, ppc::Mem(bin_byte_ptr, offset));
            break;
        case 4:
            a.lwz(to, ppc::Mem(bin_byte_ptr, offset));
            break;
        case 8:
            load_mem(to, ppc::Mem(bin_byte_ptr, offset));
            break;
        }
#else
        a.li(LR_TMP, imm(offset));
        switch (size) {
        case 2:
            a.lhbrx(to, ppc::Mem(bin_byte_ptr, LR_TMP));
            break;
        case 4:
            a.lwbrx(to, ppc::Mem(bin_byte_ptr, LR_TMP));
            break;
        case 8:
            a.ldbrx(to, ppc::Mem(bin_byte_ptr, LR_TMP));
            break;
        }
#endif
    };

    a.srdi(bin_byte_ptr, bin_offset, 3);
    a.add(bin_byte_ptr, bin_base, bin_byte_ptr);

    /* CR0 tells whether the segment is byte-aligned. None of the
     * instructions that read the bytes below change it. */
    a.andi_rc(bit_offset, bin_offset, imm(7));

    /* The segment always spans at least `num_bytes` bytes. Read them into
     * the most significant bytes of the word. */
    if (num_bytes == 7) {
        load_bytes(bitdata, -1, 8);
        a.sldi(bitdata, bitdata, 8);
    } else {
        int num_read = 0;

        for (int size : {8, 4, 2, 1}) {
            if (num_bytes - num_read < size) {
                continue;
            }

            if (num_read == 0) {
                load_bytes(bitdata, 0, size);
            } else {
                load_bytes(tmp, num_read, size);
                a.sldi(bitdata, bitdata, 8 * size);
                a.or_(bitdata, bitdata, tmp);
            }

            num_read += size;
        }

        if (num_bytes < 8) {
            a.sldi(bitdata, bitdata, 64 - 8 * num_bytes);
        }
    }

    /* An unaligned segment may span one more byte. */
    if (num_partial == 0) {
        /* Byte-sized segment. If it is not byte-aligned, it always spans an
         * additional byte. */
        a.beq(read_done);
    } else if (num_partial > 1) {
        /* Test whether the last bits fit into the bytes already read. */
        a.cmpdi(bit_offset, imm(8 - num_partial));
        a.ble(shift);
    }

    if (num_partial != 1) {
        a.lbz(tmp, ppc::Mem(bin_byte_ptr, num_bytes));

        if (num_bytes < 8) {
            /* Place the additional byte right below the bytes already
             * read. */
            if (num_bytes < 7) {
                a.sldi(tmp, tmp, 56 - 8 * num_bytes);
            }
            a.or_(bitdata, bitdata, tmp);
        } else {
            /* The word is full, so make room for the additional bits by
             * shifting first. */
            a.sld(bitdata, bitdata, bit_offset);
            a.sld(tmp, tmp, bit_offset);
            a.srdi(tmp, tmp, 8);
            a.or_(bitdata, bitdata, tmp);

            if (bits == 64) {
                comment("simplified reading of 64-bit word");
            } else {
                a.b(read_done);
            }
        }
    }

    /* Shift the read data into the most significant bits of the
     * word. */
    a.bind(shift);
    if (bits != 64) {
        a.sld(bitdata, bitdata, bit_offset);
    }

    a.bind(read_done);
}

/* Reverses the order of the `bits / 8` least significant bytes in `src`. All
 * bits above them must be zero.
 *
 * There is no instruction that reverses the bytes of a register before
 * Power ISA 3.1.
 *
 * `dst` and `src` must be different registers. Clobbers LR_TMP and TMP4. */
void BeamModuleAssembler::emit_reverse_bytes(const ppc::Gp &dst,
                                             const ppc::Gp &src,
                                             Uint bits) {
    auto reverse_word = [&](ppc::Gp to, ppc::Gp from) {
        a.rlwinm(to, from, imm(8), imm(0), imm(31));
        a.rlwimi(to, from, imm(24), imm(0), imm(7));
        a.rlwimi(to, from, imm(24), imm(16), imm(23));
    };

    ASSERT(dst != src && bits % 8 == 0 && 8 <= bits && bits <= 64);

    switch (bits) {
    case 8:
        a.mr(dst, src);
        break;
    case 16:
        a.srdi(dst, src, 8);
        a.rlwimi(dst, src, imm(8), imm(16), imm(23));
        break;
    case 24:
        reverse_word(dst, src);
        a.srdi(dst, dst, 8);
        break;
    case 32:
        reverse_word(dst, src);
        break;
    default:
        reverse_word(dst, src);
        a.srdi(TMP4, src, 32);
        reverse_word(LR_TMP, TMP4);
        a.sldi(dst, dst, 32);
        a.or_(dst, dst, LR_TMP);
        if (bits < 64) {
            a.srdi(dst, dst, 64 - bits);
        }
        break;
    }
}

/* Extracts the `bits` bits that start `position` bits above the least
 * significant bit of `bitdata`.
 *
 * Clobbers LR_TMP and TMP1 through TMP5. */
void BeamModuleAssembler::emit_extract_integer(const ppc::Gp &bitdata,
                                               Uint flags,
                                               Uint position,
                                               Uint bits,
                                               const ArgRegister &Dst) {
    ppc::Gp data_reg = bitdata;
    auto dst = init_destination(Dst, TMP1);

    if (bits <= 8) {
        /* Endian does not matter for values that fit in a byte. */
        flags &= ~BSF_LITTLE;
    }

    const bool is_little = (flags & BSF_LITTLE) != 0;
    const bool is_signed = (flags & BSF_SIGNED) != 0;

    ERTS_CT_ASSERT(_TAG_IMMED1_SMALL == _TAG_IMMED1_MASK);

    if (bits == 0) {
        mov_imm(dst.reg, make_small(0));
        flush_var(dst);
        return;
    }

    /* Optimize extraction of big-endian segments that always fit in a
     * small. The value is moved straight to its place above the tag bits,
     * and the garbage that ends up in the tag bits is overwritten by the
     * tag. */
    if (bits < SMALL_BITS && !is_little) {
        if (is_signed) {
            if (position + bits < 64) {
                a.sldi(dst.reg, bitdata, 64 - position - bits);
                a.sradi(dst.reg, dst.reg, imm(64 - _TAG_IMMED1_SIZE - bits));
            } else {
                a.sradi(dst.reg, bitdata, imm(64 - _TAG_IMMED1_SIZE - bits));
            }
        } else {
            a.rldicl(dst.reg,
                     bitdata,
                     imm((64 + _TAG_IMMED1_SIZE - position) & 63),
                     imm(64 - _TAG_IMMED1_SIZE - bits));
        }

        a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));
        flush_var(dst);
        return;
    }

    Label big = a.new_label();
    Label done = a.new_label();
    Uint num_partial = bits % 8;
    Uint num_complete = 8 * (bits / 8);

    if (bits < 64) {
        if (is_signed && !is_little) {
            data_reg = TMP2;
            if (position + bits < 64) {
                a.sldi(data_reg, bitdata, 64 - position - bits);
                a.sradi(data_reg, data_reg, imm(64 - bits));
            } else {
                a.sradi(data_reg, bitdata, imm(64 - bits));
            }
        } else {
            /* The bytes cannot be reversed in place, so extract a
             * little-endian segment to another register. */
            data_reg = is_little ? TMP3 : TMP2;
            a.rldicl(data_reg,
                     bitdata,
                     imm((64 - position) & 63),
                     imm(64 - bits));
        }
    }

    /* If this segment is little-endian, reverse endianness. */
    if (is_little) {
        comment("reverse endian for a little-endian segment");
        if (num_partial == 0) {
            emit_reverse_bytes(TMP2, data_reg, bits);
        } else {
            /* The partial byte holds the most significant bits. */
            a.rldic(TMP5,
                    data_reg,
                    imm(num_complete),
                    imm(64 - num_complete - num_partial));
            a.rldicl(TMP3,
                     data_reg,
                     imm(64 - num_partial),
                     imm(64 - num_complete));
            emit_reverse_bytes(TMP2, TMP3, num_complete);
            a.or_(TMP2, TMP2, TMP5);
        }
        data_reg = TMP2;

        /* Sign-extend the number if the segment is signed. */
        if (is_signed && bits < 64) {
            comment("sign extend extracted value");
            if (bits == 16) {
                a.extsh(TMP2, TMP2);
            } else if (bits == 32) {
                a.extsw(TMP2, TMP2);
            } else {
                a.sldi(TMP2, TMP2, 64 - bits);
                a.sradi(TMP2, TMP2, imm(64 - bits));
            }
        }
    }

    /* Handle segments whose values might not fit in a small integer. */
    if (bits >= SMALL_BITS) {
        comment("test whether it fits in a small");
        if (is_signed) {
            /* Signed segment. It fits if the bits above the value are all
             * equal to its sign bit, that is, if they are 0 or -1. */
            a.sradi(TMP3, data_reg, imm(SMALL_BITS - 1));
            a.addi(TMP3, TMP3, imm(1));
            a.cmpldi(TMP3, imm(1));
            a.bgt(big);
        } else {
            /* Unsigned segment. */
            a.rldicl_rc(TMP3,
                        data_reg,
                        imm(64 - (SMALL_BITS - 1)),
                        imm(SMALL_BITS - 1));
            a.bne(big);
        }
    }

    /* Tag and store the extracted small integer. */
    a.sldi(dst.reg, data_reg, _TAG_IMMED1_SIZE);
    a.ori(dst.reg, dst.reg, imm(_TAG_IMMED1_SMALL));

    if (bits >= SMALL_BITS) {
        a.b(done);
    }

    /* Handle a bignum (up to 64 bits). */
    a.bind(big);
    if (bits >= SMALL_BITS) {
        comment("store extracted integer as a bignum");
        a.addi(dst.reg, HTOP, imm(TAG_PRIMARY_BOXED));
        mov_imm(TMP3, make_pos_bignum_header(1));
        if (is_signed) {
            Label store = a.new_label();

            if (data_reg != TMP2) {
                a.mr(TMP2, data_reg);
                data_reg = TMP2;
            }
            a.cmpdi(TMP2, imm(0));
            a.bge(store);

            mov_imm(TMP3, make_neg_bignum_header(1));
            a.neg(TMP2, TMP2);

            a.bind(store);
        }

        a.std(TMP3, ppc::Mem(HTOP, 0));
        a.std(data_reg, ppc::Mem(HTOP, sizeof(Eterm)));
        a.addi(HTOP, HTOP, imm(sizeof(Eterm[2])));
    }

    a.bind(done);
    flush_var(dst);
}

/* Extracts the `bits` bits right below the bit that is `position` bits
 * above the least significant bit of `bitdata`.
 *
 * Clobbers LR_TMP and TMP1 through TMP4. */
void BeamModuleAssembler::emit_extract_bitstring(const ppc::Gp bitdata,
                                                 Uint position,
                                                 Uint bits,
                                                 const ArgRegister &Dst) {
    auto dst = init_destination(Dst, TMP1);

    a.addi(dst.reg, HTOP, imm(TAG_PRIMARY_BOXED));
    mov_imm(TMP2, header_heap_bits(bits));
    mov_imm(TMP3, bits);
    a.std(TMP2, ppc::Mem(HTOP, 0));
    a.std(TMP3, ppc::Mem(HTOP, sizeof(Eterm)));

    if (bits > 0) {
        ppc::Gp data_reg = bitdata;

        /* Move the bits to the most significant bits of the word. */
        if (position != 64) {
            data_reg = TMP4;
            a.rotldi(data_reg, bitdata, 64 - position);
        }

        /* The first bit of the bitstring is the most significant bit of
         * its first byte. */
#if defined(WORDS_BIGENDIAN)
        a.std(data_reg, ppc::Mem(HTOP, sizeof(Eterm[2])));
#else
        a.li(LR_TMP, imm(sizeof(Eterm[2])));
        a.stdbrx(data_reg, ppc::Mem(HTOP, LR_TMP));
#endif
        a.addi(HTOP, HTOP, imm(sizeof(Eterm[3])));
    } else {
        a.addi(HTOP, HTOP, imm(sizeof(Eterm[2])));
    }

    flush_var(dst);
}

void BeamModuleAssembler::emit_i_bs_match(ArgLabel const &Fail,
                                          ArgRegister const &Ctx,
                                          Span<const ArgVal> const &List) {
    emit_i_bs_match_test_heap(Fail, Ctx, ArgWord(0), ArgWord(0), List);
}

void BeamModuleAssembler::emit_i_bs_match_test_heap(
        ArgLabel const &Fail,
        ArgRegister const &Ctx,
        ArgWord const &Need,
        ArgWord const &Live,
        Span<const ArgVal> const &List) {
    const int orig_offset = offsetof(ErlSubBits, orig);
    const int base_offset = offsetof(ErlSubBits, base_flags);
    const int start_offset = offsetof(ErlSubBits, start);
    const int end_offset = offsetof(ErlSubBits, end);

    std::vector<BsmSegment> segments;

    segments = beam_jit_bsm_init(beam, List);
    segments = beam_jit_opt_bsm_segments(segments, Need, Live);

    /* The match context is untagged into this register before its fields
     * are accessed, as the displacement of a 64-bit load or store must be a
     * multiple of four. */
    const ppc::Gp ctx_ptr = TMP1;
    const ppc::Gp bin_base = ARG2;
    const ppc::Gp bin_position = ARG3;
    const ppc::Gp bin_size = ARG4;
    const ppc::Gp bitdata = ARG8;
    bool position_is_valid = false;
    Uint offset_in_bitdata = 0;

    ERTS_CT_ASSERT(ERL_SUB_BITS_FLAG_MASK == 3 && TAG_PTR_MASK__ == 7);

    for (auto seg : segments) {
        switch (seg.action) {
        case BsmSegment::action::ENSURE_AT_LEAST: {
            comment("ensure_at_least %ld %ld", seg.size, seg.unit);
            auto ctx_reg = load_source(Ctx, TMP1);
            auto stride = seg.size;
            auto unit = seg.unit;

            emit_untag_ptr(ctx_ptr, ctx_reg.reg);
            a.ld(bin_position, ppc::Mem(ctx_ptr, start_offset));
            a.ld(bin_size, ppc::Mem(ctx_ptr, end_offset));
            a.sub(TMP5, bin_size, bin_position);
            if (stride != 0) {
                cmpl(TMP5, stride);
                a.blt(resolve_beam_label(Fail, disp32K));
            }

            if (unit != 1) {
                if (stride % unit != 0) {
                    sub(TMP5, TMP5, stride);
                }

                if ((unit & (unit - 1)) != 0) {
                    mov_imm(TMP4, unit);

                    a.divdu(TMP3, TMP5, TMP4);
                    a.mulld(TMP3, TMP3, TMP4);

                    a.cmpd(TMP5, TMP3);
                    a.bne(resolve_beam_label(Fail, disp32K));
                } else {
                    a.rldicl_rc(LR_TMP,
                                TMP5,
                                imm(0),
                                imm(64 - Support::ctz<Eterm>(unit)));
                    a.bne(resolve_beam_label(Fail, disp32K));
                }
            }

            position_is_valid = true;
            break;
        }
        case BsmSegment::action::ENSURE_EXACTLY: {
            comment("ensure_exactly %ld", seg.size);
            auto ctx_reg = load_source(Ctx, TMP1);
            auto size = seg.size;

            emit_untag_ptr(ctx_ptr, ctx_reg.reg);
            a.ld(bin_position, ppc::Mem(ctx_ptr, start_offset));
            a.ld(TMP3, ppc::Mem(ctx_ptr, end_offset));
            a.sub(TMP3, TMP3, bin_position);
            cmpl(TMP3, size);
            a.bne(resolve_beam_label(Fail, disp32K));
            position_is_valid = true;
            break;
        }
        case BsmSegment::action::EQ: {
            ppc::Gp cmp_reg = TMP1;
            Uint value = seg.unit;
            comment("=:= %ld %ld", seg.size, seg.unit);

            offset_in_bitdata -= seg.size;

            if (seg.size == 0) {
                mov_imm(cmp_reg, 0);
            } else if (seg.size == 64) {
                cmp_reg = bitdata;
            } else {
                a.rldicl(cmp_reg,
                         bitdata,
                         imm((64 - offset_in_bitdata) & 63),
                         imm(64 - seg.size));
            }

            if (seg.size == 32) {
                /* The extracted value is zero-extended. */
                value &= 0xFFFFFFFFUL;
            }

            cmpl(cmp_reg, value);
            a.bne(resolve_beam_label(Fail, disp32K));
            break;
        }
        case BsmSegment::action::TEST_HEAP: {
            comment("test_heap %ld", seg.size);
            emit_gc_test(ArgWord(0), ArgWord(seg.size), seg.live);
            position_is_valid = false;
            break;
        }
        case BsmSegment::action::READ: {
            comment("read %ld", seg.size);
            offset_in_bitdata = 64;
            if (seg.size == 0) {
                comment("(nothing to do)");
            } else {
                auto ctx = load_source(Ctx, TMP1);

                emit_untag_ptr(ctx_ptr, ctx.reg);

                if (!position_is_valid) {
                    a.ld(bin_position, ppc::Mem(ctx_ptr, start_offset));
                    position_is_valid = true;
                }

                a.ld(bin_base, ppc::Mem(ctx_ptr, base_offset));
                a.clrrdi(bin_base, bin_base, 2);

                emit_read_bits(seg.size, bin_base, bin_position, bitdata);

                a.addi(bin_position, bin_position, imm(seg.size));
                a.std(bin_position, ppc::Mem(ctx_ptr, start_offset));
            }
            break;
        }
        case BsmSegment::action::EXTRACT_BITSTRING: {
            auto bits = seg.size;
            auto Dst = seg.dst;

            comment("extract binary %ld", bits);
            emit_extract_bitstring(bitdata, offset_in_bitdata, bits, Dst);
            offset_in_bitdata -= bits;
            break;
        }
        case BsmSegment::action::EXTRACT_INTEGER: {
            auto bits = seg.size;
            auto flags = seg.flags;
            auto Dst = seg.dst;

            comment("extract integer %ld", bits);
            offset_in_bitdata -= bits;
            emit_extract_integer(bitdata, flags, offset_in_bitdata, bits, Dst);
            break;
        }
        case BsmSegment::action::READ_INTEGER: {
            auto bits = seg.size;
            auto flags = seg.flags;
            auto Dst = seg.dst;

            comment("read integer %ld", seg.size);
            if (seg.size == 0) {
                comment("(nothing to read)");
            } else {
                auto ctx = load_source(Ctx, TMP1);

                emit_untag_ptr(ctx_ptr, ctx.reg);

                if (!position_is_valid) {
                    a.ld(bin_position, ppc::Mem(ctx_ptr, start_offset));
                    position_is_valid = true;
                }

                a.ld(bin_base, ppc::Mem(ctx_ptr, base_offset));
                a.clrrdi(bin_base, bin_base, 2);

                emit_read_bits(seg.size, bin_base, bin_position, bitdata);

                a.addi(bin_position, bin_position, imm(seg.size));
                a.std(bin_position, ppc::Mem(ctx_ptr, start_offset));
            }

            offset_in_bitdata = 64 - bits;
            emit_extract_integer(bitdata, flags, offset_in_bitdata, bits, Dst);
            break;
        }
        case BsmSegment::action::GET_INTEGER: {
            /* Match integer segments with more than 64 bits. */
            Uint live = seg.live.as<ArgWord>().get();
            Uint flags = seg.flags;
            auto bits = seg.size;
            auto Dst = seg.dst;

            comment("get integer %ld", bits);
            auto ctx = load_source(Ctx, TMP1);

            a.mr(ARG1, c_p);
            mov_imm(ARG2, bits);
            mov_imm(ARG3, flags);
            emit_untag_ptr(ARG4, ctx.reg);

            emit_enter_runtime<Update::eHeapOnlyAlloc>(live);
            runtime_call<Eterm (*)(Process *, Uint, unsigned, ErlSubBits *),
                         erts_bs_get_integer_2>();
            emit_leave_runtime<Update::eHeapOnlyAlloc>(live);

            mov_arg(Dst, ARG1);

            position_is_valid = false;
            break;
        }
        case BsmSegment::action::GET_BITSTRING: {
            auto Live = seg.live;
            ERTS_ASSERT(seg.size > 64);
            comment("get binary %ld", seg.size);
            auto ctx = load_source(Ctx, TMP1);

            emit_untag_ptr(ctx_ptr, ctx.reg);
            a.ld(ARG5, ppc::Mem(ctx_ptr, start_offset));

            lea(ARG1, ppc::Mem(c_p, offsetof(Process, htop)));
            if (seg.size <= ERL_ONHEAP_BITS_LIMIT) {
                comment("skipped setting registers not used for heap binary");
            } else {
                a.ld(ARG2, ppc::Mem(ctx_ptr, orig_offset));
                a.clrrdi(ARG3, ARG2, 3);
                a.clrldi(ARG2, ARG2, 61);
            }
            a.ld(ARG4, ppc::Mem(ctx_ptr, base_offset));
            a.clrrdi(ARG4, ARG4, 2);
            mov_imm(ARG6, seg.size);
            a.add(TMP2, ARG5, ARG6);
            a.std(TMP2, ppc::Mem(ctx_ptr, start_offset));

            emit_enter_runtime<Update::eHeapOnlyAlloc>(
                    Live.as<ArgWord>().get());

            runtime_call<Eterm (*)(Eterm **,
                                   Eterm,
                                   const BinRef *,
                                   const byte *,
                                   Uint,
                                   Uint),
                         erts_build_sub_bitstring>();

            emit_leave_runtime<Update::eHeapOnlyAlloc>(
                    Live.as<ArgWord>().get());

            mov_arg(seg.dst, ARG1);
            position_is_valid = false;
            break;
        }
        case BsmSegment::action::GET_TAIL: {
            comment("get_tail");

            mov_arg(ARG1, Ctx);
            fragment_call(ga->get_bs_get_tail_shared());
            mov_arg(seg.dst, ARG1);
            position_is_valid = false;
            break;
        }
        case BsmSegment::action::SKIP: {
            comment("skip %ld", seg.size);
            auto ctx = load_source(Ctx, TMP1);

            emit_untag_ptr(ctx_ptr, ctx.reg);
            if (!position_is_valid) {
                a.ld(bin_position, ppc::Mem(ctx_ptr, start_offset));
                position_is_valid = true;
            }
            add(bin_position, bin_position, seg.size);
            a.std(bin_position, ppc::Mem(ctx_ptr, start_offset));
            break;
        }
        case BsmSegment::action::DROP:
            auto bits = seg.size;
            comment("drop %ld", bits);
            offset_in_bitdata -= bits;
            break;
        }

        /* A match with many segments can be long enough for the labels of
         * earlier instructions to go out of range. */
        check_pending_stubs();
    }
}
