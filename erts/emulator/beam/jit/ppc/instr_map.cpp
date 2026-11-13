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
#include "beam_asm.hpp"

using namespace asmjit;

extern "C"
{
#include "erl_map.h"
#include "erl_term_hashing.h"
#include "beam_common.h"
}

/* ARG2 = term
 *
 * Helper for calculating the internal hash of keys before looking them up in a
 * map. This is a manual expansion of `erts_internal_hash`, and all changes to
 * that function must be mirrored here.
 *
 * Result in ARG3. Clobbers TMP1. */
void BeamGlobalAssembler::emit_internal_hash_helper() {
    ppc::Gp key = ARG2, key_hash = ARG3;

    /* key_hash = key ^ (key >> 33); */
    a.srdi(TMP1, key, 33);
    a.xor_(key_hash, key, TMP1);

    /* key_hash *= 0xFF51AFD7ED558CCDull */
    mov_imm(TMP1, 0xFF51AFD7ED558CCDull);
    a.mulld(key_hash, key_hash, TMP1);

    /* key_hash ^= key_hash >> 33; */
    a.srdi(TMP1, key_hash, 33);
    a.xor_(key_hash, key_hash, TMP1);

    /* key_hash *= 0xC4CEB9FE1A85EC53ull */
    mov_imm(TMP1, 0xC4CEB9FE1A85EC53ull);
    a.mulld(key_hash, key_hash, TMP1);

    /* key_hash ^= key_hash >> 33; */
    a.srdi(TMP1, key_hash, 33);
    a.xor_(key_hash, key_hash, TMP1);

#ifdef DBG_HASHMAP_COLLISION_BONANZA
    emit_enter_runtime_frame();
    emit_enter_runtime();

    a.std(ARG1, TMP_MEM1q);
    a.std(ARG2, TMP_MEM2q);
    a.std(ARG4, TMP_MEM3q);

    a.mr(ARG1, ARG3);
    runtime_call<erts_ihash_t (*)(erts_ihash_t, Eterm),
                 erts_dbg_hashmap_collision_bonanza>();
    a.mr(ARG3, ARG1);

    a.ld(ARG1, TMP_MEM1q);
    a.ld(ARG2, TMP_MEM2q);
    a.ld(ARG4, TMP_MEM3q);

    emit_leave_runtime();
    emit_leave_runtime_frame();
#endif
}

/* ARG1 = untagged hash map root
 * ARG2 = key
 * ARG3 = key hash
 * ARG4 = node header
 *
 * Result is returned in RET. CR0.EQ is set on success. */
