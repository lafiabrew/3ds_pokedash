#include "pk_app.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *const k_action_labels[11] = {
    "MAX IVS (6X31)",
    "TRICK ROOM (0 SPE)",
    "TOGGLE SHINY PID",
    "CYCLE NATURE",
    "MAX FRIENDSHIP (255)",
    "SET LEVEL 100",
    "EV: 252 ATK / 252 SPE",
    "EV: 252 SPA / 252 SPE",
    "EV: 252 HP / 252 DEF",
    "CLEAR ALL EVS (0)",
    "MAX MONEY + 999 ITEMS"
};

void pk_app_init(pk_app_t *app, const char *sd_root) {
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof(*app));
    snprintf(app->sd_root, sizeof(app->sd_root), "%s",
             sd_root != NULL ? sd_root : pk_platform_default_sd_root());
    app->calc_move_type = 1;  /* Fire */
    app->calc_def_type1 = 4;  /* Grass */
    app->calc_def_type2 = 16; /* Steel */
    app->masuda_method = true;
    app->shiny_charm = true;

    int found = pk_scan_sd_saves(app->sd_root, &app->saves);
    if (found == 0) {
        char demo_file[PK_PATH_MAX + 64];
        snprintf(demo_file, sizeof(demo_file), "%s/3ds/pokedash/saves/photonic_sun_main.sav", app->sd_root);
        if (pk_create_mock_save(demo_file, PK_GEN_7_3DS, "LAFIA", true)) {
            found = pk_scan_sd_saves(app->sd_root, &app->saves);
        }
    }
    snprintf(app->status_msg, sizeof(app->status_msg),
             "PKHAX READY: %d SAVE(S) LOADED", found);
}

void pk_app_step(pk_app_t *app, const pk_input_t *in) {
    if (app == NULL || in == NULL) {
        return;
    }

    if (in->down & PK_BTN_L) {
        app->active_tab = (pk_tab_t)((app->active_tab + 2) % 3);
    }
    if (in->down & PK_BTN_R) {
        app->active_tab = (pk_tab_t)((app->active_tab + 1) % 3);
    }

    /* Bottom screen touch tab bar (y = 0..28) */
    if (in->touch_just_pressed && in->touch_y >= 0 && in->touch_y <= 28) {
        if (in->touch_x < 106) app->active_tab = PK_TAB_PKHAX;
        else if (in->touch_x < 212) app->active_tab = PK_TAB_TYPE_CALC;
        else app->active_tab = PK_TAB_SHINY_EV;
    }

    if (app->active_tab == PK_TAB_PKHAX) {
        if ((in->down & PK_BTN_SELECT) && app->saves.count > 0) {
            app->selected_save = (app->selected_save + 1) % app->saves.count;
            app->selected_slot = 0;
            snprintf(app->status_msg, sizeof(app->status_msg),
                     "SWITCHED SAVE: %s", app->saves.entries[app->selected_save].label);
        }
        if (in->down & PK_BTN_DLEFT) {
            if (app->selected_slot > 0) app->selected_slot--;
        }
        if (in->down & PK_BTN_DRIGHT) {
            if (app->saves.count > 0) {
                int max_s = app->saves.entries[app->selected_save].party_count;
                if (app->selected_slot + 1 < max_s) app->selected_slot++;
            }
        }
        if (in->down & PK_BTN_DUP) {
            app->selected_action = (app->selected_action + 10) % 11;
        }
        if (in->down & PK_BTN_DDOWN) {
            app->selected_action = (app->selected_action + 1) % 11;
        }

        /* Touch action list on bottom screen (y = 36..204) */
        bool trigger_apply = (in->down & PK_BTN_A) != 0;
        if (in->touch_just_pressed && in->touch_y >= 36 && in->touch_y < 212) {
            int row = (in->touch_y - 36) / 16;
            if (row >= 0 && row < 11) {
                app->selected_action = row;
                trigger_apply = true;
            }
        }

        if (trigger_apply && app->saves.count > 0) {
            pk_save_summary_t *sv = &app->saves.entries[app->selected_save];
            if (pk_apply_surgical_edit(sv, app->selected_slot, (pk_edit_action_t)app->selected_action)) {
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "SAVED (.BAK + CRC %04X): %s",
                         sv->last_checksum, k_action_labels[app->selected_action]);
            } else {
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "ERR: FAILED WRITING %s", sv->label);
            }
        }
        if (in->down & PK_BTN_X) {
            int n = pk_scan_sd_saves(app->sd_root, &app->saves);
            snprintf(app->status_msg, sizeof(app->status_msg), "RESCANNED SD: %d SAVES", n);
        }
    } else if (app->active_tab == PK_TAB_TYPE_CALC) {
        if (in->down & PK_BTN_DUP) app->calc_move_type = (app->calc_move_type + 17) % 18;
        if (in->down & PK_BTN_DDOWN) app->calc_move_type = (app->calc_move_type + 1) % 18;
        if (in->down & PK_BTN_DLEFT) app->calc_def_type1 = (app->calc_def_type1 + 1) % 18;
        if (in->down & PK_BTN_DRIGHT) app->calc_def_type2 = (app->calc_def_type2 + 1) % 18;
    } else if (app->active_tab == PK_TAB_SHINY_EV) {
        if (in->down & PK_BTN_DUP) app->shiny_chain += 5;
        if (in->down & PK_BTN_DDOWN && app->shiny_chain >= 5) app->shiny_chain -= 5;
        if (in->down & PK_BTN_A) app->masuda_method = !app->masuda_method;
        if (in->down & PK_BTN_Y) app->shiny_charm = !app->shiny_charm;
    }
}

