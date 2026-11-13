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
#include "beam_common.h"
#include "code_ix.h"
#include "erl_bif_table.h"
#include "erl_nfunc_sched.h"
#include "bif.h"
#include "erl_msacc.h"
}

void BeamModuleAssembler::ubif_comment(const ArgWord &Bif) {
    if (logger.file()) {
        ErtsCodeMFA *mfa = ubif2mfa((void *)Bif.get());
        if (mfa) {
            comment("UBIF: %T/%d", mfa->function, mfa->arity);
        }
    }
}

/* ARG2 = argument vector, ARG4 (!) = bif function pointer
 *
 * Result is returned in ARG1 (will be THE_NON_VALUE if the BIF call failed). */
void BeamGlobalAssembler::emit_i_bif_guard_shared() {
    /* We use the X register array for the arguments for the BIF. The
     * actual contents of the first three X registers are kept safe in
     * callee-saved machine registers (XREG0 through XREG2).
     */
    ERTS_CT_ASSERT(ERTS_HIGHEST_CALLEE_SAVE_XREG >= 2);

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eReductions>();

    a.mr(ARG1, c_p);
    lea(ARG2, getXRef(0));
    mov_imm(ARG3, 0);
    dynamic_runtime_call<3>(ARG4); /* ARG3 is never used by guard BIFs. */

    emit_leave_runtime<Update::eReductions>();
    emit_leave_runtime_frame();
    a.blr();
}

/* ARG2 = argument vector, ARG4 (!) = bif function pointer
 *
 * Result is returned in RET. */
void BeamGlobalAssembler::emit_i_bif_body_shared() {
    Label error = a.new_label();

    /* See comment in emit_i_bif_guard_shared. */
    ERTS_CT_ASSERT(ERTS_HIGHEST_CALLEE_SAVE_XREG >= 2);

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eReductions>();

    /* Save current BIF for the error path. */
    a.mr(ARG1, c_p);
    lea(ARG2, getXRef(0));
    a.std(ARG4, TMP_MEM1q);
    mov_imm(ARG3, 0); /* ARG3 is never used by guard BIFs. */

    dynamic_runtime_call<3>(ARG4);
    emit_branch_if_not_value(ARG1, error);

    emit_leave_runtime<Update::eReductions>();

    emit_leave_runtime_frame();
    a.blr();

    a.bind(error);
    {
        /* Find the correct MFA from the BIF's function address. */
        a.ld(ARG1, TMP_MEM1q);
        runtime_call<ErtsCodeMFA *(*)(void *), ubif2mfa>();

        /* The argument registers must be reloaded on error, as the machine
         * registers may contain garbage, which will later be swapped into the
         * register array in the `raise_exception` fragment. */
        emit_leave_runtime<Update::eReductions | Update::eXRegs>(3);
        emit_leave_runtime_frame();

        a.mr(ARG4, ARG1);
        a.b(labels[raise_exception]);
    }
}

void BeamModuleAssembler::emit_i_bif1(const ArgSource &Src1,
                                      const ArgLabel &Fail,
                                      const ArgWord &Bif,
                                      const ArgRegister &Dst) {
    auto src1 = load_source(Src1);

    a.std(src1.reg, getXRef(0));

    ubif_comment(Bif);
    emit_i_bif(Fail, Bif, Dst);
}

void BeamModuleAssembler::emit_i_bif2(const ArgSource &Src1,
                                      const ArgSource &Src2,
                                      const ArgLabel &Fail,
                                      const ArgWord &Bif,
                                      const ArgRegister &Dst) {
    auto [src1, src2] = load_sources(Src1, TMP1, Src2, TMP2);

    a.std(src1.reg, getXRef(0));
    a.std(src2.reg, getXRef(1));

    ubif_comment(Bif);
    emit_i_bif(Fail, Bif, Dst);
}

void BeamModuleAssembler::emit_i_bif3(const ArgSource &Src1,
                                      const ArgSource &Src2,
                                      const ArgSource &Src3,
                                      const ArgLabel &Fail,
                                      const ArgWord &Bif,
                                      const ArgRegister &Dst) {
    auto [src1, src2] = load_sources(Src1, TMP1, Src2, TMP2);
    auto src3 = load_source(Src3, TMP3);

    a.std(src1.reg, getXRef(0));
    a.std(src2.reg, getXRef(1));
    a.std(src3.reg, getXRef(2));

    ubif_comment(Bif);
    emit_i_bif(Fail, Bif, Dst);
}

void BeamModuleAssembler::emit_i_bif(const ArgLabel &Fail,
                                     const ArgWord &Bif,
                                     const ArgRegister &Dst) {
    mov_arg(ARG4, Bif);

    if (Fail.get() != 0) {
        fragment_call(ga->get_i_bif_guard_shared());
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        fragment_call(ga->get_i_bif_body_shared());
    }

    mov_arg(Dst, ARG1);
}

/*
 * Emit code for guard BIFs that can't fail (e.g. is_list/1).  We
 * don't need to test for failure.
 */

