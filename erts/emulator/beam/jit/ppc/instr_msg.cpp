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
#include "bif.h"
#include "code_ix.h"
#include "erl_proc_sig_queue.h"
#ifdef USE_VM_PROBES
#    include "dtrace-wrapper.h"
#endif
}

void BeamModuleAssembler::emit_recv_marker_reserve(const ArgRegister &Dst) {
    emit_enter_runtime<Update::eHeapAlloc>();

    a.mr(ARG1, c_p);
    runtime_call<Eterm (*)(Process *), erts_msgq_recv_marker_insert>();

    emit_leave_runtime<Update::eHeapAlloc>();

    mov_arg(Dst, ARG1);
}

void BeamModuleAssembler::emit_recv_marker_bind(const ArgRegister &Marker,
                                                const ArgRegister &Reference) {
    auto [marker, reference] = load_sources(Marker, ARG2, Reference, ARG3);

    mov_var(ARG2, marker);
    mov_var(ARG3, reference);

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *, Eterm, Eterm),
                 erts_msgq_recv_marker_bind>();

    emit_leave_runtime();
}

void BeamModuleAssembler::emit_recv_marker_clear(const ArgRegister &Reference) {
    mov_arg(ARG2, Reference);

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *, Eterm), erts_msgq_recv_marker_clear>();

    emit_leave_runtime();
}

void BeamModuleAssembler::emit_recv_marker_use(const ArgRegister &Reference) {
    mov_arg(ARG2, Reference);

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *, Eterm), erts_msgq_recv_marker_set_save>();

    emit_leave_runtime();
}

#ifdef ERTS_ENABLE_LOCK_CHECK
int erts_lc_proc_sig_receive_helper(Process *c_p,
                                    int fcalls,
                                    int neg_o_reds,
                                    ErtsMessage **msgpp,
                                    int *get_outp) {
    int res;
    /*
     * erts_proc_sig_receive_helper() may temporarliy release
     * its own main lock...
     */
    ERTS_UNREQ_PROC_MAIN_LOCK(c_p);
    res = erts_proc_sig_receive_helper(c_p,
                                       fcalls,
                                       neg_o_reds,
                                       msgpp,
                                       get_outp);
    ERTS_REQ_PROC_MAIN_LOCK(c_p);
    return res;
}
#endif

void BeamGlobalAssembler::emit_i_loop_rec_shared() {
    Label restart = a.new_label(), peek_message = a.new_label(),
          schedule_out = a.new_label(), check_is_distributed = a.new_label(),
          done = a.new_label();

    ppc::Mem await_addr = TMP_MEM1q, message_ptr = TMP_MEM2q,
             get_out = TMP_MEM3q;
    ppc::Mem flags = ppc::Mem(c_p, offsetof(Process, flags));

    /* Clears F_DELAY_GC in the process flags. Clobbers TMP1. */
    auto clear_delay_gc = [&]() {
        ERTS_CT_ASSERT(Support::is_uint_n<15>(F_DELAY_GC));
        a.lwz(TMP1, flags);
        a.li(LR_TMP, imm(~F_DELAY_GC));
        a.and_(TMP1, TMP1, LR_TMP);
        a.stw(TMP1, flags);
    };

    /* We call C code and must therefore keep our return address in a
     * register that is preserved by it. */
    a.mflr(XREG1);

    a.lwz(TMP1, flags);
    a.ori(TMP1, TMP1, imm(F_DELAY_GC));
    a.stw(TMP1, flags);
    a.std(ARG1, ppc::Mem(c_p, offsetof(Process, i)));
    a.std(ARG2, await_addr);

    a.bind(restart);
    {
        cmp(FCALLS, 0);
        a.ble(schedule_out);

        /* !! FALL THROUGH !! */
    }

    comment("Peek next message");
    a.bind(peek_message);
    {
        a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, sig_qs.save)));
        a.ld(ARG1, ppc::Mem(TMP1));
        cmp(ARG1, 0);
        a.bne(check_is_distributed);
        comment("Inner queue empty, fetch more from outer/middle queues");

        emit_enter_runtime<Update::eReductions | Update::eHeapAlloc |
                           Update::eHeap>(0);

        a.li(LR_TMP, imm(0));
        a.std(LR_TMP, message_ptr);
        a.mr(ARG1, c_p);
        a.mr(ARG2, FCALLS);
        mov_imm(ARG3, 0);
        lea(ARG4, message_ptr);
        lea(ARG5, get_out);
#ifdef ERTS_ENABLE_LOCK_CHECK
        runtime_call<int (*)(Process *, int, int, ErtsMessage **, int *),
                     erts_lc_proc_sig_receive_helper>();
#else
        runtime_call<int (*)(Process *, int, int, ErtsMessage **, int *),
                     erts_proc_sig_receive_helper>();
