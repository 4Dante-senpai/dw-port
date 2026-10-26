#ifndef DW1_CLOCK_COSTS_H
#define DW1_CLOCK_COSTS_H

/* [hypothesis] fixed cost of one PsyQ call; pending calibration against the reference in story 3.10 */
#define CLOCK_PSYQ_ENTRY_CYCLES 64u
/* [hypothesis] NTSC lines per frame, for VSync(1) and root counter 1; pending verification in story 3.10 */
#define CLOCK_LINES_PER_FRAME 263u
/* map_load stream-poll-probe: 786469 quiet VSync(-1) calls take 82
 * cycles; the stable counter-read retry adds 20 when it straddles a tick. */
#define CLOCK_VSYNC_QUERY_CPU_CYCLES 82u
/* map_load vsync-tail-probe: non-interlaced 80091d84..return = 60.
 * v_wait exits through ten RAM instructions and two loads (22 cycles). */
#define CLOCK_VSYNC_WAIT_RETURN_CPU_CYCLES (22u + 60u)
/* map_load DecDCTvlc2/Build spans and pinned Redux psxinterpreter.cc:1986,
 * psxmem.cc: each RAM instruction costs BIAS=2, each data access adds 1.
 * The native decoder counts its own algorithm branches, not MIPS opcodes. */
#define CLOCK_CPU_INSTRUCTION_CYCLES 2u
#define CLOCK_CPU_MEMORY_ACCESS_CYCLES 1u
/* map_load OpenBIOS memcpy, including the game's A0 jump-table stub:
 * 20 bytes = 2379 cycles; 92 bytes = 10443 cycles, without IRQ overlap.
 * ROM instruction fetches make this slower than a RAM copy loop. */
#define CLOCK_LIBC_MEMCPY_FIXED_CYCLES 139u
#define CLOCK_LIBC_MEMCPY_BYTE_CYCLES 112u
/* map_load libc probe + pinned OpenBIOS ROM loops: memset(668)=62239;
 * strcmp mismatch after 0/1/2 equal characters = 185/341/497; an equal
 * terminator adds two ROM instructions (44 cycles). strrchr(6)=1246,
 * strrchr(8, one match)=1622. ROM instruction cost is 22 cycles. */
#define CLOCK_LIBC_MEMSET_FIXED_CYCLES 115u
#define CLOCK_LIBC_MEMSET_BYTE_CYCLES 93u
#define CLOCK_LIBC_STRCMP_FIXED_CYCLES 185u
#define CLOCK_LIBC_STRCMP_CHAR_CYCLES 156u
#define CLOCK_LIBC_STRCMP_EQUAL_CYCLES 44u
#define CLOCK_LIBC_STRRCHR_FIXED_CYCLES 184u
#define CLOCK_LIBC_STRRCHR_CHAR_CYCLES 177u
#define CLOCK_LIBC_STRRCHR_MATCH_CYCLES 22u
/* Redux _SsVmFlush branches: a silent-history voice adds five instructions
 * and two RAM accesses (12 cycles); a nonzero ENVX skips nine instructions
 * and three RAM accesses (21 cycles). The 6115-cycle initial tick includes
 * its nested SPU calls. Settled silence is 6403, regardless of open scores. */
#define CLOCK_SOUND_TICK_IDLE_CYCLES 6115u
#define CLOCK_SOUND_SILENT_VOICE_CYCLES 12u
#define CLOCK_SOUND_ENV_ACTIVE_SAVING_CYCLES 21u
/* map_load SPU probe: quiet SpuInit=31003 (four 64-cycle entries);
 * SsInit=81192 including SpuInit, entry and ResetCallback. A previously
 * silent history adds 12 cycles per voice while _SsVmInit clears it. */
#define CLOCK_SPU_INIT_CPU_CYCLES 30752u
#define CLOCK_SOUND_INIT_CPU_CYCLES 46674u
#define CLOCK_SPU_START_FIRST_CPU_CYCLES 2358u
/* map_load quit-probe: active SsEnd=1226 including InterruptCallback(64),
 * SpuQuit with its DMA event installed=1678, SsQuit own RAM code=18.
 * SsVabClose unused=41, used=42 before SpuFree and 31 after; SpuFree
 * with a terminal first allocation record=232 (no allocated blocks). */
