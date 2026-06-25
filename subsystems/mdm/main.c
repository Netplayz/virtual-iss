/******************************************************************************
 * MAIN.C - MDM Flight Software Entry Point
 *
 * International Space Station Multiplexer/Demultiplexer
 * Command and Data Handling Processor
 *
 * This is the main entry point for the MDM flight software.
 * In the real system, this would be called by the RTOS boot loader.
 * In simulation, it's a standalone process that communicates via
 * stdin/stdout with the NATS bridge.
 *
 * Usage: ./mdm <mdm_id> <bus_address>
 *
 * Build: gcc -O2 -Wall -Wextra -o mdm main.c mdm_core.c bus_1553.c \
 *            cmd_handler.c tlm_pack.c
 ******************************************************************************/

#include "mdm.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/******************************************************************************
 * GLOBAL STATE - defined in mdm_core.c
 ******************************************************************************/

extern MDM_STATE g_mdm_state;

/******************************************************************************
 * SIGNAL HANDLERS
 ******************************************************************************/

static volatile sig_atomic_t shutdown_flag = 0;

static void
signal_handler(int sig)
{
    (void)sig;
    shutdown_flag = 1;
}

/******************************************************************************
 * PRINT_HEADER - Print the MDM boot banner
 ******************************************************************************/

static void
print_header(UINT8 mdm_id, UINT8 bus_addr)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "********************************************************\n");
    fprintf(stderr, "*  ISS C&DH MDM v4.2.1                               *\n");
    fprintf(stderr, "*  Multiplexer/Demultiplexer Flight Software          *\n");
    fprintf(stderr, "*  NASA Johnson Space Center                          *\n");
    fprintf(stderr, "*                                                     *\n");
    fprintf(stderr, "*  MDM ID:       %3u                                 *\n", mdm_id);
    fprintf(stderr, "*  Bus Address:  0x%02X                                *\n", bus_addr);
    fprintf(stderr, "*  Target:       i386EX / MIL-STD-1750A              *\n");
    fprintf(stderr, "*  OS:           VxWorks 5.3 / RTEMS 4.0             *\n");
    fprintf(stderr, "*  Bus:          MIL-STD-1553B                        *\n");
    fprintf(stderr, "********************************************************\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[BOOT] Power-on self-test initiated...\n");
}

/******************************************************************************
 * MAIN - Entry point
 ******************************************************************************/

int
main(int argc, char *argv[])
{
    UINT8 mdm_id;
    UINT8 bus_address;

    /* Default values */
    mdm_id       = 1;
    bus_address  = 0x12;

    /* Parse command line */
    if (argc > 1) {
        mdm_id = (UINT8)atoi(argv[1]);
    }
    if (argc > 2) {
        bus_address = (UINT8)strtoul(argv[2], NULL, 0);
    }

    /* Install signal handlers */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* Print boot banner */
    print_header(mdm_id, bus_address);

    /* Initialize MDM */
    mdm_init(&g_mdm_state, mdm_id, bus_address);

    /* Mark fully operational */
    g_mdm_state.mdm_mode = MDM_MODE_OPERATIONAL;

    fprintf(stderr, "[BOOT] Self-test: %s\n",
            (g_mdm_state.error_flags & 0x02) ? "FAILED" : "PASSED");
    fprintf(stderr, "[BOOT] Mode: OPERATIONAL\n");
    fprintf(stderr, "[BOOT] Entering main executive loop (10ms tick)...\n");
    fprintf(stderr, "[BOOT] Ready. Waiting for 1553 bus traffic.\n");
    fprintf(stderr, "\n");

    /* Enter main loop */
    mdm_main_loop(&g_mdm_state);

    /* Shutdown (normally unreachable) */
    mdm_shutdown(&g_mdm_state, 0);

    return 0;
}
/******************************************************************************/
