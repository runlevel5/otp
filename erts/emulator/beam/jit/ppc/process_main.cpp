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
#include "beam_common.h"
#include "code_ix.h"
#include "export.h"
}

#undef x

#if defined(DEBUG) || defined(ERTS_ENABLE_LOCK_CHECK)
static Process *erts_debug_schedule(ErtsSchedulerData *esdp,
                                    Process *c_p,
                                    int calls) {
    PROCESS_MAIN_CHK_LOCKS(c_p);
    ERTS_UNREQ_PROC_MAIN_LOCK(c_p);
    ERTS_VERIFY_UNUSED_TEMP_ALLOC(c_p);
    c_p = erts_schedule(esdp, c_p, calls);
    ERTS_VERIFY_UNUSED_TEMP_ALLOC(c_p);
    ERTS_REQ_PROC_MAIN_LOCK(c_p);
    PROCESS_MAIN_CHK_LOCKS(c_p);
    return c_p;
}
#endif

/* void process_main(ErtsSchedulerData *esdp); */
void BeamGlobalAssembler::emit_process_main() {
    Label context_switch_local = a.new_label(),
          context_switch_simplified_local = a.new_label(),
          do_schedule_local = a.new_label(), schedule_next = a.new_label();

    const ppc::Mem start_time_i =
            getSchedulerRegRef(offsetof(ErtsSchedulerRegisters, start_time_i));
    const ppc::Mem start_time =
            getSchedulerRegRef(offsetof(ErtsSchedulerRegisters, start_time));

    /* Be kind to debuggers and `perf` by saving our return address where the
     * ABI wants it, even though we never return. */
    a.mflr(LR_TMP);
    a.std(LR_TMP, ppc::Mem(sp, RUNTIME_FRAME_LR_OFFSET));

    /* Allocate the register structure on the stack to allow computing the
     * runtime stack address from it, greatly reducing the cost of stack
     * swapping. */
    ERTS_CT_ASSERT((ERTS_CACHE_LINE_SIZE & (ERTS_CACHE_LINE_SIZE - 1)) == 0);
    sub(scheduler_registers,
        sp,
        sizeof(ErtsSchedulerRegisters) + ERTS_CACHE_LINE_SIZE);
    a.clrrdi(scheduler_registers,
             scheduler_registers,
             Support::ctz((Uint64)ERTS_CACHE_LINE_SIZE));

    a.std(scheduler_registers,
          ppc::Mem(ARG1, offsetof(ErtsSchedulerData, registers)));

    /* Set up the stack frame that is used by all C code we call from now on.
     * It is placed right below the register structure, and it is linked to
     * the frame of our caller in the same instruction that moves the stack
     * pointer, so that the stack can be walked at all times. */
    ERTS_CT_ASSERT(ERTS_CACHE_LINE_SIZE % 16 == 0);
    ERTS_CT_ASSERT(RUNTIME_FRAME_SIZE % 16 == 0);
    a.sub(TMP1, scheduler_registers, sp);
    a.addi(TMP1, TMP1, imm(-RUNTIME_FRAME_SIZE));
    a.stdux(sp, ppc::Mem(sp, TMP1));

    a.li(LR_TMP, imm(0));
    a.std(LR_TMP, start_time_i);
    a.std(LR_TMP, start_time);

    mov_imm(c_p, 0);
    mov_imm(FCALLS, 0);
    mov_imm(ARG3, 0); /* Set reds_used for erts_schedule call */

    a.b(schedule_next);

    a.bind(do_schedule_local);
    {
        /* Figure out reds_used. def_arg_reg[5] = REDS_IN */
        a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, def_arg_reg[5])));
        a.sub(ARG3, TMP1, FCALLS);
        a.b(schedule_next);
    }

    /*
     * The *next* instruction pointer is provided in ARG3, and must be preceded
     * by an ErtsCodeMFA.
     */
    a.bind(context_switch_local);
    comment("Context switch, unknown arity/MFA");
    {
        Sint arity_offset = offsetof(ErtsCodeMFA, arity) - sizeof(ErtsCodeMFA);

        ERTS_CT_ASSERT(sizeof(((ErtsCodeMFA *)nullptr)->arity) ==
                       sizeof(UWord));
        a.ld(TMP1, ppc::Mem(ARG3, arity_offset));
        a.stb(TMP1, ppc::Mem(c_p, offsetof(Process, arity)));

        sub(TMP1, ARG3, sizeof(ErtsCodeMFA));
        a.std(TMP1, ppc::Mem(c_p, offsetof(Process, current)));

        /* !! Fall through !! */
    }

    a.bind(context_switch_simplified_local);
    comment("Context switch, known arity and MFA");
    {
        Label not_exiting = a.new_label(), process_exit_ptr = a.new_label();

#ifdef DEBUG
        Label check_i = a.new_label();
        /* Check that ARG3 is set to a valid CP. */
        a.andi_rc(LR_TMP, ARG3, imm(_CPMASK));
        a.beq(check_i);
        udf(1);
        a.bind(check_i);
#endif

        a.std(ARG3, ppc::Mem(c_p, offsetof(Process, i)));
        a.lwz(TMP1, ppc::Mem(c_p, offsetof(Process, state.value)));

        ERTS_CT_ASSERT(Support::is_uint_n<16>(ERTS_PSFLG_EXITING));
        a.andi_rc(LR_TMP, TMP1, imm(ERTS_PSFLG_EXITING));
        a.beq(not_exiting);
        {
            comment("Process exiting");

            /* The fragment may be too far away for its address to be
             * computed, so we load it from a word that follows this
             * block. */
            a.lnia(TMP1);
            a.ld(TMP1, TMP1, process_exit_ptr);
            a.std(TMP1, ppc::Mem(c_p, offsetof(Process, i)));
            a.li(LR_TMP, imm(0));
            a.stb(LR_TMP, ppc::Mem(c_p, offsetof(Process, arity)));
            a.std(LR_TMP, ppc::Mem(c_p, offsetof(Process, current)));
            a.b(do_schedule_local);

            a.bind(process_exit_ptr);
            a.embed_label(labels[process_exit]);
        }

        a.bind(not_exiting);

        /* Figure out reds_used. def_arg_reg[5] = REDS_IN */
        a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, def_arg_reg[5])));
        a.sub(FCALLS, TMP1, FCALLS);

        comment("Copy out X registers");
        a.mr(ARG1, c_p);
        load_x_reg_array(ARG2);
        runtime_call<void (*)(Process *, Eterm *), copy_out_registers>();

        /* Restore reds_used from FCALLS */
        a.mr(ARG3, FCALLS);

        /* !! Fall through !! */
    }

    a.bind(schedule_next);
    comment("schedule_next");

    {
        Label schedule = a.new_label(), skip_long_schedule = a.new_label();

        /* ARG3 contains reds_used at this point */

        a.ld(TMP1, start_time);
        cmp(TMP1, 0);
        a.beq(schedule);
        {
            a.mr(ARG1, c_p);
            a.ld(ARG2, start_time);

            /* Spill reds_used in start_time slot */
            a.std(ARG3, start_time);

            a.ld(ARG3, start_time_i);
            runtime_call<void (*)(Process *, Uint64, ErtsCodePtr),
                         check_monitor_long_schedule>();

            /* Restore reds_used */
            a.ld(ARG3, start_time);
        }

        a.bind(schedule);
        mov_imm(ARG1, 0);
        a.mr(ARG2, c_p);
#if defined(DEBUG) || defined(ERTS_ENABLE_LOCK_CHECK)
        runtime_call<Process *(*)(ErtsSchedulerData *, Process *, int),
                     erts_debug_schedule>();
#else
        runtime_call<Process *(*)(ErtsSchedulerData *, Process *, int),
                     erts_schedule>();
#endif
        a.mr(c_p, ARG1);

#ifdef ERTS_MSACC_EXTENDED_STATES
        lea(ARG1, erts_msacc_cache);
        runtime_call<void (*)(ErtsMsAcc **), erts_msacc_update_cache>();
#endif

        a.li(LR_TMP, imm(0));
        a.std(LR_TMP, start_time);
        mov_imm(ARG1, &erts_system_monitor_long_schedule);
        a.ld(TMP1, ppc::Mem(ARG1));
        cmp(TMP1, 0);
        a.beq(skip_long_schedule);

        {
            /* Enable long schedule test */
            runtime_call<Uint64 (*)(), erts_timestamp_millis>();
            a.std(ARG1, start_time);
            a.ld(TMP1, ppc::Mem(c_p, offsetof(Process, i)));
            a.std(TMP1, start_time_i);
        }

        a.bind(skip_long_schedule);
        comment("skip_long_schedule");

        /* Copy arguments */
        a.mr(ARG1, c_p);
        load_x_reg_array(ARG2);
        runtime_call<void (*)(Process *, Eterm *), copy_in_registers>();

        /* Setup reduction counting */
        a.lwa(FCALLS, ppc::Mem(c_p, offsetof(Process, fcalls)));
        a.std(FCALLS, ppc::Mem(c_p, offsetof(Process, def_arg_reg[5])));

#ifdef DEBUG
        a.std(FCALLS, ppc::Mem(c_p, offsetof(Process, debug_reds_in)));
#endif

        comment("check whether save calls is on");
        a.mr(ARG1, c_p);
        mov_imm(ARG2, ERTS_PSD_SAVED_CALLS_BUF);
        runtime_call<void *(*)(Process *, int), erts_psd_get>();

        /* Read the active code index, overriding it with
         * ERTS_SAVE_CALLS_CODE_IX when save_calls is enabled (ARG1 != 0). */
        mov_imm(TMP1, &the_active_code_index);
        a.lwz(TMP1, ppc::Mem(TMP1));
        mov_imm(TMP2, ERTS_SAVE_CALLS_CODE_IX);
        cmp(ARG1, 0);
        a.isel(active_code_ix, TMP1, TMP2, ppc::cr0.eq());

        /* Start executing the Erlang process. Note that reductions have
         * already been set up above. */
        emit_leave_runtime<Update::eStack | Update::eHeap | Update::eXRegs>();

        /* Check if we are just returning from a dirty nif/bif call and if so we
         * need to do a bit of cleaning up before continuing.
         *
         * This relies on `op_call_nif_WWW` / `op_call_bif_W` being encoded as
         * UDF(opcode) followed by UDF(0), which we will never emit. */
        a.ld(ARG1, ppc::Mem(c_p, offsetof(Process, i)));
        a.ld(TMP1, ppc::Mem(ARG1));

        ERTS_CT_ASSERT((op_call_nif_WWW & 0xFFFF0000) == 0);
        cmp(TMP1, op_call_nif_WWW);
        far_beq(labels[dispatch_nif]);

        ERTS_CT_ASSERT((op_call_bif_W & 0xFFFF0000) == 0);
        cmp(TMP1, op_call_bif_W);
        far_beq(labels[dispatch_bif]);

        a.mtctr(ARG1);
        a.bctr();
    }

    /* Processes may jump to the exported entry points below, executing on the
     * Erlang stack when entering. These are separate from the `_local` labels
     * above as we don't want to worry about which stack we're on when the
     * cases overlap. */

    /* `ga->get_context_switch()`
     *
     * The *next* instruction pointer is provided in ARG3, and must be preceded
     * by an ErtsCodeMFA.
     *
     * The X registers are expected to be in CPU registers.
     */
    a.bind(labels[context_switch]);
    {
        emit_enter_runtime<Update::eStack | Update::eHeap | Update::eXRegs>();

        a.b(context_switch_local);
    }

    /* `ga->get_context_switch_simplified()`
     *
     * The next instruction pointer is provided in ARG3, which does not need to
     * point past an ErtsCodeMFA as the process structure has already been
     * updated.
     *
     * The X registers are expected to be in CPU registers.
     */
    a.bind(labels[context_switch_simplified]);
    {
        emit_enter_runtime<Update::eStack | Update::eHeap | Update::eXRegs>();

        a.b(context_switch_simplified_local);
    }

    /* `ga->get_do_schedule()`
     *
     * `c_p->i` must be set prior to jumping here.
     *
     * The X registers are expected to be in CPU registers.
     */
    a.bind(labels[do_schedule]);
    {
        emit_enter_runtime<Update::eStack | Update::eHeap | Update::eXRegs>();

        a.b(do_schedule_local);
    }
}