#define CLOCK_SOUND_END_ACTIVE_CPU_CYCLES (1226u - CLOCK_PSYQ_ENTRY_CYCLES)
#define CLOCK_SPU_QUIT_ACTIVE_CPU_CYCLES 1678u
#define CLOCK_SOUND_QUIT_CPU_CYCLES 18u
#define CLOCK_VAB_CLOSE_UNUSED_CPU_CYCLES 41u
#define CLOCK_VAB_CLOSE_SETUP_CPU_CYCLES 42u
#define CLOCK_VAB_CLOSE_RETURN_CPU_CYCLES 31u
#define CLOCK_SPU_FREE_EMPTY_CPU_CYCLES 232u
/* map_load quit-probe: finalizeMusic own work=184; inactive seqStop and
 * seqClose=23 each. Their active RAM paths add 4 and 9 respectively. */
#define CLOCK_GAME_FINALIZE_MUSIC_CPU_CYCLES 184u
#define CLOCK_GAME_SEQ_STOP_CPU_CYCLES(active) (23u + ((active) ? 4u : 0u))
#define CLOCK_GAME_SEQ_CLOSE_CPU_CYCLES(active) (23u + ((active) ? 9u : 0u))
/* map_load boot: ResetCallback's first setup takes 14289 cycles; later
 * calls take 59. SsInit's first VBlank before pad setup adds 2397 cycles:
 * 100177 - 81192 - (14289-59) - (33361-31003) = 2397. */
#define CLOCK_IRQ_SETUP_CYCLES 14289u
#define CLOCK_IRQ_SETUP_REPEAT_CYCLES 59u
#define CLOCK_VBLANK_NOPAD_CPU_CYCLES 2397u
/* map_load BIOS IRQ probe: sio0Handler=8878 before card setup,
 * 11154 with an idle card driver, 11914 when starting a transaction.
 * readPad(0)=5479, disconnected readPad(1)=3225, high-level copy=100. */
/* Full VBlank service after pad/card setup is 14104 cycles. Subtract
 * the 2397-cycle no-pad route and 11154-cycle SIO handler: the BIOS
 * dispatch route adds 553 cycles when the SIO handler is installed. */
#define CLOCK_VBLANK_PAD_ROUTE_CPU_CYCLES 553u
#define CLOCK_VBLANK_PAD_CPU_CYCLES 8878u
#define CLOCK_CARD_VBLANK_IDLE_CPU_CYCLES 2276u
#define CLOCK_CARD_VBLANK_TRANSFER_CPU_CYCLES 760u
/* map_load boot probe: main=463794644; the minimum settled VBlank
 * intercept over 256 fields is 464456634 (the first event is 467 cycles
 * late while Redux finishes a CPU block). This projects to main+97592.
 * Preserve the GPU scan phase at the executable handoff. */
#define CLOCK_BOOT_SCAN_PHASE_CYCLES (564398u - 97592u)
/* map_load quiet SsStart=5380: SetRCnt returns at +4655, followed by
 * InterruptCallback and the return path. SetRCnt arms at its mode write,
 * seven cycles before returning (spu-poll-probe below). */
#define CLOCK_SOUND_START_SETUP_CYCLES 4463u
#define CLOCK_SOUND_START_RETURN_CYCLES 661u
/* map_load boot probe: ResetGraph(0) is 270323 cycles, including its
 * 59-cycle ResetCallback. The print and queue/ctlbuf memset are CPU work.
 * gte_init is 477985 cycles including _patch_gte/FlushCache and one IRQ. */
#define CLOCK_GPU_RESET_CPU_CYCLES 270200u
/* map_load post-movie probe: ResetGraph(3)=212790, less entry (64)
 * and ResetCallback (59). Same queue/ctlbuf reset, no hardware reset. */
#define CLOCK_GPU_RESET_KEEP_CPU_CYCLES 212667u
/* map_load SS/SL headers (version 7, explicit bank): 7929/8477 cycles
 * for 18/27 samples, 128 program slots and two populated slots. RAM
 * loops: 18 per slot, +2 populated; fixed 256-slot scan 4096; sample
 * work 62 even / 60 odd. Entry is already charged by the public API. */
#define CLOCK_VAB_HEADER_FIXED_CPU_CYCLES 301u
#define CLOCK_VAB_PROGRAM_CPU_CYCLES 18u
#define CLOCK_VAB_USED_PROGRAM_CPU_CYCLES 2u
#define CLOCK_VAB_SCAN_CPU_CYCLES 4096u
#define CLOCK_VAB_SAMPLE_EVEN_CPU_CYCLES 62u
#define CLOCK_VAB_SAMPLE_ODD_CPU_CYCLES 60u
#define CLOCK_GTE_INIT_CPU_CYCLES (477985u - CLOCK_VBLANK_NOPAD_CPU_CYCLES)
/* map_load boot probe: PadInit(0), 464986583..466802119. Subtract the
 * existing two 64-cycle entries (PadInit and ResetCallback). */
