/*
	MINIXCompat.c
	
	A compatibility environment that can run M68000 MINIX 1.5 binaries,
	for example to enable building M68000 MINIX on a modern UNIX.
	
	Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
 */

#include "MINIXCompat.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "MINIXCompat_Types.h"
#include "MINIXCompat_Emulation.h"
#include "MINIXCompat_Executable.h"
#include "MINIXCompat_Filesystem.h"
#include "MINIXCompat_Messages.h"
#include "MINIXCompat_Processes.h"
#include "MINIXCompat_SysCalls.h"


static MINIXCompat_Execution_State MINIXCompat_State = MINIXCompat_Execution_State_Started;
static int MINIXCompat_exit_status = EX_OK;


int main(int argc, char **argv, char **envp)
{
    // Validate arguments.

    if (argc < 2) {
        fprintf(stderr, "%s: Insufficient arguments.\n", argv[0]);
        exit(EX_USAGE);
    }

    // Initialize subsystems.

    MINIXCompat_Filesystem_Initialize();
    MINIXCompat_CPU_Initialize();
    MINIXCompat_Processes_Initialize();
    MINIXCompat_SysCall_Initialize();

    // Run the main emulation loop.

    while (MINIXCompat_State != MINIXCompat_Execution_State_Finished) {
        switch (MINIXCompat_State) {
            case MINIXCompat_Execution_State_Started: {
                // Set up the tool to run.

                int16_t exec_err = MINIXCompat_Processes_ExecuteWithHostParams(argv[1], argc, argv, envp);
                if (exec_err != 0) {
                    fprintf(stderr, "Failed to execute %s: %d\n", argv[1], exec_err);
                    exit(EX_OSERR);
                }
            } break;

            case MINIXCompat_Execution_State_Ready: {
                // Reset the emulated CPU so it's prepared to run, then swtich to the running state.

                MINIXCompat_CPU_Reset();
                MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Running);
            } break;

            case MINIXCompat_Execution_State_Running: {
                // Run the emulated CPU for a bunch of cycles.

                (void) MINIXCompat_CPU_Run(10000);

                // If the execution state hasn't changed as a result of running the emulated CPU, handle any pending signals.

                if (MINIXCompat_State == MINIXCompat_Execution_State_Running) {
                    MINIXCompat_Processes_HandlePendingSignals();
                }
            } break;

            case MINIXCompat_Execution_State_Finished: {
                // Just exit the main work loop once we're in this state.
            } break;
        }
    }

    // Exit with whatever our exit code should be.

	return MINIXCompat_exit_status;
}


void MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State state)
{
    // Ensure a valid state transition, only these are allowed:
    // - start -> ready
    // - ready -> running
    // - running -> ready
    // - running -> finished
    // - finished -> finished (since exit(2) can be called multiple times before actually exiting)

    assert(   ((MINIXCompat_State == MINIXCompat_Execution_State_Started) && (state == MINIXCompat_Execution_State_Ready))
           || ((MINIXCompat_State == MINIXCompat_Execution_State_Ready)   && (state == MINIXCompat_Execution_State_Running))
           || ((MINIXCompat_State == MINIXCompat_Execution_State_Running) && (state == MINIXCompat_Execution_State_Ready))
           || ((MINIXCompat_State == MINIXCompat_Execution_State_Running) && (state == MINIXCompat_Execution_State_Finished))
           || ((MINIXCompat_State == MINIXCompat_Execution_State_Finished) && (state == MINIXCompat_Execution_State_Finished)));

    MINIXCompat_State = state;
}


void MINIXCompat_exit(int status)
{
    MINIXCompat_exit_status = status;
    MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Finished);
}
