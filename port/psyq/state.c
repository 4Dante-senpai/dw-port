/* Observable state record per VBlank (docs/port/playtest.md, "Canal de estado").
 * Reads globals only: no game calls, no rand(), no VRAM, no virtual clock. Every global is weak
 * so synthetic runtimes without the game still link; an absent global reads as 0. */
#define LANGUAGE_C
#include <dw/entity.h>
#include <dw/params.h>
#include <dw/script.h>
#include <dw/btl.h>
#include <dw/clock.h>
#include "state.h"
#include "crt.h"
#include "playtest.h"

extern int8_t MAIN_STATE;
extern int32_t CURRENT_MENU;
#pragma weak CURRENT_SCREEN_ID
#pragma weak GAME_STATE
#pragma weak MAIN_STATE
#pragma weak IS_SCRIPT_PAUSED
#pragma weak CURRENT_MENU
#pragma weak BTL_COMMAND_MENU_ACTIVE
#pragma weak TAMER_ENTITY
#pragma weak PARTNER_ENTITY
#pragma weak PARTNER_PARA
#pragma weak MONEY
#pragma weak YEAR
#pragma weak DAY
#pragma weak HOUR
#pragma weak MINUTE
#pragma weak PLAYTIME_HOURS
#pragma weak PLAYTIME_MINUTES
#pragma weak PLAYTIME_FRAMES

#define NAMES "k overlay screen gs ms paused menu btl_menu tx ty tz px py pz hp mp cond money " \
              "year day hour minute play_h play_m play_f frame want"

void port_state_header(FILE *out) { fputs("# dw1-state 1 " NAMES "\n", out); }

#define GET(symbol) (&(symbol) ? (long)(symbol) : 0L)

static const VECTOR *position(const Entity *entity)
{
    return entity->posData ? &entity->posData->location : NULL;
}

void port_state_record(FILE *out, unsigned k, int want)
{
    const VECTOR *tamer = &TAMER_ENTITY ? position(&TAMER_ENTITY.entity) : NULL;
    const VECTOR *partner = &PARTNER_ENTITY ? position(&PARTNER_ENTITY.digimonEntity.entity) : NULL;
    fprintf(out, "%u %u %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld 0x%08x %ld %ld %ld %ld %ld %ld %ld %ld %ld %d\n",
            k, port_playtest_overlay_id(), GET(CURRENT_SCREEN_ID), GET(GAME_STATE), GET(MAIN_STATE),
            GET(IS_SCRIPT_PAUSED), GET(CURRENT_MENU), GET(BTL_COMMAND_MENU_ACTIVE),
            tamer ? (long)tamer->vx : 0L, tamer ? (long)tamer->vy : 0L, tamer ? (long)tamer->vz : 0L,
            partner ? (long)partner->vx : 0L, partner ? (long)partner->vy : 0L,
            partner ? (long)partner->vz : 0L,
            &PARTNER_ENTITY ? (long)PARTNER_ENTITY.digimonEntity.stats.current.currentHP : 0L,
            &PARTNER_ENTITY ? (long)PARTNER_ENTITY.digimonEntity.stats.current.currentMP : 0L,
            &PARTNER_PARA ? (unsigned)PARTNER_PARA.condition : 0u, GET(MONEY), GET(YEAR), GET(DAY),
            GET(HOUR), GET(MINUTE), GET(PLAYTIME_HOURS), GET(PLAYTIME_MINUTES), GET(PLAYTIME_FRAMES),
            port_frame_count(), want);
}