#define CLOCK_PAD_INIT_CPU_CYCLES 1815413u
/* map_load timer-2 IRQ probe (OpenBIOS): verifier at target +1223..1241,
 * trapIntr 946 cycles later; callback 228 cycles after trapIntr, return 75
 * after callback to ReturnFromException, then 155 to resume the game.
 * exception-probe: quiet complete IRQ=8739 (empty) / 9027 (one open).
 * Instruction-boundary jitter remains unmodeled. */
#define CLOCK_SOUND_IRQ_VERIFY_CYCLES 1230u
#define CLOCK_SOUND_IRQ_ENTRY_CYCLES (2166u + 228u)
#define CLOCK_SOUND_IRQ_RETURN_CYCLES (75u + 155u)
/* irq-ack-cutoff-probe: trapIntr's last I_STAT loop read at 80092000
 * is 25 cycles after the callback returns (80091fd4). A later VBlank
 * remains pending for a new BIOS exception, including a fresh SIO poll. */
#define CLOCK_SOUND_IRQ_ACK_CHECK_CYCLES 25u
/* spu-poll-probe: SetRCnt(2) mode write at +58, return at +65.
 * Counter phase starts at the register write, before the return path. */
#define CLOCK_RCNT2_MODE_WRITE_CPU_CYCLES 58u
#define CLOCK_RCNT2_RETURN_CPU_CYCLES 7u
/* map_load CPU-init probe: quiet full function spans. The fixed loops clear
 * fixed-size game tables; subtract existing timed library entries for font
 * (128) and view_init (640). No device waits occur in these functions. */
#define CLOCK_GAME_MODEL_COMPONENTS_CPU_CYCLES 2963u
#define CLOCK_GAME_WORLD_OBJECTS_CPU_CYCLES 6160u
#define CLOCK_GAME_GS_TMD_MAP_CPU_CYCLES 245u
#define CLOCK_GAME_FILE_QUEUE_CPU_CYCLES 1092u
#define CLOCK_GAME_ATTACK_OBJECTS_CPU_CYCLES 2012u
#define CLOCK_GAME_FONT_CLUT_CPU_CYCLES 1569u
#define CLOCK_GAME_FADE_DATA_CPU_CYCLES 25u
#define CLOCK_GAME_VIEW_INIT_CPU_CYCLES 1194u
#define CLOCK_GAME_UI_BOX_CPU_CYCLES 184u
#define CLOCK_GAME_INVENTORY_CPU_CYCLES 4688u
#define CLOCK_GAME_CLOCK_CPU_CYCLES 216u
/* map_load lookupFileTable RAM loop: ten instructions and two loads per
 * tested entry. The successful path, prologue and epilogue add 86 cycles,
 * excluding the separately timed BIOS string calls and CdIntToPos. */
#define CLOCK_GAME_FILE_LOOKUP_FIXED_CYCLES 86u
#define CLOCK_GAME_FILE_LOOKUP_ENTRY_CYCLES 22u
/* CdIntToPos: 65 RAM instructions and three stores, no variable branches. */
#define CLOCK_CD_INT_TO_POS_CPU_CYCLES 133u
/* map_load DMA2 breakpoint bounds: 128 words completes by 109 cycles;
 * 2048 words remains busy at 796, completes by 2765. Pinned Redux gpu.cc
 * schedules memory-to-VRAM at 7 * words / BCR block size (PsyQ uses 16).
 * Direct GP0 rasterization has no delayed pixel work in the software GPU.
 * ClearImage 480x480 -> DrawSync: 245 cycles, not 230336 (spec 3.10). */
/* map_load post-movie probe: PutDrawEnv=11263 including two 64-cycle
 * entries and 10443-cycle BIOS copy. PutDispEnv=2722, +158 when display
 * mode changes after reset. valiable_init=375 without nested timed calls. */
/* landing-nested-probe: PutDrawEnv queries VSync at +473, then spends
 * 265 CPU cycles outside its 10443-cycle BIOS copy; total 11263. */