void BeamGlobalAssembler::emit_hashmap_get_element() {
    Label node_loop = a.new_label();

    ppc::Gp node = ARG1, key = ARG2, key_hash = ARG3, header_val = ARG4,
            depth = TMP5, index = TMP6;

    const int header_shift =
            (_HEADER_ARITY_OFFS + MAP_HEADER_TAG_SZ + MAP_HEADER_ARITY_SZ);

    /* Skip the root header, which is two words long (child headers are one
     * word). */
    a.addi(node, node, imm(sizeof(Eterm[2])));
    mov_imm(depth, 0);

    a.bind(node_loop);
    {
        Label done = a.new_label(), not_found = a.new_label(),
              leaf_node = a.new_label(), skip_index_adjustment = a.new_label(),
              collision_node = a.new_label();

        /* Find out which child we should follow, and shift the hash for the
         * next round. */
        a.andi_rc(index, key_hash, imm(0xF));
        a.srdi(key_hash, key_hash, 4);
        a.addi(depth, depth, imm(1));

        /* The entry offset is always equal to the index on fully populated
         * nodes, so we'll skip adjusting them.
         *
         * The bitmap is the upper half of the lower 32 bits of the header.
         * Shifting it down sign-extends it, so that it is -1 when all bits
         * are set. */
        ERTS_CT_ASSERT(header_shift == 16);
        a.srawi(header_val, header_val, imm(header_shift));
        a.cmpdi(header_val, imm(-1));
        a.beq(skip_index_adjustment);
        {
            /* If our bit isn't set on this node, the key can't be found. */
            a.srd(TMP1, header_val, index);
            a.andi_rc(LR_TMP, TMP1, imm(1));
            a.beq(not_found);

            /* The actual offset of our entry is the number of bits set (in
             * essence "entries present") before our index in the bitmap. Clear
             * the upper bits and count the remainder. */
            a.sld(TMP2, TMP1, index);
            a.xor_(TMP1, TMP2, header_val);
            a.popcntd(index, TMP1);
        }
        a.bind(skip_index_adjustment);

        a.sldi(TMP1, index, 3);
        a.ldx(TMP1, ppc::Mem(node, TMP1));
        emit_untag_ptr(node, TMP1);

        /* Have we found our leaf? */
        emit_is_boxed(leaf_node, TMP1);

        /* Nope, we have to search another node. Read and skip past the header
         * word. */
        a.ld(header_val, ppc::Mem(node));
        a.addi(node, node, imm(sizeof(Eterm)));

        /* After 8/16 nodes we've run out of the hash bits we've started with
         * and we end up in a collision node. */
        a.cmpdi(depth, imm(HAMT_MAX_LEVEL));
        a.bne(node_loop);
        a.b(collision_node);

        a.bind(leaf_node);
        {
            /* We've arrived at a leaf, set CR0.EQ according to whether its key
             * matches ours and speculatively place the element in ARG1. */
            a.ld(TMP1, ppc::Mem(node));
            a.ld(ARG1, ppc::Mem(node, sizeof(Eterm)));
            a.cmpd(TMP1, key);

            a.bind(done);
            a.blr();
        }

        /* The test that brought us here has set CR0.EQ, which would tell the
         * caller that the key was found. */
        a.bind(not_found);
        {
            a.crclr(ppc::cr0.eq());
            a.blr();
        }

        /* A collision node is a tuple of leafs where we do linear search.*/
        a.bind(collision_node);
        {
            Label linear_loop = a.new_label();

            a.srdi(TMP1, header_val, _HEADER_ARITY_OFFS - 3);

            a.bind(linear_loop);
            {
                a.addi(TMP1, TMP1, imm(-8));

                a.ldx(TMP2, ppc::Mem(node, TMP1));

                emit_untag_ptr(TMP2, TMP2);
                a.ld(TMP3, ppc::Mem(TMP2));
                a.ld(TMP4, ppc::Mem(TMP2, sizeof(Eterm)));
                a.cmpd(key, TMP3);
                a.isel(ARG1, TMP4, node, ppc::cr0.eq());
                a.beq(done);

                /* The loop counter is tested in CR1 to leave CR0.EQ clear
                 * when we run out of leafs. */
                a.cmpdi(ppc::cr1, TMP1, imm(0));
                a.bne(ppc::cr1, linear_loop);
            }

            a.blr();
        }
    }
}

/* ARG1 = untagged flat map
 * ARG2 = key
 * ARG5 = size
 *
 * Result is returned in ARG1. CR0.EQ is set on success. */
void BeamGlobalAssembler::emit_flatmap_get_element() {
    Label fail = a.new_label(), loop = a.new_label();

    /* Bump size by 1 to slide past the `keys` field in the map, and the header
     * word in the key array. We count in bytes to be able to use the size as
     * an index. */
    a.addi(ARG5, ARG5, imm(1));
    a.sldi(ARG5, ARG5, 3);

    /* Adjust our map pointer to the `keys` field before loading it. This
     * saves us from having to bump it to point at the values later on. */
    a.ldu(TMP1, ppc::Mem(ARG1, offsetof(flatmap_t, keys)));
    emit_untag_ptr(TMP1, TMP1);

    a.bind(loop);
    {
        a.addic_rc(ARG5, ARG5, imm(-(Sint)sizeof(Eterm)));
        a.beq(fail);

        a.ldx(TMP4, ppc::Mem(TMP1, ARG5));
        a.cmpd(ARG2, TMP4);
        a.bne(loop);
    }

    a.ldx(ARG1, ppc::Mem(ARG1, ARG5));
    a.blr();

    /* The test that brought us here has set CR0.EQ, which would tell the
     * caller that the key was found. */
    a.bind(fail);
    a.crclr(ppc::cr0.eq());
    a.blr();
}

