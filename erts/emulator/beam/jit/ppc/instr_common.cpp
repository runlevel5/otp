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

void BeamModuleAssembler::emit_get_list(const ArgRegister &Src,
                                        const ArgRegister &Hd,
                                        const ArgRegister &Tl) {
    auto src = load_source(Src);
    auto hd = init_destination(Hd, TMP2);
    auto tl = init_destination(Tl, TMP3);

    if (hd.reg == tl.reg) {
        /* Produce the same result as the interpreter. */
        ppc::Gp cons_ptr = emit_ptr_val(TMP1, src.reg);

        load_mem(tl.reg, getCDRRef(cons_ptr));
        flush_var(tl);
    } else {
        preserve_cache([&]() {
#if defined(TAG_LITERAL_PTR)
            /* The pointer may carry the literal tag as well, so we get rid
             * of all tag bits before using it. */
            emit_untag_ptr(TMP1, src.reg);
            a.ld(tl.reg, ppc::Mem(TMP1, sizeof(Eterm)));
            a.ld(hd.reg, ppc::Mem(TMP1));
#else
            /* The tail is likely to be the next address that is followed, so
             * it is loaded straight from the tagged pointer with the
             * displacement in an index register, which does not delay it.
             * The head is loaded through an untagged copy of the pointer,
             * which is as fast as anything when the value is not an
             * address.
             *
             * Note that the tagged pointer is not used after the tail has
             * been loaded, as the two may be in the same register. */
            a.li(SUPER_TMP, imm(sizeof(Eterm) - TAG_PRIMARY_LIST));
            a.addi(TMP1, src.reg, imm(-TAG_PRIMARY_LIST));
            a.ldx(tl.reg, ppc::Mem(src.reg, SUPER_TMP));
            a.ld(hd.reg, ppc::Mem(TMP1));
#endif
        });
        flush_vars(hd, tl);
    }
}

void BeamModuleAssembler::emit_get_hd(const ArgRegister &Src,
                                      const ArgRegister &Hd) {
    auto src = load_source(Src);
    auto hd = init_destination(Hd, TMP2);
    ppc::Gp cons_ptr = emit_ptr_val(TMP1, src.reg);

    load_mem(hd.reg, getCARRef(cons_ptr));
    flush_var(hd);
}

void BeamModuleAssembler::emit_get_tl(const ArgRegister &Src,
                                      const ArgRegister &Tl) {
    auto src = load_source(Src);
    auto tl = init_destination(Tl, TMP2);
    ppc::Gp cons_ptr = emit_ptr_val(TMP1, src.reg);

    load_mem(tl.reg, getCDRRef(cons_ptr));
    flush_var(tl);
}

void BeamModuleAssembler::emit_i_get(const ArgSource &Src,
                                     const ArgRegister &Dst) {
    mov_arg(ARG2, Src);

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm), erts_pd_hash_get>();

    emit_leave_runtime();

    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_i_get_hash(const ArgConstant &Src,
                                          const ArgWord &Hash,
                                          const ArgRegister &Dst) {
    mov_arg(ARG2, Hash);
    mov_arg(ARG3, Src);

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, erts_ihash_t, Eterm),
                 erts_pd_hash_get_with_hx>();

    emit_leave_runtime();

    mov_arg(Dst, ARG1);
}

/* Store the untagged pointer to a tuple in ARG1. */
void BeamModuleAssembler::emit_load_tuple_ptr(const ArgSource &Src) {
    auto src = load_source(Src);

    untag_ptr_preserve_cache(ARG1, src.reg);
}

#ifdef DEBUG
/* Emit an assertion to ensure that tuple_reg points into the same
 * tuple as Src. */
void BeamModuleAssembler::emit_tuple_assertion(const ArgSource &Src,
                                               ppc::Gp tuple_reg) {
    Label ok = a.new_label(), fatal = a.new_label();
    ASSERT(tuple_reg != TMP1);
    mov_arg(TMP1, Src);
    emit_is_boxed(fatal, TMP1);
    emit_untag_ptr(TMP1, TMP1);
    a.cmpd(TMP1, tuple_reg);
    a.beq(ok);

    a.bind(fatal);
    udf(0xaaaa);
    a.bind(ok);
}
#endif

/* Fetch an element from the tuple pointed to by the untagged pointer
 * in ARG1. */
void BeamModuleAssembler::emit_i_get_tuple_element(const ArgSource &Src,
                                                   const ArgWord &Element,
                                                   const ArgRegister &Dst) {
#ifdef DEBUG
    emit_tuple_assertion(Src, ARG1);
#endif

    auto dst = init_destination(Dst, TMP1);
    load_mem(dst.reg, ppc::Mem(ARG1, Element.get()));
    flush_var(dst);
}

void BeamModuleAssembler::emit_get_tuple_element_swap(
        const ArgSource &Src,
        const ArgWord &Element,
        const ArgRegister &Dst,
        const ArgRegister &OtherDst) {
#ifdef DEBUG
    emit_tuple_assertion(Src, ARG1);
#endif

    mov_arg(Dst, OtherDst);

    auto dst = init_destination(OtherDst, TMP1);
    load_mem(dst.reg, ppc::Mem(ARG1, Element.get()));
    flush_var(dst);
}

/* Fetch two consecutive tuple elements from the tuple pointed to by
 * the boxed pointer in ARG1. */
void BeamModuleAssembler::emit_get_two_tuple_elements(const ArgSource &Src,
                                                      const ArgWord &Element,
                                                      const ArgRegister &Dst1,
                                                      const ArgRegister &Dst2) {
#ifdef DEBUG
    emit_tuple_assertion(Src, ARG1);
#endif

    auto dst1 = init_destination(Dst1, TMP1);
    auto dst2 = init_destination(Dst2, TMP2);

    load_mem(dst1.reg, ppc::Mem(ARG1, Element.get()));
    load_mem(dst2.reg, ppc::Mem(ARG1, Element.get() + sizeof(Eterm)));
    flush_vars(dst1, dst2);
}

void BeamModuleAssembler::emit_init_yregs(const ArgWord &Size,
                                          const Span<const ArgVal> &args) {
    unsigned count = Size.get();
    ASSERT(count == args.size());

    if (count > 0) {
        mov_imm(TMP1, NIL);
    }

    for (unsigned i = 0; i < count; i++) {
        a.std(TMP1, getYRef(args[i].as<ArgYRegister>().get()));
    }
}

void BeamModuleAssembler::emit_trim(const ArgWord &Words,
                                    const ArgWord &Remaining) {
    trim_preserve_cache(Words);
}

void BeamModuleAssembler::emit_i_move(const ArgSource &Src,
                                      const ArgRegister &Dst) {
    mov_arg(Dst, Src);
}

void BeamModuleAssembler::emit_move_two_trim(const ArgYRegister &Src1,
                                             const ArgRegister &Dst1,
                                             const ArgYRegister &Src2,
                                             const ArgRegister &Dst2,
                                             const ArgWord &Words) {
    auto dst1 = init_destination(Dst1, TMP1);
    auto dst2 = init_destination(Dst2, TMP2);

    a.ld(dst1.reg, getArgRef(Src1));
    a.ld(dst2.reg, getArgRef(Src2));
    flush_vars(dst1, dst2);

    trim_preserve_cache(Words);
}

void BeamModuleAssembler::emit_move_trim(const ArgSource &Src,
                                         const ArgRegister &Dst,
                                         const ArgWord &Words) {
    ASSERT(Words.get() <= 1023);

    mov_arg(Dst, Src);
    trim_preserve_cache(Words);
}

void BeamModuleAssembler::emit_store_two_values(const ArgSource &Src1,
                                                const ArgRegister &Dst1,
                                                const ArgSource &Src2,
                                                const ArgRegister &Dst2) {
    auto [src1, src2] = load_sources(Src1, TMP1, Src2, TMP2);
    auto dst1 = init_destination(Dst1, src1.reg);
    auto dst2 = init_destination(Dst2, src2.reg);

    ASSERT(!isRegisterBacked(Dst1));
    ASSERT(!isRegisterBacked(Dst2));

    flush_vars(dst1, dst2);
}

void BeamModuleAssembler::emit_load_two_xregs(const ArgRegister &Src1,
                                              const ArgXRegister &Dst1,
                                              const ArgRegister &Src2,
                                              const ArgXRegister &Dst2) {
    auto dst1 = init_destination(Dst1, TMP1);
    auto dst2 = init_destination(Dst2, TMP2);

    ASSERT(!isRegisterBacked(Src1));
    ASSERT(!isRegisterBacked(Src2));

    a.ld(dst1.reg, getArgRef(Src1));
    a.ld(dst2.reg, getArgRef(Src2));
    flush_vars(dst1, dst2);
}