#define CLOCK_GPU_DRAW_ENV_ENTER_CPU_CYCLES 473u
#define CLOCK_GPU_DRAW_ENV_CPU_CYCLES 265u
#define CLOCK_GPU_DISP_ENV_CPU_CYCLES (2722u - CLOCK_PSYQ_ENTRY_CYCLES)
#define CLOCK_GPU_DISP_MODE_CPU_CYCLES 158u
#define CLOCK_GS_VARIABLE_INIT_CPU_CYCLES 375u
/* map_load post-movie spans, excluding nested calls: gpu_init 179;
 * GsSetDrawBuffClip 67; GsSetDrawBuffOffset (GPU offset path) 90;
 * GsInitGraph 77 and GsDefDispBuff 81 (each includes its 64 entry). */
#define CLOCK_GPU_INIT_CPU_CYCLES 179u
#define CLOCK_GS_CLIP_CPU_CYCLES 64u
#define CLOCK_GS_CLIP_RETURN_CPU_CYCLES 9u
#define CLOCK_GS_OFFSET_CPU_CYCLES 77u
#define CLOCK_GS_OFFSET_RETURN_CPU_CYCLES 17u
#define CLOCK_GS_INIT_CPU_CYCLES 13u
#define CLOCK_GS_DISPLAY_BUFF_CPU_CYCLES 17u
/* map_load file-probe: readVHBFile's measured own work is charged at
 * the original call sites. 1831 before readFile includes strrchr=1622;
 * after readFile: 29 before memcpy, 6 before close, 24 before head,
 * 19 before body, 8 before completion, 4 before VBaddr, 18 on return. */
#define CLOCK_GAME_VHB_ENTER_CPU_CYCLES 209u
#define CLOCK_GAME_VHB_RETURN_CPU_CYCLES 18u
#define CLOCK_GAME_VHB_COPY_ENTER_CPU_CYCLES 29u
#define CLOCK_GAME_VHB_CLOSE_ENTER_CPU_CYCLES 6u
#define CLOCK_GAME_VHB_HEAD_ENTER_CPU_CYCLES 24u
#define CLOCK_GAME_VHB_BODY_ENTER_CPU_CYCLES 19u
#define CLOCK_GAME_VHB_WAIT_ENTER_CPU_CYCLES 8u
#define CLOCK_GAME_VHB_ADDR_ENTER_CPU_CYCLES 4u
/* vhb-tail-probe: 942277699..942277739, valid loaded bank. */
#define CLOCK_VAB_GET_ADDR_CPU_CYCLES 40u
/* file-probe and retail readFile: 18 before lookup; 14 between lookup
 * and CdControl; 21 from successful CdControl to CdRead, 20 on return. */
#define CLOCK_GAME_READ_FILE_ENTER_CPU_CYCLES 18u
#define CLOCK_GAME_READ_FILE_CONTROL_ENTER_CPU_CYCLES 14u
#define CLOCK_GAME_READ_FILE_READ_ENTER_CPU_CYCLES 21u
/* readFile 800a3474..800a3488: branch, argument setup and jal = 10. */
#define CLOCK_GAME_READ_FILE_POLL_ENTER_CPU_CYCLES 10u
#define CLOCK_GAME_READ_FILE_RETURN_CPU_CYCLES 20u
#define CLOCK_GPU_DMA_BLOCK_WORDS 16u
#define CLOCK_GPU_UPLOAD_BLOCK_CYCLES 7u
#define CLOCK_GPU_DOWNLOAD_WORD_CYCLES 1u
#define CLOCK_GPU_CHAIN_WORD_CYCLES 1u
#define CLOCK_GPU_SYNC_ENTRY_CYCLES 245u

/* map_load CD command/DMA trace and pinned Redux cdrom.cc:
 * 797 us command FIFO, 20 ms read start, 1 ms + 3 us/LBA read seek,
 * 650 ms speed transition, and integer-microsecond sector cadence.
 * API CPU overhead and directory caching remain separately uncalibrated. */
#define CLOCK_CD_US(us) ((uint64_t)(us) * CLOCK_HZ / 1000000u)
#define CLOCK_CD_COMMAND_CYCLES CLOCK_CD_US(797u)
#define CLOCK_CD_READ_START_CYCLES CLOCK_CD_US(20000u)
#define CLOCK_CD_READ_SEEK_CYCLES CLOCK_CD_US(1000u)
#define CLOCK_CD_SEEK_LBA_US 3u
#define CLOCK_CD_SPEED_CHANGE_CYCLES CLOCK_CD_US(650000u)
#define CLOCK_CD_INIT_CYCLES CLOCK_CD_US(120000u)
/* [hypothesis] PsyQ setup/IRQ CPU overhead, not drive busy time. */
#define CLOCK_CD_INIT_CPU_CYCLES 120000u
#define CLOCK_CD_COMMAND_CPU_CYCLES 3500u
/* map_load CdControl(Setloc)=31251 quiet cycles: 26993 FIFO, 64 entry,
 * 4194 CPU, compared with the generic 3500 already charged. CdRead's
 * same-mode path is 1358: 1159 before ReadN, 199 after command issue. */