void BeamModuleAssembler::emit_nofail_bif1(const ArgSource &Src1,
                                           const ArgWord &Bif,
                                           const ArgRegister &Dst) {
    auto src1 = load_source(Src1);

    a.std(src1.reg, getXRef(0));

    ubif_comment(Bif);
    mov_arg(ARG4, Bif);
    fragment_call(ga->get_i_bif_guard_shared());
    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_nofail_bif2(const ArgSource &Src1,
                                           const ArgSource &Src2,
                                           const ArgWord &Bif,
                                           const ArgRegister &Dst) {
    auto [src1, src2] = load_sources(Src1, TMP1, Src2, TMP2);

    a.std(src1.reg, getXRef(0));
    a.std(src2.reg, getXRef(1));

    ubif_comment(Bif);
    mov_arg(ARG4, Bif);
    fragment_call(ga->get_i_bif_guard_shared());
    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_i_length_setup(const ArgLabel &Fail,
                                              const ArgWord &Live,
                                              const ArgSource &Src) {
    /* Store trap state after the currently live registers. There are
     * 3 extra registers beyond the ordinary ones that we're free to
     * use for whatever purpose. */
    ERTS_CT_ASSERT(ERTS_X_REGS_ALLOCATED - MAX_REG >= 3);
    auto trap_reg1 = ArgXRegister(Live.get() + 0);
    auto trap_reg2 = ArgXRegister(Live.get() + 1);
    auto trap_reg3 = ArgXRegister(Live.get() + 2);

    auto src = load_source(Src, TMP1);
    auto dst1 = init_destination(trap_reg1, src.reg);
    auto dst2 = init_destination(trap_reg2, TMP2);

    mov_imm(dst2.reg, make_small(0));
    mov_var(dst1, src);

    /* Store original argument. This is only needed for exceptions and
     * can be safely skipped in guards. */
    if (Fail.get() != 0) {
        flush_vars(dst1, dst2);
    } else {
        auto dst3 = init_destination(trap_reg3, src.reg);
        mov_var(dst3, src);
        flush_vars(dst1, dst2, dst3);
    }
}

/* ARG2 = live registers, ARG3 = entry address
 *
 * Result is returned in RET. */
void BeamGlobalAssembler::emit_i_length_common(Label fail, int state_size) {
    Label trap_or_error = a.new_label();

    ASSERT(state_size >= 2 && state_size <= ERTS_X_REGS_ALLOCATED - MAX_REG);

    /* Save arguments for error/trapping path. */
    a.std(ARG2, TMP_MEM1q);
    a.std(ARG3, TMP_MEM2q);

    emit_enter_runtime_frame();
    emit_enter_runtime<Update::eReductions | Update::eXRegs>();

    a.mr(ARG1, c_p);
    lea(TMP1, getXRef(0));
    a.sldi(ARG2, ARG2, 3);
    a.add(ARG2, TMP1, ARG2);
    runtime_call<Eterm (*)(Process *, Eterm *), erts_trapping_length_1>();

    emit_branch_if_not_value(ARG1, trap_or_error);

    emit_leave_runtime<Update::eReductions | Update::eXRegs>();
    emit_leave_runtime_frame();

    a.blr();

    a.bind(trap_or_error);
    {
        a.ld(ARG2, TMP_MEM1q);
        a.ld(ARG3, TMP_MEM2q);
        a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
        cmp(TMP1, TRAP);
        a.bne(fail);

        emit_leave_runtime<Update::eReductions | Update::eXRegs>();
        emit_leave_runtime_frame();

        /* The trap state is stored in the registers above the current live
         * ones, so we add the state size (in words) to keep it alive. */
        a.addi(ARG2, ARG2, imm(state_size));

        a.li(LR_TMP, imm(0));
        a.std(LR_TMP, ppc::Mem(c_p, offsetof(Process, current)));
        a.stb(ARG2, ppc::Mem(c_p, offsetof(Process, arity)));

        /* We'll find our way back through the entry address (ARG3). */
        a.b(labels[context_switch_simplified]);
    }
}

/* ARG2 = live registers, ARG3 = entry address
 *
 * Result is returned in RET. */
void BeamGlobalAssembler::emit_i_length_body_shared() {
    Label error = a.new_label();
    /* `state_size = 3` to include the original argument. */
    emit_i_length_common(error, 3);

    a.bind(error);
    {
        static const ErtsCodeMFA bif_mfa = {am_erlang, am_length, 1};

        /* Move the original argument to x0. It's stored in the third word of
         * the trap state. */
        lea(TMP1, getXRef(0));
        a.sldi(ARG2, ARG2, 3);
        a.add(ARG2, TMP1, ARG2);
        a.ld(TMP1, ppc::Mem(ARG2, sizeof(Eterm[2])));

        emit_leave_runtime<Update::eReductions | Update::eXRegs>();
        emit_leave_runtime_frame();

        a.mr(XREG0, TMP1);

        mov_imm(ARG4, &bif_mfa);
        emit_raise_exception();
    }
}

/* ARG2 = live registers, ARG3 = entry address
 *
 * Result is returned in ARG. Error is indicated by THE_NON_VALUE. */
void BeamGlobalAssembler::emit_i_length_guard_shared() {
    Label error = a.new_label();

    emit_i_length_common(error, 2);

    a.bind(error);
    {
        emit_leave_runtime<Update::eReductions | Update::eXRegs>();
        emit_leave_runtime_frame();

        a.blr();
    }
}

void BeamModuleAssembler::emit_i_length(const ArgLabel &Fail,
                                        const ArgWord &Live,
                                        const ArgRegister &Dst) {
    Label entry = a.new_label();

    a.bind(entry);

    mov_arg(ARG2, Live);
    emit_adr(ARG3, entry);
    if (Fail.get() != 0) {
        fragment_call(ga->get_i_length_guard_shared());
        emit_branch_if_not_value(ARG1, resolve_beam_label(Fail, dispUnknown));
    } else {
        fragment_call(ga->get_i_length_body_shared());
    }

    mov_arg(Dst, ARG1);
}

#if defined(DEBUG) || defined(ERTS_ENABLE_LOCK_CHECK)

static Eterm debug_call_light_bif(Process *c_p,
                                  Eterm *reg,
                                  ErtsCodePtr I,
                                  ErtsBifFunc vbf) {
    Eterm result;

    ERTS_ASSERT_TRACER_REFS(&c_p->common);
    ERTS_UNREQ_PROC_MAIN_LOCK(c_p);
    {
        ERTS_CHK_MBUF_SZ(c_p);
        ASSERT(!ERTS_PROC_IS_EXITING(c_p));
        result = vbf(c_p, reg, I);
        ASSERT(!ERTS_PROC_IS_EXITING(c_p) || is_non_value(result));
        ERTS_CHK_MBUF_SZ(c_p);

        ERTS_VERIFY_UNUSED_TEMP_ALLOC(c_p);
        ERTS_HOLE_CHECK(c_p);
    }
    PROCESS_MAIN_CHK_LOCKS(c_p);
    ERTS_REQ_PROC_MAIN_LOCK(c_p);
    ERTS_ASSERT_TRACER_REFS(&c_p->common);
    ERTS_ASSERT(!(erts_atomic32_read_nob(&c_p->xstate) &
                  (ERTS_PXSFLG_HANDOVER_CODE_MOD_PERM |
                   ERTS_PXSFLG_HANDOVER_CODE_STAGE_PERM)));

    return result;
}
#endif

/* It is important that the below code is as optimized as possible.
 * When doing any changes, make sure to look at the estone bif_dispatch
 * benchmark to make sure you don't introduce any regressions.
 *
 * ARG3 = entry
 * ARG4 = export entry
 * ARG8 = BIF pointer
 */
void BeamGlobalAssembler::emit_call_light_bif_shared() {
    ppc::Mem entry_mem = TMP_MEM1q, export_mem = TMP_MEM2q,
             mbuf_mem = TMP_MEM3q;

    Label trace = a.new_label(), yield = a.new_label();

    /* Spill everything we may need on the error and GC paths. */
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, mbuf)));
    a.std(ARG3, entry_mem);
    a.std(ARG4, export_mem);
    a.std(TMP1, mbuf_mem);

    /* Check if we should trace this bif call or handle save_calls. Both
     * variants dispatch through the export entry.
     *
     * The two tests are combined into the EQ bit of CR0, which is set if the
     * BIF is traced or save_calls is active. */
    a.lwz(TMP1, ppc::Mem(ARG4, offsetof(Export, is_bif_traced)));
    cmp(TMP1, 0);
    cmp(active_code_ix, ERTS_SAVE_CALLS_CODE_IX, ppc::cr1);
    a.crorc(ppc::cr0.eq(), ppc::cr1.eq(), ppc::cr0.eq());
    a.beq(trace);

    a.addic_rc(FCALLS, FCALLS, imm(-1));
    a.ble(yield);
    {
        Label check_bif_return = a.new_label(),
              gc_after_bif_call = a.new_label();

        emit_enter_runtime_frame();
        emit_enter_runtime<Update::eReductions | Update::eStack |
                           Update::eHeap | Update::eXRegs>(MAX_BIF_ARITY);

#ifdef ERTS_MSACC_EXTENDED_STATES
        {
            Label skip_msacc = a.new_label();

            a.ld(TMP1, erts_msacc_cache);
            cmp(TMP1, 0);
            a.beq(skip_msacc);

            /* The values of the X registers are in the X register array, so we
             * use XREG0 to save the entry pointer (ARG3) over this call. */
            a.mr(XREG0, ARG3);

            a.ld(ARG1, erts_msacc_cache);
            a.ld(ARG2, ppc::Mem(ARG4, offsetof(Export, info.mfa.module)));
            a.mr(ARG3, ARG8);
            runtime_call<const void *(*)(ErtsMsAcc *, Eterm, const void *),
                         erts_msacc_set_bif_state>();

            a.mr(ARG8, ARG1);
            a.mr(ARG3, XREG0);

            a.bind(skip_msacc);
        }
#endif

        {
            /* Call the BIF proper. ARG3 and ARG8 have been set earlier. */
            a.mr(ARG1, c_p);
            load_x_reg_array(ARG2);

#if defined(DEBUG) || defined(ERTS_ENABLE_LOCK_CHECK)
            a.mr(ARG4, ARG8);
            runtime_call<
                    Eterm (*)(Process *, Eterm *, ErtsCodePtr, ErtsBifFunc),
                    debug_call_light_bif>();
#else
            dynamic_runtime_call<3>(ARG8);
#endif
        }

#ifdef ERTS_MSACC_EXTENDED_STATES
        {
            Label skip_msacc = a.new_label();

            a.mr(XREG0, ARG1);

            a.ld(TMP1, erts_msacc_cache);
            cmp(TMP1, 0);
            a.beq(skip_msacc);

            lea(ARG1, erts_msacc_cache);
            runtime_call<void (*)(ErtsMsAcc **), erts_msacc_update_cache>();

            /* Set state to emulator if msacc has been enabled */
            a.ld(ARG1, erts_msacc_cache);
            cmp(ARG1, 0);
            a.beq(skip_msacc);

            mov_imm(ARG2, ERTS_MSACC_STATE_EMULATOR);
            mov_imm(ARG3, 1);
            runtime_call<void (*)(ErtsMsAcc *, Uint, int),
                         erts_msacc_set_state_m__>();

            a.bind(skip_msacc);
            a.mr(ARG1, XREG0);
        }
#endif

        /* We must update the active code index in case another process has
         * loaded new code, as the result of this BIF may be observable on both
         * ends.
         *
         * It doesn't matter whether the BIF modifies anything; if process A
         * loads new code and calls erlang:monotonic_time/0 soon after, we'd
         * break the illusion of atomic upgrades if process B still ran old code
         * after seeing a later timestamp from its own call to
         * erlang:monotonic_time/0. */
        emit_leave_runtime<Update::eReductions | Update::eCodeIndex |
                           Update::eHeap | Update::eStack | Update::eXRegs>(
                MAX_BIF_ARITY);
        emit_leave_runtime_frame();

        /* ERTS_IS_GC_DESIRED_INTERNAL
         *
         * A garbage collection is needed if either F_FORCE_GC or
         * F_DISABLE_GC is set, if the binary heap is larger than its limit,
         * or if the heap fragments are larger than the remaining heap. The
         * three tests are done first, each in a condition register field of
         * its own, and their results are then combined into one branch. */
        {
            ERTS_CT_ASSERT(Support::is_uint_n<16>(F_FORCE_GC | F_DISABLE_GC));
            a.lwz(TMP1, ppc::Mem(c_p, offsetof(Process, flags)));
            a.andi_rc(LR_TMP, TMP1, imm(F_FORCE_GC | F_DISABLE_GC));

            a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, bin_vheap_sz)));
            a.ld(TMP2, ppc::Mem(c_p, offsetof(Process, off_heap.overhead)));
            a.cmpld(ppc::cr1, TMP2, TMP1);

            a.sub(TMP1, E, HTOP);
            a.sradi(TMP1, TMP1, 3);
            a.ld(TMP2, ppc::Mem(c_p, offsetof(Process, mbuf_sz)));
            a.cmpd(ppc::cr6, TMP1, TMP2);

            /* CR1.GT = overhead > limit || a flag is set || free < needed */
            a.crorc(ppc::cr1.gt(), ppc::cr1.gt(), ppc::cr0.eq());
            a.cror(ppc::cr1.gt(), ppc::cr1.gt(), ppc::cr6.lt());
            a.bgt(ppc::cr1, gc_after_bif_call);

            /* No GC is needed, so a saved live_hf_end marker is no longer
             * valid. Clear it unless it is already cleared. */
            a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, live_hf_end)));
            mov_imm(TMP2, (UWord)ERTS_INVALID_HFRAG_PTR);
            a.cmpd(TMP1, TMP2);
            a.beq(check_bif_return);
            a.std(TMP2, ppc::Mem(c_p, offsetof(Process, live_hf_end)));
        }

        a.bind(check_bif_return);
        {
            Label error = a.new_label(), trap = a.new_label();

            emit_branch_if_not_value(ARG1, trap);

            a.mr(XREG0, ARG1);
            a.blr();

            a.bind(trap);
            {
                a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
                emit_branch_if_ne(TMP1, TRAP, error);

                /* Push our return address to the Erlang stack and trap out.
                 *
                 * The BIF_TRAP macros all set up c_p->arity and c_p->current,
                 * so we can use a simplified context switch. */
                emit_enter_erlang_frame();
                a.ld(ARG3, ppc::Mem(c_p, offsetof(Process, i)));
                a.b(labels[context_switch_simplified]);
            }

            a.bind(error);
            {
                /* raise_exception_shared expects current PC in ARG2 and MFA in
                 * ARG4. */
                a.ld(ARG2, entry_mem);
                a.ld(ARG4, export_mem);
                add(ARG4, ARG4, offsetof(Export, info.mfa));
                a.b(labels[raise_exception_shared]);
            }
        }

        a.bind(gc_after_bif_call);
        {
            emit_enter_runtime_frame();
            emit_enter_runtime<Update::eReductions | Update::eStack |
                               Update::eHeap | Update::eXRegs>(MAX_BIF_ARITY);

            a.mr(ARG3, ARG1);

            a.mr(ARG1, c_p);
            a.ld(ARG2, mbuf_mem);
            load_x_reg_array(ARG4);
            a.ld(ARG5, export_mem);
            a.ld(ARG5, ppc::Mem(ARG5, offsetof(Export, info.mfa.arity)));
            runtime_call<Eterm (*)(Process *,
                                   ErlHeapFragment *,
                                   Eterm,
                                   Eterm *,
                                   Uint),
                         erts_gc_after_bif_call_lhf>();

            emit_leave_runtime<Update::eReductions | Update::eStack |
                               Update::eHeap | Update::eXRegs>(MAX_BIF_ARITY);
            emit_leave_runtime_frame();

            a.b(check_bif_return);
        }
    }

    a.bind(trace);
    {
        /* Call the export entry instead of the BIF. */
        branch(emit_setup_dispatchable_call(ARG4));
    }

    a.bind(yield);
    {
        a.ld(ARG2, ppc::Mem(ARG4, offsetof(Export, info.mfa.arity)));
        lea(ARG4, ppc::Mem(ARG4, offsetof(Export, info.mfa)));
        a.stb(ARG2, ppc::Mem(c_p, offsetof(Process, arity)));
        a.std(ARG4, ppc::Mem(c_p, offsetof(Process, current)));

        /* We'll find our way back through ARG3 (entry address). */
        a.b(labels[context_switch_simplified]);
    }
}