void BeamGlobalAssembler::emit_new_map_shared() {
    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    runtime_call<Eterm (*)(Process *, Eterm *, Uint, Uint, const Eterm *),
                 erts_gc_new_map>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();
    emit_leave_runtime_frame();

    a.blr();
}

void BeamModuleAssembler::emit_new_map(const ArgRegister &Dst,
                                       const ArgWord &Live,
                                       const ArgWord &Size,
                                       const Span<const ArgVal> &args) {
    embed_vararg_rodata(args, ARG5);

    mov_arg(ARG3, Live);
    mov_imm(ARG4, args.size());
    fragment_call(ga->get_new_map_shared());

    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_i_new_small_map_lit(
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgLiteral &Keys,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    ASSERT(Size.get() == args.size());

    emit_gc_test(ArgWord(0),
                 ArgWord(args.size() + MAP_HEADER_FLATMAP_SZ + 1),
                 Live);

    std::vector<ArgVal> data;
    data.reserve(args.size() + MAP_HEADER_FLATMAP_SZ + 1);
    data.push_back(ArgWord(MAP_HEADER_FLATMAP));
    data.push_back(Size);
    data.push_back(Keys);
    data.insert(data.end(), std::begin(args), std::end(args));

    size_t size = data.size();

    /* A small map is far too small for the displacement to overflow. */
    ASSERT((ssize_t)(size * sizeof(Eterm)) <= MAX_DISPLACEMENT);

    for (size_t i = 0; i < size; i++) {
        if ((i % 128) == 0) {
            check_pending_stubs();
        }

        auto src = load_source(data[i], TMP2);
        a.std(src.reg, ppc::Mem(HTOP, i * sizeof(Eterm)));
    }

    add(HTOP, HTOP, size * sizeof(Eterm));

    /* The destination is set last as it may be one of the sources. */
    auto ptr = init_destination(Dst, TMP1);
    sub(ptr.reg, HTOP, size * sizeof(Eterm) - TAG_PRIMARY_BOXED);
    flush_var(ptr);
}

/* ARG1 = map
 * ARG2 = key
 *
 * Result is returned in RET. CR0.EQ is set on success. */
void BeamGlobalAssembler::emit_i_get_map_element_shared() {
    Label generic = a.new_label(), hashmap = a.new_label();

    a.andi_rc(TMP1, ARG2, imm(_TAG_PRIMARY_MASK));
    a.cmpdi(TMP1, imm(TAG_PRIMARY_IMMED1));
    a.bne(generic);

    emit_untag_ptr(ARG1, ARG1);

    /* hashmap_get_element expects node header in ARG4, flatmap_get_element
     * expects size in ARG5 */
    a.ld(ARG4, ppc::Mem(ARG1, offsetof(flatmap_t, thing_word)));
    a.ld(ARG5, ppc::Mem(ARG1, offsetof(flatmap_t, size)));
    a.andi_rc(TMP1, ARG4, imm(_HEADER_MAP_SUBTAG_MASK));
    a.cmpdi(TMP1, imm(HAMT_SUBTAG_HEAD_FLATMAP));
    a.bne(hashmap);

    emit_flatmap_get_element();

    a.bind(generic);
    {
        emit_enter_runtime_frame();

        emit_enter_runtime();
        runtime_call<Eterm (*)(Eterm, Eterm), get_map_element>();
        emit_leave_runtime();

        /* Invert CR0.EQ, we want it to be set when RET is a value. */
        cmp(ARG1, THE_NON_VALUE);
        a.crnot(ppc::cr0.eq(), ppc::cr0.eq());

        emit_leave_runtime_frame();
        a.blr();
    }

    a.bind(hashmap);
    {
        /* Calculate the internal hash of the key before diving into the
         * HAMT. */
        emit_internal_hash_helper();
        emit_hashmap_get_element();
    }
}

void BeamModuleAssembler::emit_i_get_map_element(const ArgLabel &Fail,
                                                 const ArgRegister &Src,
                                                 const ArgRegister &Key,
                                                 const ArgRegister &Dst) {
    mov_arg(ARG1, Src);
    mov_arg(ARG2, Key);

    if (maybe_one_of<BeamTypeId::MaybeImmediate>(Key)) {
        fragment_call(ga->get_i_get_map_element_shared());
        a.bne(resolve_beam_label(Fail, disp32K));
    } else {
        emit_enter_runtime();
        runtime_call<Eterm (*)(Eterm, Eterm), get_map_element>();
        emit_leave_runtime();

        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    }

    /*
     * Don't store the result if the destination is the scratch X register.
     * (This instruction was originally a has_map_fields instruction.)
     */
    if (!(Dst.isXRegister() && Dst.as<ArgXRegister>().get() == SCRATCH_X_REG)) {
        mov_arg(Dst, ARG1);
    }
}

void BeamModuleAssembler::emit_i_get_map_elements(
        const ArgLabel &Fail,
        const ArgSource &Src,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    Label generic = a.new_label(), next = a.new_label();

    /* We're not likely to gain much from inlining huge extractions, and the
     * resulting code is quite large, so we'll cut it off after a handful
     * elements.
     *
     * Note that the arguments come in flattened triplets of
     * `{Key, Dst, KeyHash}` */
    bool can_inline = args.size() < (8 * 3);

    ASSERT(Size.get() == args.size());
    ASSERT((Size.get() % 3) == 0);

    for (size_t i = 0; i < args.size(); i += 3) {
        can_inline &= args[i].isImmed();
    }

    mov_arg(ARG1, Src);

    if (can_inline) {
        comment("simplified multi-element lookup");

        emit_untag_ptr(TMP1, ARG1);

        a.ld(TMP2, ppc::Mem(TMP1, offsetof(flatmap_t, thing_word)));
        a.ld(TMP3, ppc::Mem(TMP1, offsetof(flatmap_t, size)));
        a.andi_rc(TMP2, TMP2, imm(_HEADER_MAP_SUBTAG_MASK));
        a.cmpdi(TMP2, imm(HAMT_SUBTAG_HEAD_FLATMAP));
        a.bne(generic);

        check_pending_stubs();

        /* Bump size by 1 to slide past the `keys` field in the map, and the
         * header word in the key array. We count in bytes to be able to use
         * the size as an index. */
        a.addi(TMP3, TMP3, imm(1));
        a.sldi(TMP3, TMP3, 3);

        /* Adjust our map pointer to the `keys` field before loading it. This
         * saves us from having to bump it to point at the values later on. */
        ERTS_CT_ASSERT(sizeof(flatmap_t) ==
                       offsetof(flatmap_t, keys) + sizeof(Eterm));
        a.ldu(TMP2, ppc::Mem(TMP1, offsetof(flatmap_t, keys)));
        emit_untag_ptr(TMP2, TMP2);

        for (ssize_t i = args.size() - 3; i >= 0; i -= 3) {
            Label loop = a.new_label();

            a.bind(loop);
            {
                a.addic_rc(TMP3, TMP3, imm(-(Sint)sizeof(Eterm)));
                a.beq(resolve_beam_label(Fail, disp32K));

                a.ldx(TMP4, ppc::Mem(TMP2, TMP3));

                const auto &Comparand = args[i];
                cmp_arg(TMP4, Comparand);
                a.bne(loop);
            }

            /* Don't store the result if the destination is the scratch X
             * register. (This instruction was originally a has_map_fields
             * instruction.) */
            const auto &Dst = args[i + 1];
            if (!(Dst.isXRegister() &&
                  Dst.as<ArgXRegister>().get() == SCRATCH_X_REG)) {
                auto dst = init_destination(Dst, SUPER_TMP);
                a.ldx(dst.reg, ppc::Mem(TMP1, TMP3));
                flush_var(dst);
            }
        }

        a.b(next);
    }

    a.bind(generic);
    {
        embed_vararg_rodata(args, ARG5);

        a.mr(ARG3, E);

        emit_enter_runtime<Update::eXRegs>();

        mov_imm(ARG4, args.size() / 3);
        load_x_reg_array(ARG2);
        runtime_call<Eterm (*)(Eterm, Eterm *, Eterm *, Uint, Eterm *),
                     beam_jit_get_map_elements>();

        emit_leave_runtime<Update::eXRegs>();

        a.cmpdi(ARG1, imm(0));
        a.beq(resolve_beam_label(Fail, disp32K));
    }

    a.bind(next);
}

/* ARG1 = map
 * ARG2 = key
 * ARG3 = key hash
 *
 * Result is returned in RET. CR0.EQ is set on success. */
void BeamGlobalAssembler::emit_i_get_map_element_hash_shared() {
    Label hashmap = a.new_label();

    emit_untag_ptr(ARG1, ARG1);

    /* hashmap_get_element expects node header in ARG4, flatmap_get_element
     * expects size in ARG5 */
    a.ld(ARG4, ppc::Mem(ARG1, offsetof(flatmap_t, thing_word)));
    a.ld(ARG5, ppc::Mem(ARG1, offsetof(flatmap_t, size)));
    a.andi_rc(TMP1, ARG4, imm(_HEADER_MAP_SUBTAG_MASK));
    a.cmpdi(TMP1, imm(HAMT_SUBTAG_HEAD_FLATMAP));
    a.bne(hashmap);
    emit_flatmap_get_element();

    a.bind(hashmap);
    emit_hashmap_get_element();
}

void BeamModuleAssembler::emit_i_get_map_element_hash(const ArgLabel &Fail,
                                                      const ArgRegister &Src,
                                                      const ArgConstant &Key,
                                                      const ArgWord &Hx,
                                                      const ArgRegister &Dst) {
    mov_arg(ARG1, Src);
    mov_arg(ARG2, Key);
    mov_arg(ARG3, Hx);

    if (Key.isImmed()) {
        fragment_call(ga->get_i_get_map_element_hash_shared());
        a.bne(resolve_beam_label(Fail, disp32K));
    } else {
        emit_enter_runtime();
        runtime_call<Eterm (*)(Eterm, Eterm, erts_ihash_t),
                     get_map_element_hash>();
        emit_leave_runtime();

        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    }

    /*
     * Don't store the result if the destination is the scratch X register.
     * (This instruction was originally a has_map_fields instruction.)
     */
    if (!(Dst.isXRegister() && Dst.as<ArgXRegister>().get() == SCRATCH_X_REG)) {
        mov_arg(Dst, ARG1);
    }
}

/* ARG3 = live registers, ARG4 = update vector size, ARG5 = update vector. */
void BeamGlobalAssembler::emit_update_map_assoc_shared() {
    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    runtime_call<Eterm (*)(Process *, Eterm *, Uint, Uint, const Eterm *),
                 erts_gc_update_map_assoc>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();
    emit_leave_runtime_frame();

    a.blr();
}

/* ARG2 = key
 * ARG3 = value
 * ARG4 = map
 */
void BeamGlobalAssembler::emit_update_map_single_assoc_shared() {
    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc>();

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *, Eterm, Eterm, Eterm), erts_maps_put>();

    emit_leave_runtime<Update::eHeapAlloc>();
    emit_leave_runtime_frame();

    a.blr();
}