#define CLOCK_CD_SETLOC_SETUP_CPU_CYCLES 725u
#define CLOCK_CD_SETLOC_RETURN_CPU_CYCLES 3469u
#define CLOCK_CD_READ_SETUP_CPU_CYCLES (1159u - CLOCK_PSYQ_ENTRY_CYCLES)
#define CLOCK_CD_READ_RETURN_CPU_CYCLES 199u
/* map_load SsVabTransBody: 226 cycles before SpuWrite, 32 after.
 * Three existing 64-cycle entries cover 192 of the setup. */
#define CLOCK_VAB_BODY_SETUP_CPU_CYCLES 34u
#define CLOCK_VAB_BODY_RETURN_CPU_CYCLES 32u
/* map_load SsSetTableSize(1,1)=909 cycles; 64 entry plus fixed table setup.
 * Other table dimensions retain the uncalibrated entry-only estimate. */
#define CLOCK_SOUND_TABLE_ONE_CPU_CYCLES (909u - CLOCK_PSYQ_ENTRY_CYCLES)
/* tick-mode-probe: SsSetTickMode(2)=88, including GetVideoMode(64). */
#define CLOCK_SOUND_TICK_240_SETUP_CPU_CYCLES 24u
/* map_load: CD Pause ACK interrupts lookupFileTable for exactly 2604
 * cycles (898 in trapIntr plus 1706 in BIOS routing). Ready IRQ spans
 * are 2273 cycles, or 3634 on the last sector including cb_read/Pause. */
#define CLOCK_CD_IRQ_ROUTE_CPU_CYCLES 1706u
/* sector-dispatch-probe: physical ReadN callback to getintr is 1762
 * cycles with DMA callback detached (SCN), 1771 after SpuInit (SS/SL).
 * The BIOS DMA verifier adds three RAM instructions/accesses (9).
 * The final cb_read/Pause IRQ includes a second routing pass: SCN
 * physical-to-resume is 5322, SS/SL 5340 (difference 18). */
#define CLOCK_CD_NO_DMA_ROUTE_CPU_CYCLES 9u
#define CLOCK_CD_ACK_IRQ_CPU_CYCLES (898u + CLOCK_CD_IRQ_ROUTE_CPU_CYCLES)
/* map_load ReadN ACK trap spans are 911 (882276076..882276987),
 * thirteen cycles longer than Pause ACK; same BIOS routing. */
#define CLOCK_CD_READ_ACK_IRQ_CPU_CYCLES (911u + CLOCK_CD_IRQ_ROUTE_CPU_CYCLES)
#define CLOCK_CD_COMPLETE_IRQ_CPU_CYCLES (863u + CLOCK_CD_IRQ_ROUTE_CPU_CYCLES)
/* map_load Pause completion while CdControl waits: trapIntr=1492,
 * versus 863 without a synchronous waiter; publishes its sync result. */
#define CLOCK_CD_COMPLETE_WAIT_CPU_CYCLES (1492u - 863u)
#define CLOCK_CD_READY_IRQ_CPU_CYCLES (2273u + CLOCK_CD_IRQ_ROUTE_CPU_CYCLES)
#define CLOCK_CD_LAST_READY_IRQ_CPU_CYCLES (3634u + CLOCK_CD_IRQ_ROUTE_CPU_CYCLES)
/* map_load final ReadN IRQ: Pause command=941832657, exception return=
 * 941833567, then 155 BIOS restore cycles. The drive starts pausing
 * during cb_read, before this remaining 1065-cycle IRQ return tail. */
#define CLOCK_CD_LAST_READY_RETURN_CPU_CYCLES (910u + 155u)
/* map_load: cd_read_retry at 217414998 after ReadN at 183035310.
 * USA CdReadSync (800b2d1c): retry when Vcount > last progress + 60. */
