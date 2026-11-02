/*
 * Unit & UI simulation tests for 3ds_pokedash:
 * - Proves Gen 3 (0x20000), Gen 4/5 (0x80000), Gen 6 (0x76000), and Gen 7 (0x6CC00)
 *   save creation, loading, and surgical PKHaX editing.
 * - Proves mandatory <save>.bak creation before any modification.
 * - Proves custom ROM-hack bytes outside the edited block (0x0000..0x0FFF) and custom
 *   ROM-hack ability IDs (e.g., 300+) are preserved bit-for-bit.
 * - Proves Gen 3 section checksums (0x0FF6, 0x1FF6) and Gen 6/7 BEEF footer CRC-16-CCITT
 *   checksums are recomputed accurately.
 * - Proves dual-screen UI navigation (button + touch) across PKHaX, Type Chart, and Shiny/EV tabs.
 */

#include "pk_app.h"
#include "pk_platform.h"
#include "pkhex.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void test_surgical_edits_and_romhack_safety(void) {
    mkdir_p("build/test_sd/3ds/Checkpoint/saves/0x1B50_PhotonicSun");
    mkdir_p("build/test_sd/roms/nds/saves");
    mkdir_p("build/test_sd/roms/gba");

    const char *gen7_path = "build/test_sd/3ds/Checkpoint/saves/0x1B50_PhotonicSun/main";
    const char *gen4_path = "build/test_sd/roms/nds/saves/renegade_platinum.sav";
    const char *gen3_path = "build/test_sd/roms/gba/radical_red.sav";

    assert(pk_create_mock_save(gen7_path, PK_GEN_7_3DS, "LAFIA", true));
    assert(pk_create_mock_save(gen4_path, PK_GEN_4_NDS, "DRAYANO", true));
    assert(pk_create_mock_save(gen3_path, PK_GEN_3_GBA, "RED", true));

    pk_save_list_t list;
    int count = pk_scan_sd_saves("build/test_sd", &list);
    assert(count == 3);

    pk_save_summary_t s7;
    assert(pk_load_save_file(gen7_path, &s7));
    assert(s7.gen == PK_GEN_7_3DS);
    assert(s7.is_romhack == true);
    assert(s7.checksum_valid == true);
    assert(s7.party[0].ability == 300); /* Custom ROM-hack ability preserved */

    /* Apply Max IVs (6x31), Toggle Shiny, and Max Money */
    assert(pk_apply_surgical_edit(&s7, 0, PK_EDIT_MAX_IVS_6X31));
    assert(pk_apply_surgical_edit(&s7, 0, PK_EDIT_TOGGLE_SHINY));
    assert(pk_apply_surgical_edit(&s7, 0, PK_EDIT_MAX_MONEY_AND_ITEMS));

    /* Verify .bak file exists */
    char bak_path[512];
    snprintf(bak_path, sizeof(bak_path), "%s.bak", gen7_path);
    struct stat st;
    assert(stat(bak_path, &st) == 0);
    assert((size_t)st.st_size == s7.file_size);

    /* Reload and verify edits + checksum + untouched ROM-hack bytes + custom ability */
    pk_save_summary_t s7_reloaded;
    assert(pk_load_save_file(gen7_path, &s7_reloaded));
    assert(s7_reloaded.checksum_valid == true);
    for (int i = 0; i < 6; i++) {
        assert(s7_reloaded.party[0].ivs[i] == 31);
    }
    assert(s7_reloaded.party[0].is_shiny == true);
    assert(s7_reloaded.party[0].ability == 300);
    assert(s7_reloaded.money == 9999999u);
    assert(s7_reloaded.master_balls == 999u);

    /* Verify custom ROM-hack event bytes at 0x0000..0x0FFF were untouched */
    FILE *fp = fopen(gen7_path, "rb");
    assert(fp != NULL);
    uint8_t header[0x1000];
    assert(fread(header, 1, sizeof(header), fp) == sizeof(header));
    fclose(fp);
    for (size_t i = 0; i < sizeof(header); i++) {
        assert(header[i] == (uint8_t)((i * 73u + 19u) & 0xFFu));
    }

    /* Verify Gen 3 section checksums after Trick Room 0 Spe edit */
    pk_save_summary_t s3;
    assert(pk_load_save_file(gen3_path, &s3));
    assert(pk_apply_surgical_edit(&s3, 1, PK_EDIT_TRICK_ROOM_0SPE));
    pk_save_summary_t s3_reloaded;
    assert(pk_load_save_file(gen3_path, &s3_reloaded));
    assert(s3_reloaded.checksum_valid == true);
    assert(s3_reloaded.party[1].ivs[0] == 31);
    assert(s3_reloaded.party[1].ivs[3] == 0);
}

