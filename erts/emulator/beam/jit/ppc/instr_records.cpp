/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2025-2026. All Rights Reserved.
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
#include "erl_record.h"
}

void BeamModuleAssembler::emit_is_any_native_record(const ArgLabel &Fail,
                                                    const ArgRegister &Src) {
    auto src = load_source(Src, ARG3);

    emit_is_boxed(resolve_beam_label(Fail, dispUnknown), Src, src.reg);

    preserve_cache(
            [&]() {
                ppc::Gp boxed_ptr = emit_ptr_val(TMP3, src.reg);
                load_mem(TMP3, emit_boxed_val(boxed_ptr));
                a.andi_rc(TMP3, TMP3, imm(_TAG_HEADER_MASK));
                a.cmpdi(TMP3, imm(_TAG_HEADER_RECORD));
                a.bne(resolve_beam_label(Fail, disp32K));
            },
            TMP3);
}

void BeamModuleAssembler::emit_is_native_record(const ArgLabel &Fail,
                                                const ArgRegister &Src,
                                                const ArgAtom &Module,
                                                const ArgAtom &Name) {
    auto src = load_source(Src, ARG3);

    preserve_cache(
            [&]() {
                ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
                load_mem(TMP1,
                         emit_boxed_val(boxed_ptr,
                                        offsetof(ErtsRecordInstance,
                                                 record_definition)));
                boxed_ptr = emit_ptr_val(TMP1, TMP1);
                load_mem(
                        TMP2,
                        emit_boxed_val(boxed_ptr,
                                       offsetof(ErtsRecordDefinition, module)));
                load_mem(TMP3,
                         emit_boxed_val(boxed_ptr,
                                        offsetof(ErtsRecordDefinition, name)));

                cmp(TMP2, Module.get());
                cmp(TMP3, Name.get(), ppc::cr1);
                a.crand(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
                a.bne(resolve_beam_label(Fail, disp32K));
            },
            TMP1,
            TMP2,
            TMP3);
}

void BeamModuleAssembler::emit_is_record_accessible(const ArgLabel &Fail,
                                                    const ArgRegister &Src,
                                                    const ArgAtom &Scope) {
    auto src = load_source(Src, ARG3);

    preserve_cache(
            [&]() {
                ppc::Gp boxed_ptr = emit_ptr_val(TMP1, src.reg);
                load_mem(TMP1,
                         emit_boxed_val(boxed_ptr,
                                        offsetof(ErtsRecordInstance,
                                                 record_definition)));
                boxed_ptr = emit_ptr_val(TMP1, TMP1);
                load_mem(TMP2,
                         emit_boxed_val(
                                 boxed_ptr,
                                 offsetof(ErtsRecordDefinition, is_exported)));
                if (Scope.get() == am_external) {
                    const Uint bit_num = _TAG_IMMED2_SIZE;
                    ERTS_CT_ASSERT(am_false == make_atom(0));
                    ERTS_CT_ASSERT(am_true == make_atom(1));
                    ERTS_CT_ASSERT((1 << bit_num) == (am_true - am_false));

                    comment("external operation");
                    a.andi_rc(LR_TMP, TMP2, imm(1 << bit_num));
                    a.beq(resolve_beam_label(Fail, disp32K));
                } else {
                    comment("auto_local operation");
                    cmp(TMP2, am_true);
                    load_mem(TMP2,
                             emit_boxed_val(
                                     boxed_ptr,
                                     offsetof(ErtsRecordDefinition, module)));
                    cmp(TMP2, mod, ppc::cr1);
                    a.cror(ppc::cr0.eq(), ppc::cr0.eq(), ppc::cr1.eq());
                    a.bne(resolve_beam_label(Fail, disp32K));
                }
            },
            TMP1,
            TMP2);
}

void BeamModuleAssembler::emit_i_get_local_record_elements(
        const ArgLiteral &Def,
        const ArgRegister &Src,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    Eterm def;
    ErtsRecordDefinition *defp;
    int field_count;
    Eterm cons = beamfile_get_literal(beam, Def.get());
    int argp;

    def = CAR(list_val(cons));
    defp = (ErtsRecordDefinition *)tuple_val(def);

    field_count = RECORD_DEF_FIELD_COUNT(defp);

    comment("name: %T", defp->name);
    auto src = load_source(Src, ARG1);
    auto values = emit_ptr_val(ARG1, src.reg);
    add(ARG1, values, offsetof(ErtsRecordInstance, values) - TAG_PRIMARY_BOXED);

    argp = 0;
    for (int i = 0; i < field_count; i++) {
        if ((i % 128) == 0) {
            check_pending_stubs();
        }

        if (argp + 1 < args.size() &&
            args[argp].as<ArgAtom>().get() == defp->keys[i]) {
            auto dst = init_destination(args[argp + 1], TMP1);

            load_mem(dst.reg, ppc::Mem(ARG1, i * sizeof(Eterm)));
            flush_var(dst);
            argp += 2;
        }
    }
}

void BeamModuleAssembler::emit_i_get_record_elements(
        const ArgLabel &Fail,
        const ArgRegister &Src,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    mov_arg(ARG3, Src);
    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    mov_imm(ARG4, args.size());
    embed_vararg_rodata(args, ARG5);

    emit_enter_runtime<Update::eStack | Update::eXRegs>();

    runtime_call<bool (*)(Process *, Eterm *, Eterm, Uint, const Eterm *),
                 erl_get_record_elements>();

    emit_leave_runtime<Update::eXRegs>();

    if (Fail.get() != 0) {
        a.andi_rc(LR_TMP, ARG1, imm(1));
        a.beq(resolve_beam_label(Fail, disp32K));
    }
}

void BeamModuleAssembler::emit_i_create_local_native_record(
        const ArgLiteral &Def,
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgWord &size,
        const Span<const ArgVal> &args) {
    Eterm def;
    ErtsRecordDefinition *defp;
    int field_count;
    Uint num_words_needed;
    Eterm *loader_def_values;
    Eterm cons = beamfile_get_literal(beam, Def.get());
    int argp;
    bool any_literal_defaults = false;
    bool otp_29 = beam->code.max_opcode <= genop_get_record_field_5;

    def = CAR(list_val(cons));
    defp = (ErtsRecordDefinition *)tuple_val(def);
    loader_def_values = tuple_val(CDR(list_val(cons))) + 1;

    field_count = RECORD_DEF_FIELD_COUNT(defp);
    num_words_needed = RECORD_INST_SIZE(field_count);

    comment("name: %T", defp->name);

    /* Find out whether any non-immediate default value will
     * be used for this construction. */
    argp = 0;
    for (int i = 0; i < field_count; i++) {
        if (argp < args.size() &&
            args[argp].as<ArgAtom>().get() == defp->keys[i]) {
            argp += 2;
        } else {
            Eterm value = loader_def_values[i];
            any_literal_defaults |= !is_immed(value);
        }
    }

    if (otp_29) {
        /* If compiled by OTP 29, we must do a GC test here.
         * If compiled by OTP 30 or later, this instruction is
         * preceded by a `test_heap` instruction that has already
         * ensured sufficient heap space. */
        emit_gc_test(ArgWord(0), ArgWord(num_words_needed), Live);
    }

    ppc::Gp def_values;
    mov_arg(TMP3, Def);
    if (any_literal_defaults) {
        /* At least one default value is a literal. Literals are not
         * yet at their final location. We must emit code that set up
         * a pointer (ARG3) to the beginning of the default values. */
        emit_untag_ptr(TMP3, TMP3);
        a.ld(TMP2, ppc::Mem(TMP3));
        a.ld(ARG3, ppc::Mem(TMP3, sizeof(Eterm)));
        def_values = emit_ptr_val(ARG3, ARG3);
        add(ARG3, def_values, sizeof(Eterm) - TAG_PRIMARY_BOXED);
        def_values = ARG3;
    } else {
        /* We will not need a pointer to the default values. */
        ppc::Gp cons_ptr = emit_ptr_val(TMP3, TMP3);
        load_mem(TMP2, getCARRef(cons_ptr));
    }

    /* Store header word and pointer to the definition. The words are
     * stored at fixed offsets from HTOP, which is updated once at the
     * end. */
    Uint heap_offset = 0;
    mov_imm(TMP1, MAKE_RECORD_HEADER(field_count));
    store_mem(TMP1, ppc::Mem(HTOP, heap_offset));
    heap_offset += sizeof(Eterm);
    store_mem(TMP2, ppc::Mem(HTOP, heap_offset));
    heap_offset += sizeof(Eterm);

    /* We'll keep a cache of loaded immediate values. */
    ImmedRegCache<11> values(*this,
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

    argp = 0;
    for (int i = 0; i < field_count; i++) {
        const ppc::Gp default_reg = ARG1;
        ppc::Gp reg;

        if ((i % 128) == 0) {
            check_pending_stubs();
        }

        if (argp < args.size() &&
            args[argp].as<ArgAtom>().get() == defp->keys[i]) {
            if (args[argp + 1].isImmed()) {
                Eterm value = args[argp + 1].as<ArgImmed>().get();
                reg = values.load_value(value);
            } else {
                reg = load_source(args[argp + 1], default_reg).reg;
            }
            argp += 2;
        } else {
            Eterm value = loader_def_values[i];
            if (is_immed(value)) {
                reg = values.load_value(value);
            } else {
                ASSERT(any_literal_defaults);
                reg = default_reg;
                load_mem(reg, ppc::Mem(def_values, i * sizeof(Eterm)));
            }
        }

        store_mem(reg, ppc::Mem(HTOP, heap_offset));
        heap_offset += sizeof(Eterm);
    }

    ASSERT(heap_offset == num_words_needed * sizeof(Eterm));

    auto ptr = init_destination(Dst, TMP1);
    a.addi(ptr.reg, HTOP, imm(TAG_PRIMARY_BOXED));
    add(HTOP, HTOP, heap_offset);
    flush_var(ptr);
}

/*
 * ARG4 = Id
 * ARG5 = (size << 10) | live
 * ARG6 = args
 */
void BeamGlobalAssembler::emit_create_native_record_shared() {
    Label handle_trap = a.new_label();
    Label ret = a.new_label();
    Label finish_create = a.new_label();

    /* It is essential that the continuation pointer is stored
     * on the stack for the Erlang process, because `finish_create`
     * might execute on a different scheduler thread (with a
     * different runtime stack). */
    emit_enter_erlang_frame();

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    a.mr(ARG3, active_code_ix);

    runtime_call<Eterm (*)(Process *,
                           Eterm *,
                           ErtsCodeIndex,
                           Eterm,
                           Uint,
                           const Eterm *),
                 erl_create_native_record_jit>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();
    emit_leave_runtime_frame();
    emit_branch_if_not_value(ARG1, handle_trap);

    a.bind(ret);
    {
        emit_leave_erlang_frame();
        a.blr();
    }

    a.bind(handle_trap);
    {
        a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
        emit_branch_if_ne(TMP1, TRAP, ret);

        /* Ensure that control is transferred to `finish_create` after
         * having loaded the code. erl_create_native_record_jit() has
         * reserved the word at the top of the stack for this
         * continuation pointer. */
        emit_adr(TMP1, finish_create);
        a.std(TMP1, ppc::Mem(E));

        /* Dispatch directly to the trap code. There is no need to do
         * a context switch. */
        a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, i)));
        a.mtctr(TMP1);
        a.bctr();
    }

    a.bind(finish_create);
    {
        /* Now, the module is probably loaded. Again try to create the
         * record. */

        emit_enter_runtime_frame();
        emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                           Update::eReductions>();

        a.mr(ARG1, c_p);
        load_x_reg_array(ARG2);
        a.mr(ARG3, active_code_ix);

        runtime_call<Eterm (*)(Process *, Eterm *, ErtsCodeIndex),
                     erl_finish_create_record_jit>();

        emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                           Update::eReductions | Update::eCodeIndex>();
        emit_leave_runtime_frame();

        emit_leave_erlang_frame();
        a.blr();
    }
}