void BeamModuleAssembler::emit_swap(const ArgRegister &R1,
                                    const ArgRegister &R2) {
    if (isRegisterBacked(R1)) {
        /* The temporary register is not used as R1 is in a register. */
        auto r1 = load_source(R1, SUPER_TMP);
        mov_arg(TMP1, R2);
        mov_arg(R2, R1);
        a.mr(r1.reg, TMP1);
    } else if (isRegisterBacked(R2)) {
        return emit_swap(R2, R1);
    } else {
        /* Both BEAM registers are stored in memory. */
        auto [r1, r2] = load_sources(R1, TMP1, R2, TMP2);
        auto dst1 = init_destination(R2, r1.reg);
        auto dst2 = init_destination(R1, r2.reg);
        flush_vars(dst1, dst2);
    }
}

void BeamModuleAssembler::emit_swap2(const ArgRegister &R1,
                                     const ArgRegister &R2,
                                     const ArgRegister &R3) {
    auto [arg1, arg2] = load_sources(R1, TMP1, R2, TMP2);
    auto arg3 = load_source(R3, TMP3);

    mov_var(TMP4, arg1);
    mov_var(arg1, arg2);
    mov_var(arg2, arg3);
    mov_var(arg3, TMP4);

    flush_vars(arg1, arg2, arg3);
}

void BeamModuleAssembler::emit_swap3(const ArgRegister &R1,
                                     const ArgRegister &R2,
                                     const ArgRegister &R3,
                                     const ArgRegister &R4) {
    auto [arg1, arg2] = load_sources(R1, TMP1, R2, TMP2);
    auto [arg3, arg4] = load_sources(R3, TMP3, R4, TMP4);

    mov_var(TMP5, arg1);
    mov_var(arg1, arg2);
    mov_var(arg2, arg3);
    mov_var(arg3, arg4);
    mov_var(arg4, TMP5);

    flush_vars(arg1, arg2, arg3);
    flush_var(arg4);
}

void BeamModuleAssembler::emit_swap4(const ArgRegister &R1,
                                     const ArgRegister &R2,
                                     const ArgRegister &R3,
                                     const ArgRegister &R4,
                                     const ArgRegister &R5) {
    auto [arg1, arg2] = load_sources(R1, TMP1, R2, TMP2);
    auto [arg3, arg4] = load_sources(R3, TMP3, R4, TMP4);
    auto arg5 = load_source(R5, TMP5);

    mov_var(TMP6, arg1);
    mov_var(arg1, arg2);
    mov_var(arg2, arg3);
    mov_var(arg3, arg4);
    mov_var(arg4, arg5);
    mov_var(arg5, TMP6);

    flush_vars(arg1, arg2, arg3);
    flush_vars(arg4, arg5);
}

void BeamModuleAssembler::emit_node(const ArgRegister &Dst) {
    load_constant(TMP1, &erts_this_node);
    a.ld(TMP1, ppc::Mem(TMP1));
    mov_arg(Dst, ppc::Mem(TMP1, offsetof(ErlNode, sysname)));
}

void BeamModuleAssembler::emit_put_list(const ArgSource &Hd,
                                        const ArgSource &Tl,
                                        const ArgRegister &Dst) {
    auto [hd, tl] = load_sources(Hd, TMP1, Tl, TMP2);
    auto hd_reg = hd.reg;
    auto tl_reg = tl.reg;
    auto dst = init_destination(Dst, TMP3);

    preserve_cache([&]() {
        a.std(hd_reg, ppc::Mem(HTOP));
        a.std(tl_reg, ppc::Mem(HTOP, sizeof(Eterm)));
        a.addi(dst.reg, HTOP, imm(TAG_PRIMARY_LIST));
        a.addi(HTOP, HTOP, imm(sizeof(Eterm[2])));
    });

    flush_var(dst);
}

void BeamModuleAssembler::emit_put_list_deallocate(const ArgSource &Hd,
                                                   const ArgSource &Tl,
                                                   const ArgRegister &Dst,
                                                   const ArgWord &Deallocate) {
    Sint dealloc = Deallocate.get() * sizeof(Eterm);
    auto dst = init_destination(Dst, TMP3);

    ASSERT(dealloc < MAX_REG * sizeof(Eterm));

    auto [hd, tl] = load_sources(Hd, TMP1, Tl, TMP2);

    a.std(hd.reg, ppc::Mem(HTOP));
    a.std(tl.reg, ppc::Mem(HTOP, sizeof(Eterm)));
    a.addi(dst.reg, HTOP, imm(TAG_PRIMARY_LIST));
    a.addi(HTOP, HTOP, imm(sizeof(Eterm[2])));

    flush_var(dst);

    if (dealloc > 0) {
        add(E, E, dealloc);
    }
}

void BeamModuleAssembler::emit_put_list2(const ArgSource &Hd1,
                                         const ArgSource &Hd2,
                                         const ArgSource &Tl,
                                         const ArgRegister &Dst) {
    auto [hd1, hd2] = load_sources(Hd1, TMP1, Hd2, TMP2);
    auto tl = load_source(Tl, TMP3);
    auto dst = init_destination(Dst, TMP4);

    /* All sources are stored before the destination is written, as it may
     * be in the same register as one of them. */
    a.std(hd1.reg, ppc::Mem(HTOP));
    a.std(tl.reg, ppc::Mem(HTOP, sizeof(Eterm)));
    a.std(hd2.reg, ppc::Mem(HTOP, sizeof(Eterm[2])));

    a.addi(dst.reg, HTOP, imm(TAG_PRIMARY_LIST));
    a.std(dst.reg, ppc::Mem(HTOP, sizeof(Eterm[3])));
    a.addi(dst.reg, HTOP, imm(sizeof(Eterm[2]) + TAG_PRIMARY_LIST));
    a.addi(HTOP, HTOP, imm(sizeof(Eterm[4])));

    flush_var(dst);
}

void BeamModuleAssembler::emit_put_tuple2(const ArgRegister &Dst,
                                          const ArgWord &Arity,
                                          const Span<const ArgVal> &args) {
    ASSERT(arityval(Arity.get()) == args.size());

    std::vector<ArgVal> data;
    data.reserve(args.size() + 1);
    data.push_back(Arity);
    data.insert(data.end(), std::begin(args), std::end(args));

    size_t size = data.size();
    ImmedRegCache<12> values(*this,
                             ARG3,
                             ARG4,
                             ARG5,
                             ARG6,
                             ARG7,
                             ARG8,
                             TMP1,
                             TMP2,
                             TMP3,
                             TMP4,
                             TMP5,
                             TMP6);

    /* Number of bytes that have been written but not yet added to HTOP. */
    ssize_t offset = 0;

    for (size_t i = 0; i < size; i++) {
        ppc::Gp reg;

        if ((i % 128) == 0) {
            check_pending_stubs();
        }

        if (data[i].isImmed()) {
            reg = values.load_value(data[i].as<ArgImmed>().get());
        } else {
            reg = load_source(data[i], ARG1).reg;
        }

        if (offset + (ssize_t)sizeof(Eterm) > MAX_DISPLACEMENT) {
            add(HTOP, HTOP, offset);
            offset = 0;
        }

        a.std(reg, ppc::Mem(HTOP, offset));
        offset += sizeof(Eterm);
    }

    add(HTOP, HTOP, offset);

    auto ptr = init_destination(Dst, TMP1);
    sub(ptr.reg, HTOP, size * sizeof(Eterm) - TAG_PRIMARY_BOXED);
    flush_var(ptr);
}

void BeamModuleAssembler::emit_self(const ArgRegister &Dst) {
    mov_arg(Dst, ppc::Mem(c_p, offsetof(Process, common.id)));
}

/* Copies `count` words from `from_offset` bytes past `from` to `to_offset`
 * bytes past `to`. Neither `from` nor `to` is changed, as there is no
 * addressing mode that increments the base register after the access.
 *
 * Clobbers LR_TMP, SUPER_TMP, TMP5, TMP6, and CR0. */
void BeamModuleAssembler::emit_copy_words(ppc::Gp from,
                                          ssize_t from_offset,
                                          ppc::Gp to,
                                          ssize_t to_offset,
                                          size_t count) {
    const ssize_t size = count * sizeof(Eterm);

    check_pending_stubs();

    /* Copy the words inline if we can, otherwise use a loop. */
    if (count <= 16 && Support::is_int_n<16>(from_offset + size) &&
        Support::is_int_n<16>(to_offset + size)) {
        for (size_t i = 0; i < count; i++) {
            /* Alternate between two registers to let the copies overlap. */
            const ppc::Gp tmp = (i & 1) ? SUPER_TMP : LR_TMP;
            const ssize_t offset = i * sizeof(Eterm);

            a.ld(tmp, ppc::Mem(from, from_offset + offset));
            a.std(tmp, ppc::Mem(to, to_offset + offset));
        }
    } else {
        Label copy_next = a.new_label();

        add(TMP5, from, from_offset - (ssize_t)sizeof(Eterm));
        add(TMP6, to, to_offset - (ssize_t)sizeof(Eterm));
        mov_imm(LR_TMP, count);

        a.bind(copy_next);
        {
            a.ldu(SUPER_TMP, ppc::Mem(TMP5, sizeof(Eterm)));
            a.stdu(SUPER_TMP, ppc::Mem(TMP6, sizeof(Eterm)));
            a.addic_rc(LR_TMP, LR_TMP, imm(-1));
            a.bne(copy_next);
        }
    }
}