void BeamModuleAssembler::emit_call_light_bif(const ArgWord &Bif,
                                              const ArgExport &Exp) {
    Label entry = a.new_label();
    BeamFile_ImportEntry *e = &beam->imports.entries[Exp.get()];

    a.bind(entry);

    mov_arg(ARG4, Exp);
    mov_arg(ARG8, Bif);
    emit_adr(ARG3, entry);

    if (logger.file()) {
        comment("BIF: %T:%T/%d", e->module, e->function, e->arity);
    }
    fragment_call(ga->get_call_light_bif_shared());
}

void BeamModuleAssembler::emit_send() {
    Label entry = a.new_label();

    /* This is essentially a mirror of call_light_bif, there's no point to
     * specializing send/2 anymore. We do it here because it's far more work to
     * do it in the loader. */
    a.bind(entry);

    load_constant(ARG4, BIF_TRAP_EXPORT(BIF_send_2));
    load_constant(ARG8, send_2);
    emit_adr(ARG3, entry);

    fragment_call(ga->get_call_light_bif_shared());
}

void BeamModuleAssembler::emit_nif_start() {
    /* load time only instruction */
}

void BeamGlobalAssembler::emit_bif_nif_epilogue(void) {
    Label check_trap = a.new_label(), trap = a.new_label(),
          error = a.new_label();

#ifdef ERTS_MSACC_EXTENDED_STATES
    {
        Label skip_msacc = a.new_label();

        a.ld(TMP1, erts_msacc_cache);
        cmp(TMP1, 0);
        a.beq(skip_msacc);

        /* The values of the X registers are in the X register array,
         * so we can use XREG0 to save the contents of ARG1 during the
         * call. */
        a.mr(XREG0, ARG1);
        a.ld(ARG1, erts_msacc_cache);
        mov_imm(ARG2, ERTS_MSACC_STATE_EMULATOR);
        mov_imm(ARG3, 1);
        runtime_call<void (*)(ErtsMsAcc *, Uint, int),
                     erts_msacc_set_state_m__>();
        a.mr(ARG1, XREG0);

        a.bind(skip_msacc);
    }
#endif

    /* Another process may have loaded new code and somehow notified us through
     * this call, so we must update the active code index. */
    emit_leave_runtime<Update::eStack | Update::eHeap | Update::eXRegs |
                       Update::eReductions | Update::eCodeIndex>();

    emit_branch_if_not_value(ARG1, check_trap);

    comment("Do return and dispatch to it");
    a.mr(XREG0, ARG1);

    emit_leave_erlang_frame();

    if (erts_alcu_enable_code_atags) {
        /* See emit_i_test_yield. */
        a.mflr(LR_TMP);
        a.std(LR_TMP, ppc::Mem(c_p, offsetof(Process, i)));
    }

    a.blr();

    a.bind(check_trap);
    a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, freason)));
    cmp(TMP1, TRAP);
    a.bne(error);
    {
        comment("yield");

        comment("test trap to hibernate");
        ERTS_CT_ASSERT(Support::is_uint_n<15>(F_HIBERNATE_SCHED));
        a.lwz(TMP1, ppc::Mem(c_p, offsetof(Process, flags)));
        a.andi_rc(LR_TMP, TMP1, imm(F_HIBERNATE_SCHED));
        a.beq(trap);

        comment("do hibernate trap");
        a.li(LR_TMP, imm(~F_HIBERNATE_SCHED));
        a.and_(TMP1, TMP1, LR_TMP);
        a.stw(TMP1, ppc::Mem(c_p, offsetof(Process, flags)));
        a.b(labels[do_schedule]);
    }

    a.bind(trap);
    {
        comment("do normal trap");

        /* The BIF_TRAP macros all set up c_p->arity and c_p->current, so we
         * can use a simplified context switch. */
        a.ld(ARG3, ppc::Mem(c_p, offsetof(Process, i)));
        a.b(labels[context_switch_simplified]);
    }

    a.bind(error);
    {
        a.mr(ARG2, E);

        emit_enter_runtime();

        a.mr(ARG1, c_p);
        runtime_call<ErtsCodePtr (*)(const Process *, const Eterm *),
                     erts_printable_return_address>();

        emit_leave_runtime();

        a.mr(ARG2, ARG1);
        a.ld(ARG4, ppc::Mem(c_p, offsetof(Process, current)));
        a.b(labels[raise_exception_shared]);
    }
}