void BeamModuleAssembler::emit_i_create_native_record(
        const ArgLabel &Fail,
        const ArgConstant &Id,
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgWord &size,
        const Span<const ArgVal> &args) {
    Uint size_live = (args.size() << 10) | Live.get();

    mov_arg(ARG4, Id);
    mov_imm(ARG5, size_live);
    embed_vararg_rodata(args, ARG6);

    fragment_call(ga->get_create_native_record_shared());

    if (Fail.get() != 0) {
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        Label next = a.new_label();

        emit_branch_if_value(ARG1, next);

        emit_raise_exception();

        a.bind(next);
    }
    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_i_update_local_native_record(
        const ArgAtom &Hint,
        const ArgLiteral &Def,
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgWord &Size,
        const Span<const ArgVal> &args) {
    Eterm cons = beamfile_get_literal(beam, Def.get());
    Eterm def = CAR(list_val(cons));
    ErtsRecordDefinition *defp = (ErtsRecordDefinition *)tuple_val(def);
    int field_count = RECORD_DEF_FIELD_COUNT(defp);

    comment("name: %T", defp->name);

    /* Create the `updates` vector, mapping from positions to
     * values. */
    int argp = 0;
    std::vector<ArgVal> updates;
    updates.reserve(args.size());
    const Uint header_offset = RECORD_INST_SIZE(0);
    for (int i = 0; i < field_count; i++) {
        if (argp < args.size() &&
            args[argp].as<ArgAtom>().get() == defp->keys[i]) {
            updates.emplace(updates.end(), ArgWord(header_offset + i));
            updates.emplace(updates.end(), args[argp + 1]);
            argp += 2;
        }
    }

    /* Reuse the code for updating a tuple record. */
    Span<const ArgVal> Updates(updates.data(), updates.size());
    emit_update_any_record(Hint,
                           RECORD_INST_SIZE(field_count),
                           Src,
                           Dst,
                           Size,
                           Updates);
}

void BeamModuleAssembler::emit_i_update_native_record(
        const ArgLabel &Fail,
        const ArgSource &Src,
        const ArgRegister &Dst,
        const ArgWord &Live,
        const ArgWord &size,
        const Span<const ArgVal> &args) {
    mov_arg(ARG3, Src);
    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    mov_arg(ARG4, Live);
    mov_imm(ARG5, args.size());
    embed_vararg_rodata(args, ARG6);

    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    runtime_call<
            Eterm (*)(Process *, Eterm *, Eterm, Uint, Uint, const Eterm *args),
            erl_update_native_record>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs |
                       Update::eReductions>();

    if (Fail.get() != 0) {
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        Label next = a.new_label();

        emit_branch_if_value(ARG1, next);
        emit_raise_exception();

        a.bind(next);
    }
    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_get_record_field(const ArgLabel &Fail,
                                                const ArgRegister &Src,
                                                const ArgConstant &Id,
                                                const ArgAtom &Name,
                                                const ArgRegister &Dst) {
    a.mr(ARG1, c_p);
    mov_arg(ARG2, Src);
    mov_arg(ARG3, Id);
    mov_arg(ARG4, Name);

    emit_enter_runtime<Update::eHeapAlloc>();
    if (Id.isImmed()) {
        comment("local record");
        runtime_call<Eterm (*)(Process *, Eterm, Eterm, Eterm),
                     erl_get_local_record_field>();
    } else {
        comment("external record");
        runtime_call<Eterm (*)(Process *, Eterm, Eterm, Eterm),
                     erl_get_record_field>();
    }
    emit_leave_runtime<Update::eHeapAlloc>();

    if (Fail.get() != 0) {
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        Label next = a.new_label();

        emit_branch_if_value(ARG1, next);
        emit_raise_exception();

        a.bind(next);
    }

    mov_arg(Dst, ARG1);
}