#define CLOCK_CD_READ_RETRY_VBLANKS 61u
/* map_load CD parse probe: SCN 4 records/21 copied bytes = 3738 cycles;
 * ETCHI 7/54 = 8622; MOVIE 6/36 = 6210. Subtract the BIOS memcpy
 * costs: exactly 80 + 257 * records. Path table 37/160 = 29877,
 * exactly 43 + 183 * records plus memcpy. These are CPU work. */
#define CLOCK_CD_DIRECTORY_PARSE_FIXED_CYCLES 80u
#define CLOCK_CD_DIRECTORY_PARSE_RECORD_CYCLES 257u
#define CLOCK_CD_PATH_PARSE_FIXED_CYCLES 43u
#define CLOCK_CD_PATH_PARSE_RECORD_CYCLES 183u
/* [hypothesis] directory search on discs with unknown ISO metadata. */
#define CLOCK_CD_SEEK_CYCLES (CLOCK_HZ / 10)
#define CLOCK_CD_SECTOR_1X_CYCLES (CLOCK_CD_US(13333u) + CLOCK_CD_SECTOR_DISPATCH_CYCLES)
/* title full ReadN capture: 342 intervals, mean 225795.602 cycles,
 * after excluding masked first/last IRQ delivery (short recovery gaps).
 * Nominal 225769 plus 27 cycles of Redux branch dispatch. SCN alone
 * averages +32, SS +20: neither isolated file represents all polling.
 * One global mean applies to every ReadN; IRQ latency is separate. */
#define CLOCK_CD_SECTOR_DISPATCH_CYCLES 27u
/* title: two masked ReadN IRQ / recovery pairs each span 451572 cycles,
 * i.e. 34 over two nominal sectors. With the 27-cycle polling dispatch,
 * the callback dispatched during an IRQ contributes the remaining 7.
 * The physical drive continues while CPU IRQ delivery is masked. */
#define CLOCK_CD_MASKED_SECTOR_DISPATCH_CYCLES 7u
/* read-poll-branches: blocking CdReadSync prologue 25, loop 218 cycles.
 * Taken branches end at these offsets within the loop. VSync(-1) is
 * ordinary compiled C behavior; this analytic cost schedule runs no MIPS. */
#define CLOCK_CD_READ_POLL_SETUP_CPU_CYCLES 25u
#define CLOCK_CD_SECTOR_2X_CYCLES (CLOCK_CD_US(6666u) + CLOCK_CD_SECTOR_DISPATCH_CYCLES)
/* movie0 ReadS raw-sector path: 12423 consecutive DataReady intervals
 * average 225783.17 cycles (225769 nominal + 14 CPU dispatch). The
 * callback/ring path differs from ReadN file-transfer polling above. */
#define CLOCK_CD_STREAM_SECTOR_DISPATCH_CYCLES 14u
/* movie0 StGetNext empty path: 17 RAM instructions and four loads = 38.
 * Publishing a ready frame adds 17 instructions and six accesses = 40.
 * MOV_readFrame spends eight instructions between unsuccessful polls. */
#define CLOCK_CD_STREAM_POLL_CYCLES 38u
#define CLOCK_CD_STREAM_PUBLISH_CYCLES 40u
#define CLOCK_MOVIE_READ_POLL_CYCLES 16u
/* [hypothesis] spin granularity while awaiting a GPU display flip. */
#define CLOCK_MOVIE_FLIP_POLL_CYCLES 1024u
/* map_load SPU probe and pinned Redux psxdma.cc: DMA4 schedules an IRQ
 * after BCR words / 2 CPU cycles (eight bytes per cycle). CPU IRQ service
 * and BIOS event polling are accounted separately from the device. */
#define CLOCK_SPU_DMA_CYCLES(bytes) ((uint64_t)(bytes) / 8u)
/* map_load SPU detail probe: clear-work-area chunks are 6171 cycles
 * for 1024 bytes: 128 DMA + 6043 CPU (PsyQ, IRQ and OpenBIOS WaitEvent).
 * Entry/last-iteration work adds 38 cycles in total. */
#define CLOCK_SPU_CLEAR_CHUNK_CPU_CYCLES 6043u
#define CLOCK_SPU_CLEAR_FIXED_CPU_CYCLES 38u
/* DMA4 start 817681796, completion at +6696; _spu_FiDMA enters at
 * 817690424 and returns at 817693358. IRQ routing/event publication
 * includes the BIOS event publication measured separately below. */
/* map_load spu-poll-probe: TestEvent failure=317, success=384;
 * RAM loop adds 17 cycles once, 13 thereafter. DMA exception service
 * is 5134, separately from the polling CPU work it interrupts. Both
 * SS and SL blocking polls take 12190 despite different DMA sizes. */