/* Used by call_bif, dispatch_bif, and export_trampoline.
 *
 * Note that we don't check reductions here as we may have jumped here through
 * interpreted code (e.g. an ErtsNativeFunc or export entry) and it's very
 * tricky to yield back. Reductions are checked in module code instead.
 *
 * ARG2 = BIF MFA
 * ARG3 = I (rip), doesn't need to point past an MFA
 * ARG4 = function to be called */
void BeamGlobalAssembler::emit_call_bif_shared(void) {
    /* "Heavy" BIFs need up-to-date values for `c_p->i`, `c_p->current`, and
     * `c_p->arity`. */

    emit_enter_runtime_frame();
    a.std(ARG2, ppc::Mem(c_p, offsetof(Process, current)));
    /* `call_bif` wants arity in ARG5. */
    a.ld(ARG5, ppc::Mem(ARG2, offsetof(ErtsCodeMFA, arity)));
    a.stb(ARG5, ppc::Mem(c_p, offsetof(Process, arity)));
    a.std(ARG3, ppc::Mem(c_p, offsetof(Process, i)));

    /* The corresponding leave can be found in the epilogue. */
    emit_enter_runtime<Update::eStack | Update::eHeap | Update::eXRegs |
                       Update::eReductions>();

#ifdef ERTS_MSACC_EXTENDED_STATES
    {
        Label skip_msacc = a.new_label();

        a.ld(TMP1, erts_msacc_cache);
        cmp(TMP1, 0);
        a.beq(skip_msacc);

        /* The values of the X registers are in the X register array, so we can
         * use XREG0 and XREG1 to save the contents of the ARG* registers
         * during the call. */
        a.mr(XREG0, ARG3);
        a.mr(XREG1, ARG5);

        a.ld(ARG1, erts_msacc_cache);
        a.ld(ARG2, ppc::Mem(ARG2, offsetof(ErtsCodeMFA, module)));
        a.mr(ARG3, ARG4);
        runtime_call<const void *(*)(ErtsMsAcc *, Eterm, const void *),
                     erts_msacc_set_bif_state>();
        a.mr(ARG4, ARG1);

        a.mr(ARG3, XREG0);
        a.mr(ARG5, XREG1);

        a.bind(skip_msacc);
    }
#endif

    a.mr(ARG1, c_p);
    load_x_reg_array(ARG2);
    /* ARG3 (I), ARG4 (func), and ARG5 (arity) have already been provided. */
    runtime_call<Eterm (*)(Process *, Eterm *, ErtsCodePtr, ErtsBifFunc, Uint),
                 beam_jit_call_bif>();

#ifdef ERTS_MSACC_EXTENDED_STATES
    /* The values of the X registers are in the X register array, so we can use
     * XREG0 to save the contents of ARG1 during the call. */
    a.mr(XREG0, ARG1);
    lea(ARG1, erts_msacc_cache);
    runtime_call<void (*)(ErtsMsAcc **), erts_msacc_update_cache>();
    a.mr(ARG1, XREG0);
#endif

    emit_leave_runtime_frame();
    emit_bif_nif_epilogue();
}