void BeamModuleAssembler::emit_update_record(
        const ArgAtom &Hint,
        const ArgWord &TupleSize,
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &UpdateCount,
        const Span<const ArgVal> &updates) {
    const size_t size_on_heap = TupleSize.get() + 1;
    emit_update_any_record(Hint, size_on_heap, Src, Dst, UpdateCount, updates);
}

/* Update a tuple, a tuple record, or a native record. */
void BeamModuleAssembler::emit_update_any_record(
        const ArgAtom &Hint,
        const size_t size_on_heap,
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &UpdateCount,
        const Span<const ArgVal> &updates) {
    Label next = a.new_label();

    ASSERT(UpdateCount.get() == updates.size());
    ASSERT((UpdateCount.get() % 2) == 0);

    ASSERT(size_on_heap > 2);

    auto destination = init_destination(Dst, ARG1);
    auto src = load_source(Src, ARG2);

    ppc::Gp untagged_src = ARG3;
    emit_untag_ptr(untagged_src, src.reg);

    /* Setting a field to the same value is pretty common, so we'll check for
     * that since it's vastly cheaper than copying if we're right, and doesn't
     * cost much if we're wrong. */
    if (Hint.get() == am_reuse && updates.size() == 2) {
        const auto next_index = updates[0].as<ArgWord>().get();
        const auto &next_value = updates[1].as<ArgSource>();

        load_mem(TMP1, ppc::Mem(untagged_src, next_index * sizeof(Eterm)));
        cmp_arg(TMP1, next_value);

        if (destination.reg != src.reg) {
            a.isel(destination.reg, src.reg, destination.reg, ppc::cr0.eq());
        }
        a.beq(next);
    }

    /* The new tuple is written at fixed offsets from HTOP, which is only
     * bumped once the tuple is complete. */
    size_t copy_index = 0;

    for (size_t i = 0; i < updates.size(); i += 2) {
        const auto next_index = updates[i].as<ArgWord>().get();
        const auto &next_value = updates[i + 1].as<ArgSource>();

        ASSERT(next_index > 0 && next_index >= copy_index);

        emit_copy_words(untagged_src,
                        copy_index * sizeof(Eterm),
                        HTOP,
                        copy_index * sizeof(Eterm),
                        next_index - copy_index);

        auto value = load_source(next_value, TMP1);
        store_mem(value.reg, ppc::Mem(HTOP, next_index * sizeof(Eterm)));

        copy_index = next_index + 1;
    }

    emit_copy_words(untagged_src,
                    copy_index * sizeof(Eterm),
                    HTOP,
                    copy_index * sizeof(Eterm),
                    size_on_heap - copy_index);

    a.addi(destination.reg, HTOP, imm(TAG_PRIMARY_BOXED));
    add(HTOP, HTOP, size_on_heap * sizeof(Eterm));

    a.bind(next);
    flush_var(destination);
}

void BeamModuleAssembler::emit_update_record_in_place(
        const ArgWord &TupleSize,
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &UpdateCount,
        const Span<const ArgVal> &updates) {
    bool all_safe = true;
    ArgSource maybe_immediate = ArgNil();
    const size_t size_on_heap = TupleSize.get() + 1;

    ASSERT(UpdateCount.get() == updates.size());
    ASSERT((UpdateCount.get() % 2) == 0);

    ASSERT(size_on_heap > 2);

    auto destination = init_destination(Dst, ARG1);
    auto src = load_source(Src, ARG2);

    ppc::Gp untagged_src = ARG3;
    emit_untag_ptr(untagged_src, src.reg);

    for (size_t i = 0; i < updates.size(); i += 2) {
        const auto &value = updates[i + 1].as<ArgSource>();
        if (!(always_immediate(value) || value.isLiteral())) {
            all_safe = false;
            if (maybe_immediate.isNil() &&
                always_one_of<BeamTypeId::MaybeImmediate>(value)) {
                maybe_immediate = value;
            } else {
                maybe_immediate = ArgNil();
                break;
            }
        }
    }

    if (all_safe) {
        comment("skipped copy fallback because all new values are safe");
    } else {
        Label update = a.new_label();

        if (!maybe_immediate.isNil()) {
            auto value = load_source(maybe_immediate, ARG5);
            emit_is_boxed(update, value.reg);
        }

        /* The tuple can be updated in place if it is in the part of the
         * heap that was allocated after the last garbage collection, that
         * is, if high_water <= untagged_src < HTOP. */
        a.ld(ARG4, ppc::Mem(c_p, offsetof(Process, high_water)));
        a.cmpld(untagged_src, HTOP);
        a.cmpld(ppc::cr1, untagged_src, ARG4);
        a.crandc(ppc::cr0.lt(), ppc::cr0.lt(), ppc::cr1.lt());
        a.blt(update);

        emit_copy_words(untagged_src, 0, HTOP, 0, size_on_heap);
        a.mr(untagged_src, HTOP);
        add(HTOP, HTOP, size_on_heap * sizeof(Eterm));

        a.bind(update);
    }

    for (size_t i = 0; i < updates.size(); i += 2) {
        const auto next_index = updates[i].as<ArgWord>().get();
        const auto &next_value = updates[i + 1].as<ArgSource>();

        auto value = load_source(next_value, TMP1);
        store_mem(value.reg,
                  ppc::Mem(untagged_src, next_index * sizeof(Eterm)));
    }

    a.addi(destination.reg, untagged_src, imm(TAG_PRIMARY_BOXED));
    flush_var(destination);

#ifdef DEBUG
    if (!all_safe && maybe_immediate.isNil()) {
        Label pointer_ok = a.new_label();

        /* If p->high_water contained a garbage value, a tuple not in
         * the safe part of the new heap could have been destructively
         * updated. */
        comment("sanity-checking tuple pointer");
        mov_arg(ARG2, Dst);
        a.ld(ARG4, ppc::Mem(c_p, offsetof(Process, heap)));
        a.cmpld(ARG2, HTOP);
        a.cmpld(ppc::cr1, ARG2, ARG4);
        a.crandc(ppc::cr0.lt(), ppc::cr0.lt(), ppc::cr1.lt());
        a.blt(pointer_ok);

        emit_enter_runtime();
        a.mr(ARG1, c_p);
        runtime_call<void (*)(Process *, Eterm), beam_jit_invalid_heap_ptr>();
        emit_leave_runtime();

        a.bind(pointer_ok);
    }
#endif
}

void BeamModuleAssembler::emit_set_tuple_element(const ArgSource &Element,
                                                 const ArgRegister &Tuple,
                                                 const ArgWord &Offset) {
    /* TODO: As of Erlang/OTP 29, this instruction is no longer
     * emitted by the compiler. It can be removed when the runtime
     * system no longer supports loading code compiled by Erlang/OTP
     * 28 or earlier. */

    auto tuple = load_source(Tuple, TMP1);
    auto element = load_source(Element, TMP2);
    ppc::Gp boxed_ptr = emit_ptr_val(TMP1, tuple.reg);
    ppc::Mem boxed_val = emit_boxed_val(boxed_ptr, Offset.get());

    store_mem(element.reg, boxed_val);
}

void BeamModuleAssembler::emit_is_nonempty_list(const ArgLabel &Fail,
                                                const ArgRegister &Src) {
    auto list_ptr = load_source(Src);
    emit_is_cons(resolve_beam_label(Fail, dispUnknown), list_ptr.reg);
}

void BeamModuleAssembler::emit_jump(const ArgLabel &Fail) {
    a.b(resolve_beam_label(Fail, disp32MB));
    mark_unreachable();
}

void BeamModuleAssembler::emit_is_atom(const ArgLabel &Fail,
                                       const ArgSource &Src) {
    auto src = load_source(Src);

    preserve_cache(
            [&]() {
                a.andi_rc(TMP1, src.reg, imm(_TAG_IMMED2_MASK));
                cmp(TMP1, _TAG_IMMED2_ATOM);
                a.bne(resolve_beam_label(Fail, disp32K));
            },
            TMP1);
}

void BeamModuleAssembler::emit_is_boolean(const ArgLabel &Fail,
                                          const ArgSource &Src) {
    /* Since am_true and am_false differ by a single bit, we can simplify the
     * check by setting said bit and comparing against the greater one. */
    ERTS_CT_ASSERT(am_false == make_atom(0));
    ERTS_CT_ASSERT(am_true == make_atom(1));

    auto src = load_source(Src, TMP1);
    a.ori(TMP1, src.reg, imm(am_true & ~_TAG_IMMED2_MASK));
    cmp(TMP1, am_true);
    a.bne(resolve_beam_label(Fail, disp32K));
}