void pk_app_render(const pk_app_t *app) {
    if (app == NULL) {
        return;
    }
    pk_render_begin_frame();

    /* Top Screen (400x240) */
    pk_render_begin_target(PK_TARGET_TOP_LEFT, PK_RGBA8(16, 22, 36, 255));
    pk_draw_rect(0, 0, 400, 26, PK_RGBA8(28, 42, 72, 255));
    pk_draw_text(8, 7, 0.6f, PK_RGBA8(250, 210, 85, 255),
                 "3DS POKEDASH - PKHAX & ROMHACK COMPANION");

    if (app->saves.count > 0) {
        const pk_save_summary_t *sv = &app->saves.entries[app->selected_save];
        const pk_mon_slot_t *m = &sv->party[app->selected_slot];
        char line[128];

        snprintf(line, sizeof(line), "SAVE [%d/%d]: %s (GEN %d %s) CRC:%04X %s",
                 app->selected_save + 1, app->saves.count, sv->label, (int)sv->gen,
                 sv->is_romhack ? "ROMHACK" : "VANILLA", sv->last_checksum,
                 sv->checksum_valid ? "OK" : "ERR");
        pk_draw_text(8, 34, 0.5f, PK_RGBA8(130, 220, 255, 255), line);

        snprintf(line, sizeof(line), "OT: %s  TID:%05u SID:%05u  $%u  CANDY:%u  MBALL:%u",
                 sv->trainer_name, (unsigned)sv->tid, (unsigned)sv->sid,
                 (unsigned)sv->money, (unsigned)sv->rare_candies, (unsigned)sv->master_balls);
        pk_draw_text(8, 48, 0.5f, PK_RGBA8(210, 225, 245, 255), line);

        pk_draw_rect(6, 62, 388, 146, PK_RGBA8(24, 34, 54, 255));
        pk_draw_rect_outline(6, 62, 388, 146, 1.0f, PK_RGBA8(80, 120, 185, 255));

        snprintf(line, sizeof(line), "SLOT %d/%d: %s (%s) LV.%u %s",
                 app->selected_slot + 1, sv->party_count, m->nickname,
                 pk_species_name(m->species), (unsigned)m->level,
                 m->is_shiny ? "*SHINY*" : "NORMAL");
        pk_draw_text(14, 70, 0.55f, m->is_shiny ? PK_RGBA8(255, 215, 70, 255) : PK_RGBA8(255, 255, 255, 255), line);

        snprintf(line, sizeof(line), "NATURE: %s (%s)  FRIENDSHIP: %u  ABILITY ID: %u",
                 pk_nature_name(m->nature), pk_nature_stat_summary(m->nature),
                 (unsigned)m->friendship, (unsigned)m->ability);
        pk_draw_text(14, 88, 0.5f, PK_RGBA8(185, 235, 195, 255), line);

        snprintf(line, sizeof(line), "IVS: HP %2u | ATK %2u | DEF %2u | SPE %2u | SPA %2u | SPD %2u",
                 (unsigned)m->ivs[0], (unsigned)m->ivs[1], (unsigned)m->ivs[2],
                 (unsigned)m->ivs[3], (unsigned)m->ivs[4], (unsigned)m->ivs[5]);
        pk_draw_text(14, 108, 0.5f, PK_RGBA8(255, 220, 140, 255), line);

        snprintf(line, sizeof(line), "EVS: HP %3u| ATK %3u| DEF %3u| SPE %3u| SPA %3u| SPD %3u",
                 (unsigned)m->evs[0], (unsigned)m->evs[1], (unsigned)m->evs[2],
                 (unsigned)m->evs[3], (unsigned)m->evs[4], (unsigned)m->evs[5]);
        pk_draw_text(14, 126, 0.5f, PK_RGBA8(150, 210, 255, 255), line);

        int hp_type = pk_hidden_power_type(m->ivs);
        snprintf(line, sizeof(line), "PID: %08X   HIDDEN POWER: %s   ITEM ID: %u",
                 (unsigned)m->pid, pk_type_name(hp_type), (unsigned)m->held_item);
        pk_draw_text(14, 146, 0.5f, PK_RGBA8(210, 195, 255, 255), line);

        if (app->active_tab == PK_TAB_TYPE_CALC) {
            int eff = pk_type_effectiveness_x4(app->calc_move_type, app->calc_def_type1, app->calc_def_type2);
            const char *mult = (eff == 16) ? "4.0X SUPER" : (eff == 8) ? "2.0X SUPER" :
                               (eff == 4) ? "1.0X NEUTRAL" : (eff == 2) ? "0.5X RESIST" :
                               (eff == 1) ? "0.25X RESIST" : "0.0X IMMUNE";
            snprintf(line, sizeof(line), "TYPE CALC: %s -> %s/%s = %s",
                     pk_type_name(app->calc_move_type),
                     pk_type_name(app->calc_def_type1),
                     pk_type_name(app->calc_def_type2), mult);
            pk_draw_text(14, 174, 0.5f, PK_RGBA8(255, 190, 110, 255), line);
        } else if (app->active_tab == PK_TAB_SHINY_EV) {
            int rolls = 1 + (app->masuda_method ? 5 : 0) + (app->shiny_charm ? 2 : 0) + (app->shiny_chain / 10);
            snprintf(line, sizeof(line), "SHINY ODDS: %d/4096 (~1/%d) [MASUDA:%s CHARM:%s CHAIN:%d]",
                     rolls, 4096 / rolls, app->masuda_method ? "ON" : "OFF",
                     app->shiny_charm ? "ON" : "OFF", app->shiny_chain);
            pk_draw_text(14, 174, 0.5f, PK_RGBA8(255, 230, 95, 255), line);
        } else {
            pk_draw_text(14, 174, 0.5f, PK_RGBA8(160, 185, 215, 255),
                         "D-PAD L/R: SLOT | U/D: EDIT | A: APPLY (.BAK+CRC) | SEL: SAVE");
        }
    } else {
        pk_draw_text(14, 90, 0.55f, PK_RGBA8(255, 140, 140, 255),
                     "NO SAVES FOUND ON SD CARD (PRESS X TO RESCAN)");
    }

    pk_draw_rect(0, 216, 400, 24, PK_RGBA8(22, 32, 52, 255));
    pk_draw_text(8, 222, 0.5f, PK_RGBA8(120, 245, 170, 255), app->status_msg);

    /* Bottom Screen (320x240): Tabs + PKHaX Surgical Action Menu */
    pk_render_begin_target(PK_TARGET_BOTTOM, PK_RGBA8(14, 18, 30, 255));
    static const char *const k_tabs[3] = { "L:PKHAX EDIT", "TYPE CHART", "SHINY/EV:R" };
    for (int t = 0; t < 3; t++) {
        uint32_t bg = ((int)app->active_tab == t) ? PK_RGBA8(55, 95, 165, 255) : PK_RGBA8(28, 38, 60, 255);
        pk_draw_rect((float)(t * 106 + 2), 2.0f, 102.0f, 24.0f, bg);
        pk_draw_text((float)(t * 106 + 8), 8.0f, 0.5f, PK_RGBA8(255, 255, 255, 255), k_tabs[t]);
    }

    for (int i = 0; i < 11; i++) {
        float y = 34.0f + (float)i * 16.0f;
        bool sel = (i == app->selected_action);
        uint32_t row_bg = sel ? PK_RGBA8(65, 115, 195, 255) : PK_RGBA8(24, 32, 50, 255);
        pk_draw_rect(6.0f, y, 308.0f, 14.0f, row_bg);
        pk_draw_text(12.0f, y + 3.0f, 0.5f,
                     sel ? PK_RGBA8(255, 235, 115, 255) : PK_RGBA8(205, 215, 235, 255),
                     k_action_labels[i]);
    }

    pk_draw_text(8.0f, 218.0f, 0.5f, PK_RGBA8(160, 175, 205, 255),
                 "TOUCH OR PRESS A TO WRITE SURGICAL EDIT");

    pk_render_end_frame();
}