void BeamGlobalAssembler::emit_dispatch_bif(void) {
    /* c_p->i points into the trampoline of a ErtsNativeFunc, right after the
     * `info` structure. */
    a.ld(ARG3, ppc::Mem(c_p, offsetof(Process, i)));

    ERTS_CT_ASSERT(offsetof(ErtsNativeFunc, trampoline.call_bif_nif) ==
                   sizeof(ErtsCodeInfo));

    ssize_t mfa_offset = offsetof(ErtsNativeFunc, trampoline.call_bif_nif) -
                         offsetof(ErtsNativeFunc, trampoline.info.mfa);

    sub(ARG2, ARG3, mfa_offset);

    ssize_t dfunc_offset = offsetof(ErtsNativeFunc, trampoline.dfunc) -
                           offsetof(ErtsNativeFunc, trampoline.call_bif_nif);
    a.ld(ARG4, ppc::Mem(ARG3, dfunc_offset));

    a.b(labels[call_bif_shared]);
}

/* This is only used for opcode compatibility with the interpreter, it's never
 * actually called. */
void BeamModuleAssembler::emit_call_bif(const ArgWord &Func) {
    (void)Func;

    emit_nyi("emit_call_bif");
}

void BeamModuleAssembler::emit_call_bif_mfa(const ArgAtom &M,
                                            const ArgAtom &F,
                                            const ArgWord &A) {
    const Export *e;
    UWord func;

    e = erts_active_export_entry(M.get(), F.get(), A.get());
    ASSERT(e != NULL && e->bif_number != -1);

    func = (UWord)bif_table[e->bif_number].f;

    emit_adr(ARG3, current_label);
    sub(ARG2, ARG3, sizeof(ErtsCodeMFA));
    comment("HBIF: %T:%T/%d",
            e->info.mfa.module,
            e->info.mfa.function,
            A.get());
    mov_imm(ARG4, func);

    a.b(resolve_fragment(ga->get_call_bif_shared(), disp32MB));
}