void BeamModuleAssembler::emit_is_bitstring(const ArgLabel &Fail,
                                            const ArgSource &Src) {
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Bitstring) {
        comment("skipped header test since we know it's a bitstring when "
                "boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(ARG1, src.reg);
        load_mem(TMP1, emit_boxed_val(boxed_ptr));

        /* Use a mask that also clears the primary tag bits. That works
         * because we KNOW that a boxed pointer always points to a header
         * word and that the primary tag for a header is 0. */
        const auto mask = _BITSTRING_TAG_MASK & ~_TAG_PRIMARY_MASK;
        ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
        ERTS_CT_ASSERT(_TAG_HEADER_HEAP_BITS == (_TAG_HEADER_HEAP_BITS & mask));
        a.andi_rc(TMP1, TMP1, imm(mask));
        cmp(TMP1, _TAG_HEADER_HEAP_BITS);
        a.bne(resolve_beam_label(Fail, disp32K));
    }
}

void BeamModuleAssembler::emit_is_binary(const ArgLabel &Fail,
                                         const ArgSource &Src) {
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);
    emit_untag_ptr(ARG1, src.reg);

    a.ld(TMP1, ppc::Mem(ARG1, offsetof(ErlHeapBits, thing_word)));
    a.ld(TMP2, ppc::Mem(ARG1, offsetof(ErlHeapBits, size)));

    Label not_sub_bits = a.new_label();
    cmp(TMP1, HEADER_SUB_BITS);
    a.bne(not_sub_bits);
    {
        a.ld(TMP2, ppc::Mem(ARG1, offsetof(ErlSubBits, start)));
        a.ld(TMP3, ppc::Mem(ARG1, offsetof(ErlSubBits, end)));
        a.sub(TMP2, TMP3, TMP2);
    }
    a.bind(not_sub_bits);

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Bitstring) {
        comment("skipped header test since we know it's a bitstring when "
                "boxed");
        a.andi_rc(LR_TMP, TMP2, imm(7));
    } else {
        const auto mask = _BITSTRING_TAG_MASK & ~_TAG_PRIMARY_MASK;
        ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
        ERTS_CT_ASSERT(_TAG_HEADER_HEAP_BITS == (_TAG_HEADER_HEAP_BITS & mask));
        a.andi_rc(TMP1, TMP1, imm(mask));

        /* Shift out all but the lowest three bits in the size, leaving a
         * non-zero value if the size is not evenly divisible by 8.
         *
         * Thus, OR-ing this value into the header word forces the check to
         * fail when we have a non-binary bitstring. */
        ERTS_CT_ASSERT((UWORD_CONSTANT(7) << (64 - 3)) > _BITSTRING_TAG_MASK);
        a.sldi(TMP2, TMP2, 64 - 3);
        a.or_(TMP1, TMP1, TMP2);
        cmp(TMP1, _TAG_HEADER_HEAP_BITS);
    }

    a.bne(resolve_beam_label(Fail, disp32K));
}