#endif

        /* erts_proc_sig_receive_helper merely inspects FCALLS, so we don't
         * need to update it here.
         *
         * Also note that another process may have loaded new code and sent us
         * a message to notify us about it, so we must update the active code
         * index. */
        emit_leave_runtime<Update::eHeapAlloc | Update::eCodeIndex>(0);

        a.sub(FCALLS, FCALLS, ARG1);

        /* Need to spill message_ptr to ARG1 as check_is_distributed uses it. */
        a.ld(ARG1, message_ptr);
        cmp(ARG1, 0);
        a.bne(check_is_distributed);

        /* Did we receive a signal or run out of reds? */
        a.lwz(TMP1, get_out);
        cmp(TMP1, 0);
        a.bne(schedule_out);

        /* The queue is empty and we're not yielding or exiting, so we'll jump
         * to our wait/timeout instruction.
         *
         * Note that the message queue lock is still held in this case. */
        clear_delay_gc();

        a.ld(TMP1, await_addr);
        a.mtctr(TMP1);
        a.bctr();
    }

    a.bind(schedule_out);
    {
        /* We either ran out of reductions or received an exit signal; schedule
         * ourselves out. The yield address (`c_p->i`) was set on ingress. */
        clear_delay_gc();
        a.li(LR_TMP, imm(0));
        a.stb(LR_TMP, ppc::Mem(c_p, offsetof(Process, arity)));
        a.std(LR_TMP, ppc::Mem(c_p, offsetof(Process, current)));

        a.b(labels[do_schedule]);
    }

    /*
     * ARG1 now contains the pointer to a message.
     */
    comment("Check if message is distributed");
    a.bind(check_is_distributed);
    {
        a.ld(TMP1, ppc::Mem(ARG1, offsetof(ErtsSignal, common.tag)));
        emit_branch_if_value(TMP1, done);

        sub(FCALLS, FCALLS, 10);

        emit_enter_runtime(0);

        a.mr(ARG2, ARG1);
        a.mr(ARG1, c_p);
        runtime_call<ErtsMessage *(*)(Process *, ErtsMessage *),
                     beam_jit_decode_dist>();

        emit_leave_runtime(0);

        cmp(ARG1, 0);
        a.beq(restart);

        /* !! FALL THROUGH !! */
    }

    a.bind(done);
    {
        a.ld(XREG0, ppc::Mem(ARG1, offsetof(ErtsMessage, m[0])));
        a.mtlr(XREG1);
        a.blr();
    }
}

void BeamModuleAssembler::emit_i_loop_rec(const ArgLabel &Wait) {
    Label entry = a.new_label();

    a.bind(entry);
    emit_adr(ARG1, entry);
    load_constant(ARG2, Wait);
    fragment_call(ga->get_i_loop_rec_shared());
}

void BeamModuleAssembler::emit_remove_message() {
    /* HTOP and E are passed explicitly and only read from, so we don't need to
     * swap them out. */
    a.mr(ARG3, HTOP);
    a.mr(ARG4, E);

    emit_enter_runtime();

    a.mr(ARG1, c_p);
    a.mr(ARG2, FCALLS);
    a.mr(ARG5, active_code_ix);
    runtime_call<Sint32 (*)(Process *, Sint32, Eterm *, Eterm *, Uint32),
                 beam_jit_remove_message>();
    a.mr(FCALLS, ARG1);

    emit_leave_runtime();
}

void BeamModuleAssembler::emit_loop_rec_end(const ArgLabel &Dest) {
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *), erts_msgq_set_save_next>();

    emit_leave_runtime(0);

    a.addi(FCALLS, FCALLS, imm(-1));
    a.b(resolve_beam_label(Dest, disp32MB));
    mark_unreachable();
}

void BeamModuleAssembler::emit_wait_unlocked(const ArgLabel &Dest) {
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    load_constant(ARG2, Dest);
    runtime_call<void (*)(Process *, ErtsCodePtr), beam_jit_wait_unlocked>();

    emit_leave_runtime(0);

    a.b(resolve_fragment(ga->get_do_schedule(), disp32MB));
    mark_unreachable();
}

void BeamModuleAssembler::emit_wait_locked(const ArgLabel &Dest) {
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    load_constant(ARG2, Dest);
    runtime_call<void (*)(Process *, ErtsCodePtr), beam_jit_wait_locked>();

    emit_leave_runtime(0);

    a.b(resolve_fragment(ga->get_do_schedule(), disp32MB));

    /* Must check stubs here because this branch is followed by
     * a label when part of `wait_timeout_locked`. */
    mark_unreachable_check_pending_stubs();
}

void BeamModuleAssembler::emit_wait_timeout_unlocked(const ArgSource &Src,
                                                     const ArgLabel &Dest) {
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *), beam_jit_take_receive_lock>();

    emit_leave_runtime(0);

    emit_wait_timeout_locked(Src, Dest);
}

void BeamModuleAssembler::emit_wait_timeout_locked(const ArgSource &Src,
                                                   const ArgLabel &Dest) {
    Label wait = a.new_label(), next = a.new_label();

    mov_arg(ARG2, Src);

    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    emit_adr(ARG3, next);
    runtime_call<enum beam_jit_tmo_ret (*)(Process *, Eterm, ErtsCodePtr),
                 beam_jit_wait_timeout>();

    emit_leave_runtime(0);

    ERTS_CT_ASSERT(RET_next < RET_wait && RET_wait < RET_badarg);
    cmp(ARG1, RET_wait);
    a.beq(wait);
    a.blt(next);

    emit_raise_exception(current_label, (ErtsCodeMFA *)nullptr);

    a.bind(wait);
    emit_wait_locked(Dest);

    a.bind(next);
}

void BeamModuleAssembler::emit_timeout_locked() {
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *), beam_jit_timeout_locked>();

    emit_leave_runtime(0);
}

void BeamModuleAssembler::emit_timeout() {
    emit_enter_runtime(0);

    a.mr(ARG1, c_p);
    runtime_call<void (*)(Process *), beam_jit_timeout>();

    emit_leave_runtime(0);
}