void BeamGlobalAssembler::emit_call_nif_early() {
    a.mflr(ARG2);
    sub(ARG2, ARG2, BEAM_ASM_BP_RETURN_OFFSET + sizeof(ErtsCodeInfo));

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    runtime_call<ErtsCodePtr (*)(Process *, const ErtsCodeInfo *),
                 erts_call_nif_early>();

    emit_leave_runtime();

    /* Emulate `emit_call_nif`, loading the current (phony) instruction
     * pointer into ARG3.
     *
     * Note that we "inherit" the frame that was pushed to the stack prior to
     * running the breakpoint instruction, discarding the current content of
     * LR. */
    a.mr(ARG3, ARG1);
    a.b(labels[call_nif_shared]);
}

/* Used by call_nif, call_nif_early, and dispatch_nif.
 *
 * Note that we don't check reductions here as we may have jumped here through
 * interpreted code (e.g. an ErtsNativeFunc or export entry) and it's very
 * tricky to yield back. Reductions are checked in module code instead.
 *
 * ARG3 = current I, just past the end of an ErtsCodeInfo. */
void BeamGlobalAssembler::emit_call_nif_shared(void) {
    /* The corresponding leave can be found in the epilogue. */
    emit_enter_runtime<Update::eStack | Update::eHeap | Update::eXRegs |
                       Update::eReductions>();

#ifdef ERTS_MSACC_EXTENDED_STATES
    {
        Label skip_msacc = a.new_label();

        a.ld(TMP1, erts_msacc_cache);
        cmp(TMP1, 0);
        a.beq(skip_msacc);

        /* The values of the X registers are in the X register array,
         * so we can use XREG0 to save the contents of ARG3 during the
         * call. */
        a.mr(XREG0, ARG3);
        a.ld(ARG1, erts_msacc_cache);
        mov_imm(ARG2, ERTS_MSACC_STATE_NIF);
        mov_imm(ARG3, 1);
        runtime_call<void (*)(ErtsMsAcc *, Uint, int),
                     erts_msacc_set_state_m__>();
        a.mr(ARG3, XREG0);

        a.bind(skip_msacc);
    }
#endif

    /* The function pointer and the module of the NIF follow the
     * `call_bif_nif` field of the ErtsNativeFunc that ARG3 points to. */
    a.mr(ARG1, c_p);
    a.mr(ARG2, ARG3);
    load_x_reg_array(ARG3);
    ERTS_CT_ASSERT(BEAM_ASM_NFUNC_SIZE % sizeof(UWord) == 0);
    a.ld(ARG4, ppc::Mem(ARG2, BEAM_ASM_NFUNC_SIZE));
    a.ld(ARG5, ppc::Mem(ARG2, BEAM_ASM_NFUNC_SIZE + sizeof(UWord)));
    runtime_call<Eterm (*)(Process *,
                           ErtsCodePtr,
                           Eterm *,
                           BeamJitNifF *,
                           struct erl_module_nif *),
                 beam_jit_call_nif>();

    emit_bif_nif_epilogue();
}