void BeamModuleAssembler::emit_update_map_assoc(
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    ASSERT(Size.get() == args.size());

    if (args.size() == 2) {
        mov_arg(ARG2, args[0]);
        mov_arg(ARG3, args[1]);
        mov_arg(ARG4, Src);
        fragment_call(ga->get_update_map_single_assoc_shared());
    } else {
        auto src = load_source(Src, TMP1);

        embed_vararg_rodata(args, ARG5);

        mov_arg(ArgXRegister(Live.get()), src.reg);
        mov_arg(ARG3, Live);
        mov_imm(ARG4, args.size());

        fragment_call(ga->get_update_map_assoc_shared());
    }
    mov_arg(Dst, ARG1);
}

/* ARG3 = live registers, ARG4 = update vector size, ARG5 = update vector.
 *
 * Result is returned in RET, which is THE_NON_VALUE on error. */
void BeamGlobalAssembler::emit_update_map_exact_guard_shared() {
    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    runtime_call<Eterm (*)(Process *, Eterm *, Uint, Uint, const Eterm *),
                 erts_gc_update_map_exact>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();
    emit_leave_runtime_frame();

    a.blr();
}

/* ARG3 = live registers, ARG4 = update vector size, ARG5 = update vector.
 *
 * Does not return on error. */
