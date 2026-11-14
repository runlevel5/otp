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