void BeamGlobalAssembler::emit_dispatch_nif(void) {
    /* c_p->i points into the trampoline of a ErtsNativeFunc, right after the
     * `info` structure.
     *
     * ErtsNativeFunc already follows the NIF call layout, so we don't need to
     * do anything beyond loading the address. */
    a.ld(ARG3, ppc::Mem(c_p, offsetof(Process, i)));
    a.b(labels[call_nif_shared]);
}

/* Distance from the start of a NIF stub to the second instruction after its
 * `dispatch` label, which is the address that the stub passes to
 * `call_nif_yield_helper` in SUPER_TMP. See `emit_call_nif`. */
#define CALL_NIF_HELPER_OFFSET                                                 \
    (BEAM_ASM_NFUNC_SIZE + sizeof(UWord[3]) + sizeof(Uint32))

/* SUPER_TMP = address inside the NIF stub, see above. */
void BeamGlobalAssembler::emit_call_nif_yield_helper() {
    Label yield = a.new_label();

    /* ARG3 = current I, just past the end of an ErtsCodeInfo. */
    sub(ARG3, SUPER_TMP, CALL_NIF_HELPER_OFFSET);

    if (erts_alcu_enable_code_atags) {
        /* See emit_i_test_yield. */
        a.std(ARG3, ppc::Mem(c_p, offsetof(Process, i)));
    }

    a.addic_rc(FCALLS, FCALLS, imm(-1));
    a.ble(yield);
    a.b(labels[call_nif_shared]);

    a.bind(yield);
    {
        int mfa_offset = sizeof(ErtsCodeMFA);
        int arity_offset = offsetof(ErtsCodeMFA, arity) - mfa_offset;

        a.ld(TMP1, ppc::Mem(ARG3, arity_offset));
        a.stb(TMP1, ppc::Mem(c_p, offsetof(Process, arity)));

        sub(TMP1, ARG3, mfa_offset);
        a.std(TMP1, ppc::Mem(c_p, offsetof(Process, current)));

        /* Yield to `dispatch` rather than `entry` to avoid pushing too many
         * frames to the stack. See `emit_call_nif` for details. */
        add(ARG3, ARG3, BEAM_ASM_NFUNC_SIZE + sizeof(UWord[3]));
        a.b(labels[context_switch_simplified]);
    }
}