#define CLOCK_SPU_DMA_IRQ_CPU_CYCLES 5134u
#define CLOCK_SPU_TRANSFER_POLL_SETUP_CYCLES 42u
#define CLOCK_SPU_TRANSFER_POLL_FAIL_CYCLES 317u
#define CLOCK_SPU_TRANSFER_POLL_SUCCESS_CYCLES 384u
#define CLOCK_SPU_TRANSFER_POLL_FIRST_LOOP_CYCLES 17u
#define CLOCK_SPU_TRANSFER_POLL_LOOP_CYCLES 13u
#define CLOCK_SPU_TRANSFER_POLL_RETURN_CYCLES 26u
/* map_load SsVabTransCompleted wrapper: 11 before/after the SPU call. */
#define CLOCK_VAB_COMPLETED_ENTER_CPU_CYCLES 11u
#define CLOCK_VAB_COMPLETED_RETURN_CPU_CYCLES 11u
#define CLOCK_SPU_WRITE_SETUP_CYCLES 352u
#define CLOCK_SPU_WRITE_RETURN_CYCLES 58u

/* map_load DMA1: 2112 words busy at 8744 cycles, done by 18790.
 * Redux mdec.cc/mdec.h schedule output at 2 cycles per byte (16896 for
 * that request). This depends on actual output size, including 24-bit mode. */
#define CLOCK_MDEC_OUTPUT_BYTE_CYCLES 2u
/* map_load CD command/IRQ trace: Redux Pause schedules 70/35 ms while
 * reading (1x/2x), 200 us when idle; cdrom.cc cdlPause. */
#define CLOCK_CD_PAUSE_1X_CYCLES CLOCK_CD_US(70000u)
#define CLOCK_CD_PAUSE_2X_CYCLES CLOCK_CD_US(35000u)
#define CLOCK_CD_PAUSE_IDLE_CYCLES CLOCK_CD_US(200u)
/* [hypothesis] memory-card command and 128-byte sector costs in CPU cycles.
 * Story 3.10 must calibrate against OpenBIOS/Redux; never tune per scene. */
#define CLOCK_CARD_COMMAND_CYCLES 1024u
/* map_load/OpenBIOS: MemCardInit entry 471080614, first BIOS sector request
 * 472185526. This is CPU setup before any card transfer, not drive latency. */
#define CLOCK_CARD_INIT_CPU_CYCLES 1104912u
/* map_load: 74 SIO0 transactions; reads 188714..189025 cycles,
 * writes 184720..184743, beginning 12799..12867 cycles after VBlank.
 * OpenBIOS alternates ports at each VBlank (sio0/driver.c).
 * Completion offsets include pad IRQ setup; select by operation, not scene. */
#define CLOCK_CARD_READ_PHASE_CYCLES 201800u
#define CLOCK_CARD_WRITE_PHASE_CYCLES 197600u
/* map_load boot probe: MemCardStart is 21588 cycles without IRQ overlap,
 * including its two 64-cycle public entries. */
#define CLOCK_CARD_START_CPU_CYCLES 21460u
/* [hypothesis] BIOS CPU work after a sector, observed request gap ~21400. */
#define CLOCK_CARD_RETURN_CYCLES 21400u
#define CLOCK_CARD_SECTOR_CYCLES (2u * 564398u)

/* read-counter-probe: 2450 Timer 1 samples; cycleStart modulo 2146 is
 * 5 cycles after the virtual anchor (MMIO sample is two cycles before
 * the breakpoint). VSync stable-query taken branches: 69/82 quiet,
 * 44 on first failed comparison and 20 on subsequent retries. */
#define CLOCK_CD_POLL_CALL_CPU_CYCLES 4u
#define CLOCK_CD_POLL_SECOND_CALL_CPU_CYCLES 26u
#define CLOCK_CD_POLL_TIMEOUT_CPU_CYCLES 13u
#define CLOCK_CD_POLL_CONTINUE_CPU_CYCLES 11u
#define CLOCK_CD_POLL_T1_PHASE_CYCLES 5u
#define CLOCK_CD_POLL_T1_FIRST_CYCLES 27u
#define CLOCK_CD_POLL_T1_SECOND_CYCLES 38u
#define CLOCK_CD_POLL_T1_FIRST_RETRY_CYCLES 44u
#define CLOCK_CD_POLL_T1_RETRY_FIRST_CYCLES 3u
#define CLOCK_CD_POLL_T1_RETRY_SECOND_CYCLES 14u
#define CLOCK_CD_POLL_T1_RETRY_CYCLES 20u
#define CLOCK_CD_POLL_VSYNC_JUMP_CYCLES 69u
#define CLOCK_CD_POLL_VSYNC_RETRY_JUMP_CYCLES 45u
#define CLOCK_CD_POLL_VSYNC_RETURN_CYCLES 13u
/* read-counter-probe: CdReady(1,NULL)=396, CdReadSync epilogue=21. */
#define CLOCK_CD_READY_QUERY_CPU_CYCLES 396u
#define CLOCK_CD_READ_SYNC_EPILOGUE_CPU_CYCLES 21u