void BeamGlobalAssembler::emit_update_map_exact_body_shared() {
    Label error = a.new_label();

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    runtime_call<Eterm (*)(Process *, Eterm *, Uint, Uint, const Eterm *),
                 erts_gc_update_map_exact>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();
    emit_leave_runtime_frame();

    emit_branch_if_not_value(ARG1, error);
    a.blr();

    a.bind(error);
    {
        mov_imm(ARG4, 0);
        a.b(labels[raise_exception]);
    }
}

/* ARG2 = key
 * ARG3 = value
 * ARG4 = map
 *
 * Does not return on error. */
void BeamGlobalAssembler::emit_update_map_single_exact_body_shared() {
    Label error = a.new_label();

    a.std(ARG2, TMP_MEM2q);

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc>();

    a.mr(ARG1, c_p);
    lea(ARG5, TMP_MEM1q);
    runtime_call<int (*)(Process *, Eterm, Eterm, Eterm, Eterm *),
                 erts_maps_update>();

    emit_leave_runtime<Update::eHeapAlloc>();
    emit_leave_runtime_frame();

    a.cmpwi(ARG1, imm(0));
    a.beq(error);

    a.ld(ARG1, TMP_MEM1q);
    a.blr();

    a.bind(error);
    {
        a.ld(TMP2, TMP_MEM2q);
        mov_imm(TMP1, BADKEY);
        a.std(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
        a.std(TMP2, ppc::Mem(c_p, offsetof(Process, fvalue)));
        mov_imm(ARG4, 0);
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_update_map_exact(
        const ArgSource &Src,
        const ArgLabel &Fail,
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    ASSERT(Size.get() == args.size());

    if (args.size() == 2 && Fail.get() == 0) {
        mov_arg(ARG2, args[0]);
        mov_arg(ARG3, args[1]);
        mov_arg(ARG4, Src);
        fragment_call(ga->get_update_map_single_exact_body_shared());
    } else {
        auto src = load_source(Src, ARG4);
        embed_vararg_rodata(args, ARG5);
        mov_arg(ArgXRegister(Live.get()), src.reg);
        mov_arg(ARG3, Live);
        mov_imm(ARG4, args.size());

        if (Fail.get() != 0) {
            fragment_call(ga->get_update_map_exact_guard_shared());
            emit_branch_if_not_value(ARG1,
                                     resolve_beam_label(Fail, dispUnknown));
        } else {
            fragment_call(ga->get_update_map_exact_body_shared());
        }
    }

    mov_arg(Dst, ARG1);
}