/* WARNING: This stub is memcpy'd, so all code herein must be explicitly
 * position-independent.
 *
 * It must also fit in the `call_nif` field of ErtsNifBeamStub, which leaves
 * room for no more than 24 bytes after the three words of data. */
void BeamModuleAssembler::emit_call_nif(const ArgWord &Func,
                                        const ArgWord &NifMod,
                                        const ArgWord &DirtyFunc) {
    Label entry = a.new_label(), dispatch = a.new_label(),
          helper = a.new_label();

    /* The start of this function must mimic the layout of ErtsNativeFunc.
     *
     * We jump here on the very first entry. */
    a.bind(entry);
    {
        a.b(dispatch);

        /* The data that follows must be word-aligned. */
        a.nop();

        /* Everything prior to this, including the breakpoint, is part of the
         * `call_bif_nif` field. */
        ASSERT(a.offset() % sizeof(UWord) == 0);

        /* ErtsNativeFunc.func */
        a.embed_uint64(Func.get());

        /* ErtsNativeFunc.m */
        a.embed_uint64(NifMod.get());

        /* ErtsNativeFunc.dfunc */
        a.embed_uint64(DirtyFunc.get());
    }

    /* `emit_call_nif_yield_helper` relies on this to compute the address of
     * `dispatch` */
    ASSERT((a.offset() - code.label_offset_from_base(current_label)) ==
           BEAM_ASM_NFUNC_SIZE + sizeof(UWord[3]));

    a.bind(dispatch);
    {
        /* The helper works out the address of this stub from SUPER_TMP,
         * which holds the address of the second instruction below. That
         * saves the instructions it would take to compute it here, which
         * there is no room for. */
        a.lnia(SUPER_TMP);
        a.ld(TMP1, SUPER_TMP, helper);
        a.mtctr(TMP1);
        a.bctr();

        a.bind(helper);
        a.embed_uint64((Uint64)ga->get_call_nif_yield_helper());
    }
}

static ErtsCodePtr get_on_load_address(Process *c_p, Eterm module) {
    const Module *modp = erts_get_module(module, erts_active_code_ix());

    if (modp && modp->on_load) {
        const BeamCodeHeader *hdr = (modp->on_load)->code_hdr;

        if (hdr) {
            return erts_codeinfo_to_code(hdr->on_load);
        }
    }

    c_p->freason = BADARG;

    return NULL;
}

/* Implements the internal and undocumented erlang:call_on_load_function/1,
 * which is very tricky to implement as a BIF. */
void BeamModuleAssembler::emit_i_call_on_load_function() {
    static ErtsCodeMFA mfa = {am_erlang, am_call_on_load_function, 1};
    Label next = a.new_label();

    a.mr(ARG2, XREG0);

    /* The first X register must be preserved for the error path. */
    emit_enter_runtime(1);

    a.mr(ARG1, c_p);
    runtime_call<ErtsCodePtr (*)(Process *, Eterm), get_on_load_address>();

    emit_leave_runtime(1);

    cmp(ARG1, 0);
    a.bne(next);
    emit_raise_exception(&mfa);

    a.bind(next);
    erlang_call(ARG1);
}

void BeamModuleAssembler::emit_i_load_nif() {
    static ErtsCodeMFA mfa = {am_erlang, am_load_nif, 2};

    Label entry = a.new_label(), next = a.new_label(), schedule = a.new_label();

    a.bind(entry);

    emit_enter_runtime<Update::eHeapAlloc | Update::eXRegs>(2);

    a.mr(ARG1, c_p);
    emit_adr(ARG2, current_label);
    load_x_reg_array(ARG3);
    runtime_call<beam_jit_nif_load_ret (*)(Process *, ErtsCodePtr, Eterm *),
                 beam_jit_load_nif>();

    emit_leave_runtime<Update::eHeapAlloc | Update::eXRegs>(2);

    cmp(ARG1, RET_NIF_yield);
    a.beq(schedule);

    cmp(ARG1, RET_NIF_success);
    a.beq(next);

    emit_raise_exception(current_label, &mfa);

    a.bind(schedule);
    {
        emit_adr(ARG3, entry);
        a.b(resolve_fragment(ga->get_context_switch_simplified(), disp32MB));
    }

    a.bind(next);
}