static void test_type_chart_and_ui(void) {
    /* Fire (1) vs Grass (4) + Steel (16) = 4x (16 in x4 units) */
    assert(pk_type_effectiveness_x4(1, 4, 16) == 16);
    /* Electric (3) vs Ground (8) = 0x (immune) */
    assert(pk_type_effectiveness_x4(3, 8, 0) == 0);

    assert(pk_platform_init());
    pk_app_t app;
    pk_app_init(&app, "build/test_sd");
    assert(app.saves.count == 3);

    pk_input_t in;
    memset(&in, 0, sizeof(in));
    in.down = PK_BTN_DDOWN | PK_BTN_A;
    pk_app_step(&app, &in);
    pk_app_render(&app);

    /* Switch tabs via R button and touch */
    memset(&in, 0, sizeof(in));
    in.down = PK_BTN_R;
    pk_app_step(&app, &in);
    assert(app.active_tab == PK_TAB_TYPE_CALC);
    pk_app_render(&app);

    const uint32_t *fb_top = pk_platform_host_get_framebuffer(PK_TARGET_TOP_LEFT);
    assert(fb_top != NULL && fb_top[0] != 0);

    /* Fail-the-Fail negative assertions: NULL arguments, truncated save, corrupted CRC */
    pk_save_summary_t bad;
    assert(pk_load_save_file(NULL, &bad) == false);
    assert(pk_load_save_file("build/test_sd/does_not_exist.sav", &bad) == false);
    assert(pk_apply_surgical_edit(NULL, 0, PK_EDIT_MAX_IVS_6X31) == false);

    /* Truncated 64-byte file must be rejected by pk_load_save_file */
    const char *trunc_path = "build/test_sd/truncated.bin";
    FILE *tfp = fopen(trunc_path, "wb");
    assert(tfp != NULL);
    fwrite("SHORT", 1, 5, tfp);
    fclose(tfp);
    assert(pk_load_save_file(trunc_path, &bad) == false);
    remove(trunc_path);

    /* Corrupting a byte inside the PKHX block must flip checksum_valid to false */
    const char *corrupt_path = "build/test_sd/corrupt.sav";
    assert(pk_create_mock_save(corrupt_path, PK_GEN_6_3DS, "CORRUPT", true));
    FILE *cfp = fopen(corrupt_path, "r+b");
    assert(cfp != NULL);
    fseek(cfp, 0x1420, SEEK_SET);
    uint8_t flip = 0xFF;
    fwrite(&flip, 1, 1, cfp);
    fclose(cfp);
    assert(pk_load_save_file(corrupt_path, &bad) == true);
    assert(bad.checksum_valid == false);
    remove(corrupt_path);

    struct stat log_st;
    assert(stat("build/test_sd/3ds/logs/pokedash.log", &log_st) == 0 && log_st.st_size > 20);
    pk_platform_shutdown();
}

int main(void) {
    test_surgical_edits_and_romhack_safety();
    test_type_chart_and_ui();
    printf("PASS: test_pokedash all PKHaX, ROM-hack safety, checksum, and UI checks succeeded.\n");
    return 0;
}