/* map_load initializeMusic 800c5fd0..800c605c: RAM instructions
 * between its nested API calls (measured/read from the retail binary). */
#define CLOCK_GAME_MUSIC_RESET_ENTER_CPU_CYCLES 9u
#define CLOCK_GAME_MUSIC_INIT_ENTER_CPU_CYCLES 4u
#define CLOCK_GAME_MUSIC_TABLE_ENTER_CPU_CYCLES 10u
#define CLOCK_GAME_MUSIC_TICK_ENTER_CPU_CYCLES 4u
#define CLOCK_GAME_MUSIC_BANK_ENTER_CPU_CYCLES 10u
#define CLOCK_GAME_MUSIC_NEXT_BANK_CPU_CYCLES 16u
#define CLOCK_GAME_MUSIC_START_ENTER_CPU_CYCLES 10u

/* Retail VSync(0), v_wait (80091c10..80091ca8): busy loops test
 * interrupts after 19/13-cycle taken branches; the no-wait path is 30.
 * VSync zero-mode branches end at 60,70,79,83 before first v_wait. */
#define CLOCK_VSYNC_COUNTER_PREFIX_CYCLES 24u
#define CLOCK_VSYNC_ZERO_MODE_BRANCH_CYCLES 60u
#define CLOCK_VSYNC_ZERO_TARGET_BRANCH_CYCLES 10u
#define CLOCK_VSYNC_ZERO_WAIT_BRANCH_CYCLES 9u
#define CLOCK_VSYNC_WAIT_DONE_ENTER_CYCLES 21u
#define CLOCK_VSYNC_WAIT_EPILOGUE_CYCLES 9u
#define CLOCK_VSYNC_WAIT_FIRST_BRANCH_CYCLES 42u
#define CLOCK_VSYNC_WAIT_CHECK_BRANCH_CYCLES 13u
#define CLOCK_VSYNC_WAIT_LOOP_BRANCH_CYCLES 19u
#define CLOCK_VSYNC_WAIT_RETURN_CHECK_CYCLES 22u
#define CLOCK_VSYNC_BETWEEN_WAITS_CPU_CYCLES 21u
#define CLOCK_VSYNC_TAIL_CPU_CYCLES 60u

/* map_load landing-return-probe, quiet retail calls: ResetGraph(1)=219,
 * GsSetOrign=12, GsDrawOt=428. The latter enters VSync at +88;
 * its wrapper spends 12 before DrawOTag and 9 after it. */
#define CLOCK_GPU_RESET_QUEUE_CPU_CYCLES 219u
#define CLOCK_GS_ORIGIN_CPU_CYCLES 12u
#define CLOCK_GS_DRAW_OT_ENTER_CPU_CYCLES 12u
#define CLOCK_GS_DRAW_OT_RETURN_CPU_CYCLES 9u
#define CLOCK_GPU_DRAW_OT_ENTER_CPU_CYCLES 76u
#define CLOCK_GPU_DRAW_OT_RETURN_CPU_CYCLES 249u

/* landing-nested-probe: SetDispMask(1)=86; GsSwapDispBuff own RAM
 * work=92, separated around its nested APIs (25593 total). */
#define CLOCK_GPU_DISP_MASK_ON_CPU_CYCLES 86u
#define CLOCK_GS_SWAP_ENTER_CPU_CYCLES 38u
#define CLOCK_GS_SWAP_MASK_ENTER_CPU_CYCLES 4u
#define CLOCK_GS_SWAP_COUNT_CPU_CYCLES 18u
#define CLOCK_GS_SWAP_CLIP_ENTER_CPU_CYCLES 19u
#define CLOCK_GS_SWAP_OFFSET_ENTER_CPU_CYCLES 4u
#define CLOCK_GS_SWAP_RETURN_CPU_CYCLES 9u

#endif