void BeamModuleAssembler::emit_is_float(const ArgLabel &Fail,
                                        const ArgSource &Src) {
    auto src = load_source(Src, TMP1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Float) {
        comment("skipped header test since we know it's a float when boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(TMP1, emit_boxed_val(boxed_ptr));

        cmp(TMP1, HEADER_FLONUM);
        a.bne(resolve_beam_label(Fail, disp32K));
    }
}

void BeamModuleAssembler::emit_is_function(const ArgLabel &Fail,
                                           const ArgRegister &Src) {
    auto src = load_source(Src, TMP1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Fun) {
        comment("skipped header test since we know it's a fun when boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);

        /* The sub tag is in the lowest byte of the header word. */
        ERTS_CT_ASSERT(FUN_HEADER_ARITY_OFFS == 8);
        load_mem(TMP1, emit_boxed_val(boxed_ptr));
        a.andi_rc(TMP1, TMP1, imm(0xFF));
        cmp(TMP1, FUN_SUBTAG);
        a.bne(resolve_beam_label(Fail, disp32K));
    }
}

void BeamModuleAssembler::emit_is_function2(const ArgLabel &Fail,
                                            const ArgSource &Src,
                                            const ArgSource &Arity) {
    if (!Arity.isSmall()) {
        /*
         * Non-literal arity - extremely uncommon. Generate simple code.
         */
        mov_arg(ARG2, Src);
        mov_arg(ARG3, Arity);

        emit_enter_runtime();

        a.mr(ARG1, c_p);
        runtime_call<Eterm (*)(Process *, Eterm, Eterm), erl_is_function>();

        emit_leave_runtime();

        cmp(ARG1, am_true);
        a.bne(resolve_beam_label(Fail, disp32K));

        return;
    }

    unsigned arity = Arity.as<ArgSmall>().getUnsigned();
    if (arity > MAX_ARG) {
        /* Arity is negative or too large. */
        a.b(resolve_beam_label(Fail, disp32MB));
        mark_unreachable();

        return;
    }

    auto src = load_source(Src, TMP1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);

    /* The sub tag and the arity are in the lowest two bytes of the header
     * word. */
    ERTS_CT_ASSERT(FUN_HEADER_ARITY_OFFS == 8);
    ERTS_CT_ASSERT(FUN_HEADER_ENV_SIZE_OFFS == 16);
    load_mem(TMP2, emit_boxed_val(boxed_ptr));
    a.andi_rc(TMP2, TMP2, imm(0xFFFF));
    cmpl(TMP2, MAKE_FUN_HEADER(arity, 0, 0) & 0xFFFF);
    a.bne(resolve_beam_label(Fail, disp32K));
}

void BeamModuleAssembler::emit_is_integer(const ArgLabel &Fail,
                                          const ArgSource &Src) {
    auto src = load_source(Src, TMP1);

    if (always_immediate(Src)) {
        comment("skipped test for boxed since the value is always immediate");
        a.andi_rc(TMP2, src.reg, imm(_TAG_IMMED1_MASK));
        cmp(TMP2, _TAG_IMMED1_SMALL);
        a.bne(resolve_beam_label(Fail, disp32K));

        return;
    }

    Label next = a.new_label();

    if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(Src)) {
        comment("simplified small test since all other types are boxed");
        emit_is_boxed(next, Src, src.reg);
    } else {
        a.andi_rc(TMP2, src.reg, imm(_TAG_IMMED1_MASK));
        cmp(TMP2, _TAG_IMMED1_SMALL);
        a.beq(next);

        emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, TMP2);
    }

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Integer) {
        comment("skipped header test since we know it's a bignum when "
                "boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(TMP1, emit_boxed_val(boxed_ptr));

        /* The sign bit is left out of the mask, so that both kinds of
         * bignums compare equal to a positive one. */
        auto mask = _HEADER_SUBTAG_MASK - _BIG_SIGN_BIT;
        ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
        a.andi_rc(TMP1, TMP1, imm(mask));
        cmp(TMP1, _TAG_HEADER_POS_BIG);
        a.bne(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_is_list(const ArgLabel &Fail,
                                       const ArgSource &Src) {
    auto src = load_source(Src);

    emit_is_list(resolve_beam_label(Fail, dispUnknown), src.reg);
}

void BeamModuleAssembler::emit_is_map(const ArgLabel &Fail,
                                      const ArgSource &Src) {
    auto src = load_source(Src);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    /* As an optimization for the `error | #{}` case, skip checking the header
     * word when we know that the only possible boxed type is a map. */
    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Map) {
        comment("skipped header test since we know it's a map when boxed");
    } else {
        preserve_cache(
                [&]() {
                    ppc::Gp boxed_ptr = emit_ptr_val(TMP3, src.reg);
                    load_mem(TMP3, emit_boxed_val(boxed_ptr));
                    a.andi_rc(TMP3, TMP3, imm(_TAG_HEADER_MASK));
                    cmp(TMP3, _TAG_HEADER_MAP);
                    a.bne(resolve_beam_label(Fail, disp32K));
                },
                TMP3);
    }
}

void BeamModuleAssembler::emit_is_nil(const ArgLabel &Fail,
                                      const ArgRegister &Src) {
    auto src = load_source(Src);

    if (always_one_of<BeamTypeId::List>(Src)) {
        emit_is_not_cons(resolve_beam_label(Fail, dispUnknown), src.reg);
    } else {
        preserve_cache([&]() {
            cmp(src.reg, NIL);
            a.bne(resolve_beam_label(Fail, disp32K));
        });
    }
}

void BeamModuleAssembler::emit_is_number(const ArgLabel &Fail,
                                         const ArgSource &Src) {
    auto src = load_source(Src, TMP1);
    Label next = a.new_label();

    if (always_one_of<BeamTypeId::Integer, BeamTypeId::AlwaysBoxed>(Src)) {
        comment("simplified small test since all other types are boxed");
        emit_is_boxed(next, Src, src.reg);
    } else {
        a.andi_rc(TMP2, src.reg, imm(_TAG_IMMED1_MASK));
        cmp(TMP2, _TAG_IMMED1_SMALL);
        a.beq(next);

        emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);
    }

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Number) {
        comment("skipped header test since we know it's a number when boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(TMP1, emit_boxed_val(boxed_ptr));

        /* The value is a number if the header is that of a bignum of either
         * sign, or that of a float. Both tests are done before branching on
         * their combined result. */
        auto mask = _HEADER_SUBTAG_MASK - _BIG_SIGN_BIT;
        ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
        a.andi_rc(TMP2, TMP1, imm(mask));
        cmp(TMP2, _TAG_HEADER_POS_BIG);
        cmp(TMP1, HEADER_FLONUM, ppc::cr1);
        a.cror(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
        a.bne(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_is_pid(const ArgLabel &Fail,
                                      const ArgSource &Src) {
    auto src = load_source(Src, TMP1);
    Label next = a.new_label();

    if (always_one_of<BeamTypeId::Pid, BeamTypeId::AlwaysBoxed>(Src)) {
        comment("simplified local pid test since all other types are boxed");
        emit_is_boxed(next, Src, src.reg);
    } else {
        a.andi_rc(TMP2, src.reg, imm(_TAG_IMMED1_MASK));
        cmp(TMP2, _TAG_IMMED1_PID);
        a.beq(next);

        /* Reuse TMP2 as the important bits are still available. */
        emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, TMP2);
    }

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Pid) {
        comment("skipped header test since we know it's a pid when boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(TMP2, emit_boxed_val(boxed_ptr));
        a.andi_rc(TMP2, TMP2, imm(_TAG_HEADER_MASK));
        cmp(TMP2, _TAG_HEADER_EXTERNAL_PID);
        a.bne(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_is_port(const ArgLabel &Fail,
                                       const ArgSource &Src) {
    auto src = load_source(Src, TMP1);
    Label next = a.new_label();

    if (always_one_of<BeamTypeId::Port, BeamTypeId::AlwaysBoxed>(Src)) {
        comment("simplified local port test since all other types are boxed");
        emit_is_boxed(next, Src, src.reg);
    } else {
        a.andi_rc(TMP2, src.reg, imm(_TAG_IMMED1_MASK));
        cmp(TMP2, _TAG_IMMED1_PORT);
        a.beq(next);

        /* Reuse TMP2 as the important bits are still available. */
        emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, TMP2);
    }

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Port) {
        comment("skipped header test since we know it's a port when boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(TMP2, emit_boxed_val(boxed_ptr));
        a.andi_rc(TMP2, TMP2, imm(_TAG_HEADER_MASK));
        cmp(TMP2, _TAG_HEADER_EXTERNAL_PORT);
        a.bne(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_is_reference(const ArgLabel &Fail,
                                            const ArgSource &Src) {
    auto src = load_source(Src, TMP1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Reference) {
        comment("skipped header test since we know it's a ref when boxed");
    } else {
        ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
        load_mem(TMP1, emit_boxed_val(boxed_ptr));

        /* The value is a reference if the header is that of a local or an
         * external one. */
        a.andi_rc(TMP1, TMP1, imm(_TAG_HEADER_MASK));
        cmp(TMP1, _TAG_HEADER_EXTERNAL_REF);
        cmp(TMP1, _TAG_HEADER_REF, ppc::cr1);
        a.cror(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
        a.bne(resolve_beam_label(Fail, disp32K));
    }
}

/* Note: This instruction leaves the untagged pointer to the tuple in
 * ARG1. */
void BeamModuleAssembler::emit_i_is_tagged_tuple(const ArgLabel &Fail,
                                                 const ArgSource &Src,
                                                 const ArgWord &Arity,
                                                 const ArgAtom &Tag) {
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    emit_untag_ptr(ARG1, src.reg);

    /* It is safe to fetch the both the header word and the first
     * element of the tuple because the empty tuple is always
     * a literal that is padded so that the word after arity is
     * allocated. */
    a.ld(TMP1, ppc::Mem(ARG1));
    a.ld(TMP2, ppc::Mem(ARG1, sizeof(Eterm)));

    /* Both the tag and the arity must match. */
    cmp_arg(TMP2, Tag);
    cmp_arg(TMP1, Arity, ppc::cr1);
    a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());

    a.bne(resolve_beam_label(Fail, disp32K));
}

/* Note: This instruction leaves the untagged pointer to the tuple in
 * ARG2. */
void BeamModuleAssembler::emit_i_is_tagged_tuple_ff(const ArgLabel &NotTuple,
                                                    const ArgLabel &NotRecord,
                                                    const ArgSource &Src,
                                                    const ArgWord &Arity,
                                                    const ArgAtom &Tag) {
    Label correct_arity = a.new_label();
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(NotTuple, dispUnknown), Src, src.reg);

    emit_untag_ptr(ARG1, src.reg);

    /* It is safe to fetch the both the header word and the first
     * element of the tuple because the empty tuple is always
     * a literal that is padded so that the word after arity is
     * allocated. */
    a.ld(TMP1, ppc::Mem(ARG1));
    a.ld(TMP2, ppc::Mem(ARG1, sizeof(Eterm)));

    cmp_arg(TMP1, Arity);
    a.beq(correct_arity);

    /* Not a tuple or the wrong arity. Decide which. */
    ERTS_CT_ASSERT(_TAG_HEADER_ARITYVAL == 0);
    a.andi_rc(LR_TMP, TMP1, imm(_TAG_HEADER_MASK));
    a.beq(resolve_beam_label(NotRecord, disp32K));
    a.b(resolve_beam_label(NotTuple, disp32MB));

    a.bind(correct_arity);
    {
        cmp_arg(TMP2, Tag);
        a.bne(resolve_beam_label(NotRecord, disp32K));
    }
}

/* Note: This instruction leaves the untagged pointer to the tuple in
 * ARG1. */
void BeamModuleAssembler::emit_i_is_tuple(const ArgLabel &Fail,
                                          const ArgSource &Src) {
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);
    emit_untag_ptr(ARG1, src.reg);

    /* As an optimization for the `error | {ok, Value}` case, skip checking the
     * header word when we know that the only possible boxed type is a tuple. */
    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Tuple) {
        comment("skipped header test since we know it's a tuple when boxed");
    } else {
        a.ld(TMP1, ppc::Mem(ARG1));
        ERTS_CT_ASSERT(_TAG_HEADER_ARITYVAL == 0);
        a.andi_rc(LR_TMP, TMP1, imm(_TAG_HEADER_MASK));
        a.bne(resolve_beam_label(Fail, disp32K));
    }
}

/* Note: This instruction leaves the untagged pointer to the tuple in
 * ARG1. */
void BeamModuleAssembler::emit_i_is_tuple_of_arity(const ArgLabel &Fail,
                                                   const ArgSource &Src,
                                                   const ArgWord &Arity) {
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    emit_untag_ptr(ARG1, src.reg);

    a.ld(TMP1, ppc::Mem(ARG1));
    cmp_arg(TMP1, Arity);
    a.bne(resolve_beam_label(Fail, disp32K));
}

/* Note: This instruction leaves the untagged pointer to the tuple in
 * ARG1. */
void BeamModuleAssembler::emit_i_is_tuple_of_arity_ff(const ArgLabel &NotTuple,
                                                      const ArgLabel &BadArity,
                                                      const ArgSource &Src,
                                                      const ArgWord &Arity) {
    auto src = load_source(Src, ARG1);

    emit_is_boxed(resolve_beam_label(NotTuple, dispUnknown), Src, src.reg);

    emit_untag_ptr(ARG1, src.reg);

    a.ld(TMP1, ppc::Mem(ARG1));

    /* As an optimization for the `error | {ok, Value}` case, skip checking the
     * header word when we know that the only possible boxed type is a tuple. */
    if (masked_types<BeamTypeId::MaybeBoxed>(Src) == BeamTypeId::Tuple) {
        comment("skipped header test since we know it's a tuple when boxed");
    } else {
        ERTS_CT_ASSERT(_TAG_HEADER_ARITYVAL == 0);
        a.andi_rc(LR_TMP, TMP1, imm(_TAG_HEADER_MASK));
        a.bne(resolve_beam_label(NotTuple, disp32K));
    }

    cmp_arg(TMP1, Arity);
    a.bne(resolve_beam_label(BadArity, disp32K));
}

/* Note: This instruction leaves the untagged pointer to the tuple in
 * ARG1. */
void BeamModuleAssembler::emit_i_test_arity(const ArgLabel &Fail,
                                            const ArgSource &Src,
                                            const ArgWord &Arity) {
    auto src = load_source(Src, ARG1);
    emit_untag_ptr(ARG1, src.reg);

    a.ld(TMP1, ppc::Mem(ARG1));
    cmp_arg(TMP1, Arity);
    a.bne(resolve_beam_label(Fail, disp32K));
}

/*
 * ARG1 = First operand
 * ARG2 = Literal list
 *
 * The result is returned in the EQ bit of CR0.
 */
void BeamGlobalAssembler::emit_is_eq_exact_list_shared() {
    Label loop = a.new_label(), mid = a.new_label(), done = a.new_label();

    a.b(mid);

    a.bind(loop);
    emit_untag_ptr(ARG1, ARG1);
    emit_untag_ptr(ARG2, ARG2);
    a.ld(TMP1, ppc::Mem(ARG1));
    a.ld(ARG1, ppc::Mem(ARG1, sizeof(Eterm)));
    a.ld(TMP2, ppc::Mem(ARG2));
    a.ld(ARG2, ppc::Mem(ARG2, sizeof(Eterm)));
    a.cmpd(TMP1, TMP2);
    a.bne(done);

    a.bind(mid);
    a.cmpd(ARG1, ARG2);
    a.beq(done);

    /* If not equal, both terms must be CONSes. */
#if !defined(DEBUG)
    ERTS_CT_ASSERT(!is_list(make_small(0) | make_list(0)));
    ERTS_CT_ASSERT(!is_list(make_boxed(0) | make_list(0)));
#endif
    a.or_(TMP1, ARG1, ARG2);
    emit_is_not_cons(loop, TMP1);

    /* Not equal. Clear the EQ bit; TMP1 cannot be zero here. */
    cmp(TMP1, 0);

    a.bind(done);
    a.blr();
}

/*
 * ARG1 = LHS
 * ARG2 = RHS
 *
 * The result is returned in the EQ bit of CR0.
 */
void BeamGlobalAssembler::emit_is_eq_exact_shallow_boxed_shared() {
    Label loop = a.new_label();
    Label done = a.new_label();
    Label not_equal = a.new_label();

    a.or_(TMP1, ARG1, ARG2);
    emit_is_boxed(not_equal, TMP1);

    emit_untag_ptr(TMP1, ARG1);
    a.ld(TMP3, ppc::Mem(TMP1));
    emit_untag_ptr(TMP2, ARG2);
    a.srdi(ARG3, TMP3, _HEADER_ARITY_OFFS);
    a.addi(ARG3, ARG3, imm(-1));

    a.bind(loop);
    {
        /* Two words are compared in each iteration. */
        a.ld(TMP3, ppc::Mem(TMP1));
        a.ld(TMP4, ppc::Mem(TMP1, sizeof(Eterm)));
        a.addi(TMP1, TMP1, imm(sizeof(Eterm[2])));
        a.ld(TMP5, ppc::Mem(TMP2));
        a.ld(TMP6, ppc::Mem(TMP2, sizeof(Eterm)));
        a.addi(TMP2, TMP2, imm(sizeof(Eterm[2])));
        a.cmpd(TMP3, TMP5);
        a.cmpd(ppc::cr1, TMP4, TMP6);
        a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
        a.bne(done);

        /* The record form of the instruction compares the result with
         * zero. */
        a.addic_rc(ARG3, ARG3, imm(-2));
        a.bge(loop);
    }

    /* There is one more word to compare unless the count is now -2. */
    cmp(ARG3, -2);
    a.beq(done);

    a.ld(TMP3, ppc::Mem(TMP1));
    a.ld(TMP5, ppc::Mem(TMP2));
    a.cmpd(TMP3, TMP5);

    a.bind(done);
    a.blr();

    a.bind(not_equal);
    /* Clear the EQ bit; TMP1 cannot be zero here. */
    cmp(TMP1, 0);
    a.blr();
}

void BeamModuleAssembler::emit_is_eq_exact(const ArgLabel &Fail,
                                           const ArgSource &X,
                                           const ArgSource &Y) {
    auto x = load_source(X, ARG1);

    if (Y.isLiteral()) {
        Eterm literal = beamfile_get_literal(beam, Y.as<ArgLiteral>().get());

        if (is_list(literal) && is_immed(CAR(list_val(literal))) &&
            is_nil(CDR(list_val(literal)))) {
            /* Inline the equality test if the RHS argument is a list
             * of one immediate value such as `[42]` or `[a]`. */
            ppc::Gp cons_ptr;

            comment("inlined equality test with %T", literal);
            if (!exact_type<BeamTypeId::Cons>(X)) {
                emit_is_cons(resolve_beam_label(Fail, dispUnknown), x.reg);
            }
            cons_ptr = emit_ptr_val(TMP1, x.reg);
            a.addi(TMP1, cons_ptr, imm(-TAG_PRIMARY_LIST));
            a.ld(TMP2, ppc::Mem(TMP1));
            a.ld(TMP3, ppc::Mem(TMP1, sizeof(Eterm)));

            /* Both the head and the tail must match. */
            cmp(TMP2, CAR(list_val(literal)));
            cmp(TMP3, NIL, ppc::cr1);
            a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
            a.bne(resolve_beam_label(Fail, disp32K));

            return;
        } else if (beam_jit_is_list_of_immediates(literal)) {
            comment("optimized equality test with %T", literal);
            mov_var(ARG1, x);
            mov_arg(ARG2, Y);
            fragment_call(ga->get_is_eq_exact_list_shared());
            a.bne(resolve_beam_label(Fail, disp32K));

            return;
        } else if (beam_jit_is_shallow_boxed(literal)) {
            comment("optimized equality test with %T", literal);
            mov_var(ARG1, x);
            mov_arg(ARG2, Y);
            fragment_call(ga->get_is_eq_exact_shallow_boxed_shared());
            a.bne(resolve_beam_label(Fail, disp32K));

            return;
        } else if (is_bitstring(literal) && bitstring_size(literal) == 0) {
            Label not_sub_bits = a.new_label();

            comment("simplified equality test with empty binary");
            emit_is_boxed(resolve_beam_label(Fail, dispUnknown), X, x.reg);
            emit_untag_ptr(ARG1, x.reg);

            a.ld(TMP1, ppc::Mem(ARG1, offsetof(ErlHeapBits, thing_word)));
            a.ld(TMP2, ppc::Mem(ARG1, offsetof(ErlHeapBits, size)));

            cmp(TMP1, HEADER_SUB_BITS);
            a.bne(not_sub_bits);
            {
                a.ld(TMP2, ppc::Mem(ARG1, offsetof(ErlSubBits, start)));
                a.ld(TMP3, ppc::Mem(ARG1, offsetof(ErlSubBits, end)));
                a.sub(TMP2, TMP3, TMP2);
            }
            a.bind(not_sub_bits);

            if (masked_types<BeamTypeId::MaybeBoxed>(X) ==
                BeamTypeId::Bitstring) {
                comment("skipped header test since we know it's a bitstring "
                        "when boxed");
                cmp(TMP2, 0);
                a.bne(resolve_beam_label(Fail, disp32K));
            } else {
                const auto mask = _BITSTRING_TAG_MASK & ~_TAG_PRIMARY_MASK;
                ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
                ERTS_CT_ASSERT(_TAG_HEADER_HEAP_BITS ==
                               (_TAG_HEADER_HEAP_BITS & mask));

                /* It is an empty binary if it is a bitstring and its size
                 * is zero. */
                a.andi_rc(TMP1, TMP1, imm(mask));
                cmp(TMP1, _TAG_HEADER_HEAP_BITS);
                cmp(TMP2, 0, ppc::cr1);
                a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
                a.bne(resolve_beam_label(Fail, disp32K));
            }

            return;
        } else if (is_map(literal) && erts_map_size(literal) == 0) {
            comment("optimized equality test with empty map", literal);
            emit_is_boxed(resolve_beam_label(Fail, dispUnknown), X, x.reg);
            emit_untag_ptr(ARG1, x.reg);
            a.ld(TMP1, ppc::Mem(ARG1));
            a.ld(TMP2, ppc::Mem(ARG1, sizeof(Eterm)));

            /* It is an empty map if it is a flatmap and its size is zero. */
            cmp(TMP1, MAP_HEADER_FLATMAP);
            cmp(TMP2, 0, ppc::cr1);
            a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
            a.bne(resolve_beam_label(Fail, disp32K));

            return;
        }
    }

    /* If either argument is known to be an immediate, we can fail immediately
     * if they're not equal. */
    if (always_immediate(X) || always_immediate(Y)) {
        if (!X.isImmed() && !Y.isImmed()) {
            comment("simplified check since one argument is an immediate");
        }

        preserve_cache([&]() {
            cmp_arg(x.reg, Y);
            a.bne(resolve_beam_label(Fail, disp32K));
        });

        return;
    }

    /* Both operands are registers or literals. */
    Label next = a.new_label();
    auto y = load_source(Y, ARG2);

    a.cmpd(x.reg, y.reg);
    a.beq(next);

    if (exact_type<BeamTypeId::Integer>(X) &&
        exact_type<BeamTypeId::Integer>(Y)) {
        /* Fail immediately if one of the operands is a small. */
        a.or_(TMP1, x.reg, y.reg);
        emit_is_boxed(resolve_beam_label(Fail, dispUnknown), TMP1);
    } else if (always_same_types(X, Y)) {
        comment("skipped tag test since they are always equal");
    } else {
        /* Fail immediately if the pointer tags are not equal. */
        emit_is_unequal_based_on_tags(resolve_beam_label(Fail, dispUnknown),
                                      X,
                                      x.reg,
                                      Y,
                                      y.reg);
    }

    /* Both operands are pointers having the same tag. Must do a
     * deeper comparison. */
    mov_var(ARG1, x);
    mov_var(ARG2, y);

    if (always_one_of<BeamTypeId::Integer, BeamTypeId::Float>(X) ||
        always_one_of<BeamTypeId::Integer, BeamTypeId::Float>(Y)) {
        fragment_call(ga->get_is_eq_exact_shallow_boxed_shared());
        a.bne(resolve_beam_label(Fail, disp32K));
    } else {
        emit_enter_runtime();
        runtime_call<int (*)(Eterm, Eterm), eq>();
        emit_leave_runtime();
        cmp(ARG1, 0);
        a.beq(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_is_ne_exact(const ArgLabel &Fail,
                                           const ArgSource &X,
                                           const ArgSource &Y) {
    auto x = load_source(X, ARG1);

    if (Y.isLiteral()) {
        Eterm literal = beamfile_get_literal(beam, Y.as<ArgLiteral>().get());
        bool imm_list = beam_jit_is_list_of_immediates(literal);

        if (imm_list && erts_list_length(literal) == 1) {
            ppc::Gp cons_ptr;
            Label next = a.new_label();

            /* Inline the equality test if the RHS argument is a list
             * of one immediate value such as `[42]` or `[a]`. */
            comment("inlined non-equality test with %T", literal);
            if (!exact_type<BeamTypeId::Cons>(X)) {
                emit_is_cons(next, x.reg);
            }
            cons_ptr = emit_ptr_val(TMP1, x.reg);
            a.addi(TMP1, cons_ptr, imm(-TAG_PRIMARY_LIST));
            a.ld(TMP2, ppc::Mem(TMP1));
            a.ld(TMP3, ppc::Mem(TMP1, sizeof(Eterm)));

            /* The terms are equal if both the head and the tail match. */
            cmp(TMP2, CAR(list_val(literal)));
            cmp(TMP3, NIL, ppc::cr1);
            a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
            a.beq(resolve_beam_label(Fail, disp32K));

            a.bind(next);

            return;
        } else if (imm_list) {
            comment("optimized non-equality test with %T", literal);
            mov_var(ARG1, x);
            mov_arg(ARG2, Y);
            fragment_call(ga->get_is_eq_exact_list_shared());
            a.beq(resolve_beam_label(Fail, disp32K));

            return;
        } else if (beam_jit_is_shallow_boxed(literal)) {
            comment("optimized non-equality test with %T", literal);
            mov_var(ARG1, x);
            mov_arg(ARG2, Y);
            fragment_call(ga->get_is_eq_exact_shallow_boxed_shared());
            a.beq(resolve_beam_label(Fail, disp32K));

            return;
        } else if (is_bitstring(literal) && bitstring_size(literal) == 0) {
            Label next = a.new_label(), not_sub_bits = a.new_label();

            comment("simplified non-equality test with empty binary");
            emit_is_boxed(next, X, x.reg);
            emit_untag_ptr(ARG1, x.reg);

            a.ld(TMP1, ppc::Mem(ARG1, offsetof(ErlHeapBits, thing_word)));
            a.ld(TMP2, ppc::Mem(ARG1, offsetof(ErlHeapBits, size)));

            cmp(TMP1, HEADER_SUB_BITS);
            a.bne(not_sub_bits);
            {
                a.ld(TMP2, ppc::Mem(ARG1, offsetof(ErlSubBits, start)));
                a.ld(TMP3, ppc::Mem(ARG1, offsetof(ErlSubBits, end)));
                a.sub(TMP2, TMP3, TMP2);
            }
            a.bind(not_sub_bits);

            if (masked_types<BeamTypeId::MaybeBoxed>(X) ==
                BeamTypeId::Bitstring) {
                comment("skipped header test since we know it's a bitstring "
                        "when boxed");
                cmp(TMP2, 0);
                a.beq(resolve_beam_label(Fail, disp32K));
            } else {
                const auto mask = _BITSTRING_TAG_MASK & ~_TAG_PRIMARY_MASK;
                ERTS_CT_ASSERT(TAG_PRIMARY_HEADER == 0);
                ERTS_CT_ASSERT(_TAG_HEADER_HEAP_BITS ==
                               (_TAG_HEADER_HEAP_BITS & mask));

                /* It is an empty binary if it is a bitstring and its size
                 * is zero. */
                a.andi_rc(TMP1, TMP1, imm(mask));
                cmp(TMP1, _TAG_HEADER_HEAP_BITS);
                cmp(TMP2, 0, ppc::cr1);
                a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
                a.beq(resolve_beam_label(Fail, disp32K));
            }

            a.bind(next);

            return;
        } else if (is_map(literal) && erts_map_size(literal) == 0) {
            Label next = a.new_label();

            comment("optimized non-equality test with empty map", literal);
            emit_is_boxed(next, X, x.reg);
            emit_untag_ptr(ARG1, x.reg);
            a.ld(TMP1, ppc::Mem(ARG1));
            a.ld(TMP2, ppc::Mem(ARG1, sizeof(Eterm)));

            /* It is an empty map if it is a flatmap and its size is zero. */
            cmp(TMP1, MAP_HEADER_FLATMAP);
            cmp(TMP2, 0, ppc::cr1);
            a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
            a.beq(resolve_beam_label(Fail, disp32K));

            a.bind(next);

            return;
        }
    }

    /* If either argument is known to be an immediate, we can fail immediately
     * if they're equal. */
    if (always_immediate(X) || always_immediate(Y)) {
        if (!X.isImmed() && !Y.isImmed()) {
            comment("simplified check since one argument is an immediate");
        }

        preserve_cache([&]() {
            cmp_arg(x.reg, Y);
            a.beq(resolve_beam_label(Fail, disp32K));
        });

        return;
    }

    /* Both operands are registers or literals. */
    Label next = a.new_label();
    auto y = load_source(Y, ARG2);

    a.cmpd(x.reg, y.reg);
    a.beq(resolve_beam_label(Fail, disp32K));

    if (exact_type<BeamTypeId::Integer>(X) &&
        exact_type<BeamTypeId::Integer>(Y)) {
        /* Succeed immediately if one of the operands is a small. */
        a.or_(TMP1, x.reg, y.reg);
        emit_is_boxed(next, TMP1);
    } else if (always_same_types(X, Y)) {
        comment("skipped tag test since they are always equal");
    } else {
        /* Test whether the terms are definitely unequal based on the tags
         * alone. */
        emit_is_unequal_based_on_tags(next, X, x.reg, Y, y.reg);
    }

    /* Both operands are pointers having the same tag. Must do a
     * deeper comparison. */
    mov_var(ARG1, x);
    mov_var(ARG2, y);

    if (always_one_of<BeamTypeId::Integer, BeamTypeId::Float>(X) ||
        always_one_of<BeamTypeId::Integer, BeamTypeId::Float>(Y)) {
        fragment_call(ga->get_is_eq_exact_shallow_boxed_shared());
        a.beq(resolve_beam_label(Fail, disp32K));
    } else {
        emit_enter_runtime();
        runtime_call<int (*)(Eterm, Eterm), eq>();
        emit_leave_runtime();
        cmp(ARG1, 0);
        a.bne(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_badmatch(const ArgSource &Src) {
    emit_error(BADMATCH, Src);
    mark_unreachable();
}

void BeamModuleAssembler::emit_case_end(const ArgSource &Src) {
    emit_error(EXC_CASE_CLAUSE, Src);
    mark_unreachable();
}

void BeamModuleAssembler::emit_system_limit_body() {
    emit_error(SYSTEM_LIMIT);
    mark_unreachable();
}

void BeamModuleAssembler::emit_if_end() {
    emit_error(EXC_IF_CLAUSE);
    mark_unreachable();
}

void BeamModuleAssembler::emit_badrecord(const ArgSource &Src) {
    emit_error(EXC_BADRECORD, Src);
    mark_unreachable();
}

void BeamModuleAssembler::emit_catch(const ArgYRegister &Y,
                                     const ArgCatch &Handler) {
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    a.addi(TMP1, TMP1, imm(1));
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));

    mov_arg(Y, Handler);
}

void BeamGlobalAssembler::emit_catch_end_shared() {
    Label not_throw = a.new_label(), not_error = a.new_label(),
          after_gc = a.new_label();

    /* XREG0 = THE_NON_VALUE
     * XREG1 = error reason/thrown value
     * XREG2 = raw stacktrace.
     * XREG3 = class
     */

    a.mr(XREG0, XREG1);
    cmp(XREG3, am_throw);

    a.bne(not_throw);

    /* Return thrown value. */
    a.blr();

    a.bind(not_throw);
    {
        emit_enter_runtime_frame();

        a.mr(ARG1, XREG0);

        cmp(XREG3, am_error);
        a.bne(not_error);

        a.mr(ARG2, XREG0);
        a.mr(ARG3, XREG2);

        /* This is an error, attach a stacktrace to the reason. */
        ERTS_CT_ASSERT(ERTS_HIGHEST_CALLEE_SAVE_XREG >= 2);
        emit_enter_runtime<Update::eHeapAlloc>(2);

        a.mr(ARG1, c_p);
        runtime_call<Eterm (*)(Process *, Eterm, Eterm), add_stacktrace>();

        emit_leave_runtime<Update::eHeapAlloc>(2);

        /* Fall through! */
    }

    /* Error term from exit/1 or stack backtrace from error/{1,2,3} is
     * now in ARG1. */
    a.bind(not_error);
    {
        const int32_t bytes_needed = (3 + S_RESERVED) * sizeof(Eterm);
        add(ARG3, HTOP, bytes_needed);
        a.cmpld(ARG3, E);

        a.ble(after_gc);
        {
            /* Preserve stacktrace / reason. */
            a.mr(XREG0, ARG1);

            mov_imm(ARG4, 1);
            a.bl(labels[garbage_collect]);

            a.mr(ARG1, XREG0);
        }
        a.bind(after_gc);

        a.addi(XREG0, HTOP, imm(TAG_PRIMARY_BOXED));
        mov_imm(TMP1, make_arityval(2));
        mov_imm(TMP2, am_EXIT);
        a.std(TMP1, ppc::Mem(HTOP));
        a.std(TMP2, ppc::Mem(HTOP, sizeof(Eterm)));
        a.std(ARG1, ppc::Mem(HTOP, sizeof(Eterm[2])));
        a.addi(HTOP, HTOP, imm(sizeof(Eterm[3])));
    }

    emit_leave_runtime_frame();
    a.blr();
}

void BeamModuleAssembler::emit_catch_end(const ArgYRegister &CatchTag) {
    Label next = a.new_label();

    /* XREG0 = THE_NON_VALUE
     * XREG1 = class
     * XREG2 = error reason/thrown value
     * XREG3 = raw stacktrace. */

    emit_try_end(CatchTag);
    emit_branch_if_value(XREG0, next);
    fragment_call(ga->get_catch_end_shared());
    a.bind(next);
}

void BeamModuleAssembler::emit_try_end(const ArgYRegister &CatchTag) {
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    a.addi(TMP1, TMP1, imm(-1));
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    mov_imm(TMP1, NIL);
    a.std(TMP1, getArgRef(CatchTag));
}

void BeamModuleAssembler::emit_try_end_deallocate(const ArgWord &Deallocate) {
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    a.addi(TMP1, TMP1, imm(-1));
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    if (Deallocate.get() > 0) {
        add(E, E, Deallocate.get() * sizeof(Eterm));
    }
}

void BeamModuleAssembler::emit_try_end_move_deallocate(
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &Deallocate) {
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    a.addi(TMP1, TMP1, imm(-1));
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    mov_arg(Dst, Src);
    if (Deallocate.get() > 0) {
        add(E, E, Deallocate.get() * sizeof(Eterm));
    }
}

void BeamModuleAssembler::emit_try_case(const ArgYRegister &CatchTag) {
    /* XREG0 = THE_NON_VALUE
     * XREG1 = error reason/thrown value
     * XREG2 = raw stacktrace.
     * XREG3 = class */

    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));
    a.mr(XREG0, XREG3);
    a.addi(TMP1, TMP1, imm(-1));
    a.std(TMP1, ppc::Mem(c_p, offsetof(Process, catches)));

    /* The try_tag in the Y slot in the stack frame has already been
     * cleared. */

#ifdef DEBUG
    Label ok = a.new_label();
    comment("Start of assertion code");
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, fvalue)));
    a.ld(TMP2, ppc::Mem(c_p, offsetof(Process, ftrace)));
    cmp(TMP1, NIL);
    cmp(TMP2, NIL, ppc::cr1);
    a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
    a.beq(ok);

    comment("Assertion c_p->fvalue == NIL && c_p->ftrace == NIL failed");
    udf(0x42);

    a.bind(ok);
#endif
}

void BeamModuleAssembler::emit_try_case_end(const ArgSource &Src) {
    emit_error(EXC_TRY_CLAUSE, Src);
}

void BeamGlobalAssembler::emit_raise_shared() {
    a.std(ARG1, ppc::Mem(c_p, offsetof(Process, fvalue)));
    a.std(ARG2, ppc::Mem(c_p, offsetof(Process, ftrace)));

    /* The call below overwrites the link register, so the address of the
     * instruction that raised the exception is kept in a register that C
     * code preserves. */
    a.mflr(TMP2);

    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *, Eterm), erts_sanitize_freason>();

    emit_leave_runtime(0);

    mov_imm(ARG4, 0);
    a.mr(ARG2, TMP2);
    a.b(labels[raise_exception_shared]);
}

void BeamModuleAssembler::emit_raise(const ArgSource &Trace,
                                     const ArgSource &Value) {
    mov_arg(ARG1, Value);
    mov_arg(ARG2, Trace);
    fragment_call(ga->get_raise_shared());

    mark_unreachable();

    /* `line` instructions need to know the latest offset that may throw an
     * exception. See the `line` instruction for details. */
    last_error_offset = a.offset();
}

void BeamModuleAssembler::emit_build_stacktrace() {
    a.mr(ARG2, XREG0);

    emit_enter_runtime<Update::eHeapAlloc>(0);

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm), build_stacktrace>();

    emit_leave_runtime<Update::eHeapAlloc>(0);

    a.mr(XREG0, ARG1);
}

/* This instruction has the same semantics as the erlang:raise/3 BIF,
 * except that it can rethrow a raw stack backtrace. */
void BeamModuleAssembler::emit_raw_raise() {
    Label next = a.new_label();

    a.mr(ARG1, XREG2);
    a.mr(ARG2, XREG0);
    a.mr(ARG3, XREG1);
    a.mr(ARG4, c_p);

    emit_enter_runtime(0);
    runtime_call<int (*)(Eterm, Eterm, Eterm, Process *), raw_raise>();
    emit_leave_runtime(0);

    a.cmpwi(ARG1, imm(0));
    a.bne(next);

    emit_raise_exception();

    a.bind(next);
    mov_imm(XREG0, am_badarg);
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

void BeamModuleAssembler::emit_i_perf_counter() {
    Label next = a.new_label(), small = a.new_label();

    /* Call the function pointer used by erts_sys_perf_counter */
#ifdef WIN32
    mov_imm(TMP1, erts_sys_time_data__.r.o.sys_hrtime);
#else
    mov_imm(TMP1, erts_sys_time_data__.r.o.perf_counter);
#endif
    dynamic_runtime_call<0>(TMP1);

    a.sradi(TMP1, ARG1, imm(SMALL_BITS - 1));
    a.addi(TMP1, TMP1, imm(1));
    a.cmpldi(TMP1, imm(1));
    a.ble(small);

    {
        a.mr(XREG0, ARG1);

        emit_gc_test(ArgWord(0),
                     ArgWord(ERTS_MAX_UINT64_HEAP_SIZE),
                     ArgWord(0));

        mov_imm(TMP1, make_pos_bignum_header(1));
        a.std(TMP1, ppc::Mem(HTOP));
        a.std(XREG0, ppc::Mem(HTOP, sizeof(Eterm)));
        a.addi(HTOP, HTOP, imm(sizeof(Eterm[2])));
        a.addi(XREG0, HTOP, imm(-(Sint)(sizeof(Eterm[2]) - TAG_PRIMARY_BOXED)));
        a.b(next);
    }

    a.bind(small);
    {
        a.sldi(ARG1, ARG1, _TAG_IMMED1_SIZE);
        a.ori(XREG0, ARG1, imm(_TAG_IMMED1_SMALL));
    }

    a.bind(next);
}

void BeamModuleAssembler::emit_mark_unreachable() {
    mark_unreachable();
}

void BeamModuleAssembler::emit_coverage(void *coverage, Uint index, Uint size) {
    Uint address = Uint(coverage) + index * size;
    comment("coverage index = %d", index);

    mov_imm(TMP1, address);
    if (size == sizeof(Uint)) {
        Label again = a.new_label();

        /* The instructions that load and reserve, and store conditionally,
         * only exist in an indexed form. */
        a.li(LR_TMP, imm(0));

        a.bind(again);
        {
            a.ldarx(TMP2, ppc::Mem(TMP1, LR_TMP));
            a.addi(TMP2, TMP2, imm(1));
            a.stdcx_rc(TMP2, ppc::Mem(TMP1, LR_TMP));
            a.bne(again);
        }
    } else if (size == sizeof(byte)) {
        if ((address & 0xff) != 0) {
            a.stb(TMP1, ppc::Mem(TMP1));
        } else {
            mov_imm(TMP2, 1);
            a.stb(TMP2, ppc::Mem(TMP1));
        }
    } else {
        ASSERT(0);
    }
}

void BeamModuleAssembler::emit_i_debug_line(const ArgWord &Loc,
                                            const ArgWord &Index,
                                            const ArgWord &Live) {
    emit_validate(Live);

    /*
     * We store live in TMP1, which will be used in case the line-breakpoint
     * is enabled in the trampoline that follows. Doing it here keeps the.
     * trampoline logic simpler
     */
    ASSERT(Live.get() <= MAX_ARG);
    mov_imm(TMP1, Live.get());

    /* The trampoline code for a line-breakpoint needs to be aligned to
     * a word, so that changing the code at runtime to enable the breakpoint
     * happens atomically. Notice this is emitted before the current offset
     * is added to the line-table.
     */
    a.align(AlignMode::kCode, 8);
}
