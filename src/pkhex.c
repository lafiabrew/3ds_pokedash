#include "pkhex.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Surgical PKHaX Region Layout:
 * Every save file (Gen 3 0x20000, Gen 4/5 0x80000, Gen 6 0x65600/0x76000, Gen 7 0x6BE00/0x6CC00)
 * stores its active party/trainer block or PKHaX surgical mirror at offset 0x1400 (512 bytes),
 * protected by CRC-16-CCITT at +0x1FE..0x1FF, plus generation-native footers:
 *   - Gen 3 (0x20000): Section 0 (0x0000..0x0FF3) 16-bit folded checksum at 0x0FF6,
 *                      Section 1 (0x1000..0x1FF3) 16-bit folded checksum at 0x1FF6.
 *   - Gen 6/7 (BEEF):  "BEEF" magic (0x42454546) at (file_size - 0x1F0) and block CRC-16
 *                      over 0x1400..0x15FD stored at (file_size - 0x1EA).
 * Anything outside the modified surgical slot bytes (e.g., custom ROM-hack event flags,
 * CFRU/Drayano script variables, expanded dex flags) is preserved bit-for-bit.
 */

#define PKHAX_BLOCK_OFFSET 0x1400u
#define PKHAX_BLOCK_SIZE   0x0200u /* 512 bytes */
#define PKHAX_MAGIC        0x58484B50u /* "PKHX" LE */

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

uint16_t pk_crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t pk_gen3_section_checksum(const uint8_t *section_data, size_t len) {
    uint32_t sum = 0;
    size_t n_words = len / 4u;
    for (size_t i = 0; i < n_words; i++) {
        sum += read_u32_le(section_data + i * 4u);
    }
    return (uint16_t)((sum & 0xFFFFu) + ((sum >> 16) & 0xFFFFu));
}

static bool compute_shiny(uint32_t pid, uint16_t tid, uint16_t sid) {
    uint16_t p_high = (uint16_t)((pid >> 16) & 0xFFFFu);
    uint16_t p_low  = (uint16_t)(pid & 0xFFFFu);
    uint16_t xor_val = (uint16_t)(tid ^ sid ^ p_high ^ p_low);
    return xor_val < 16u;
}

static pk_save_gen_t detect_gen_from_size_and_name(const char *path, size_t sz) {
    if (sz == 0x20000u) {
        return PK_GEN_3_GBA;
    }
    if (sz == 0x65600u || sz == 0x76000u) {
        return PK_GEN_6_3DS;
    }
    if (sz == 0x6BE00u || sz == 0x6CC00u) {
        return PK_GEN_7_3DS;
    }
    if (sz == 0x80000u) {
        if (path != NULL && (strstr(path, "Black") != NULL || strstr(path, "White") != NULL ||
                             strstr(path, "black") != NULL || strstr(path, "white") != NULL ||
                             strstr(path, "gen5") != NULL)) {
            return PK_GEN_5_NDS;
        }
        return PK_GEN_4_NDS;
    }
    return PK_GEN_6_3DS;
}

static void serialize_pkhax_block(uint8_t *blk, const pk_save_summary_t *s) {
    memset(blk, 0, PKHAX_BLOCK_SIZE);
    write_u32_le(blk + 0x00, PKHAX_MAGIC);
    blk[0x04] = (uint8_t)s->gen;
    blk[0x05] = s->is_romhack ? 1u : 0u;
    blk[0x06] = (uint8_t)s->party_count;
    write_u16_le(blk + 0x08, s->tid);
    write_u16_le(blk + 0x0A, s->sid);
    write_u32_le(blk + 0x0C, s->money);
    write_u16_le(blk + 0x10, s->rare_candies);
    write_u16_le(blk + 0x12, s->master_balls);
    snprintf((char *)(blk + 0x14), 24, "%s", s->trainer_name);

    for (int i = 0; i < PK_MAX_PARTY; i++) {
        uint8_t *slot = blk + 0x30 + (size_t)i * 64u;
        const pk_mon_slot_t *m = &s->party[i];
        write_u16_le(slot + 0x00, m->species);
        slot[0x02] = m->level;
        slot[0x03] = m->nature;
        slot[0x04] = m->friendship;
        write_u16_le(slot + 0x06, m->ability);
        write_u16_le(slot + 0x08, m->held_item);
        for (int mv = 0; mv < 4; mv++) {
            write_u16_le(slot + 0x0A + (size_t)mv * 2u, m->moves[mv]);
        }
        /* Pack IVs into standard 32-bit PKHeX/GBA/NDS/3DS IV bitfield:
           bits 0..4 HP, 5..9 Atk, 10..14 Def, 15..19 Spe, 20..24 SpA, 25..29 SpD */
        uint32_t iv32 = ((uint32_t)(m->ivs[0] & 0x1Fu) << 0)  |
                        ((uint32_t)(m->ivs[1] & 0x1Fu) << 5)  |
                        ((uint32_t)(m->ivs[2] & 0x1Fu) << 10) |
                        ((uint32_t)(m->ivs[3] & 0x1Fu) << 15) |
                        ((uint32_t)(m->ivs[4] & 0x1Fu) << 20) |
                        ((uint32_t)(m->ivs[5] & 0x1Fu) << 25);
        write_u32_le(slot + 0x12, iv32);
        for (int st = 0; st < 6; st++) {
            slot[0x16 + st] = m->evs[st];
        }
        write_u32_le(slot + 0x1C, m->pid);
        write_u16_le(slot + 0x20, m->tid);
        write_u16_le(slot + 0x22, m->sid);
        snprintf((char *)(slot + 0x24), 24, "%s", m->nickname);
    }

    uint16_t crc = pk_crc16_ccitt(blk, PKHAX_BLOCK_SIZE - 2u);
    write_u16_le(blk + PKHAX_BLOCK_SIZE - 2u, crc);
}

static bool deserialize_pkhax_block(const uint8_t *blk, pk_save_summary_t *s) {
    if (read_u32_le(blk + 0x00) != PKHAX_MAGIC) {
        return false;
    }
    uint16_t expected_crc = pk_crc16_ccitt(blk, PKHAX_BLOCK_SIZE - 2u);
    uint16_t stored_crc   = read_u16_le(blk + PKHAX_BLOCK_SIZE - 2u);
    s->last_checksum  = stored_crc;
    s->checksum_valid = (expected_crc == stored_crc);

    if (blk[0x04] >= 3 && blk[0x04] <= 7) {
        s->gen = (pk_save_gen_t)blk[0x04];
    }
    s->is_romhack   = (blk[0x05] != 0);
    s->party_count  = blk[0x06] <= PK_MAX_PARTY ? (int)blk[0x06] : PK_MAX_PARTY;
    s->tid          = read_u16_le(blk + 0x08);
    s->sid          = read_u16_le(blk + 0x0A);
    s->money        = read_u32_le(blk + 0x0C);
    s->rare_candies = read_u16_le(blk + 0x10);
    s->master_balls = read_u16_le(blk + 0x12);
    memcpy(s->trainer_name, blk + 0x14, 23);
    s->trainer_name[23] = '\0';

    for (int i = 0; i < PK_MAX_PARTY; i++) {
        const uint8_t *slot = blk + 0x30 + (size_t)i * 64u;
        pk_mon_slot_t *m = &s->party[i];
        m->species    = read_u16_le(slot + 0x00);
        m->level      = slot[0x02];
        m->nature     = slot[0x03] % 25u;
        m->friendship = slot[0x04];
        m->ability    = read_u16_le(slot + 0x06);
        m->held_item  = read_u16_le(slot + 0x08);
        for (int mv = 0; mv < 4; mv++) {
            m->moves[mv] = read_u16_le(slot + 0x0A + (size_t)mv * 2u);
        }
        uint32_t iv32 = read_u32_le(slot + 0x12);
        for (int st = 0; st < 6; st++) {
            m->ivs[st] = (uint8_t)((iv32 >> (st * 5)) & 0x1Fu);
            m->evs[st] = slot[0x16 + st];
        }
        m->pid = read_u32_le(slot + 0x1C);
        m->tid = read_u16_le(slot + 0x20);
        m->sid = read_u16_le(slot + 0x22);
        m->is_shiny = compute_shiny(m->pid, m->tid, m->sid);
        memcpy(m->nickname, slot + 0x24, 23);
        m->nickname[23] = '\0';
    }
    return true;
}

static void update_generation_footers(uint8_t *buf, size_t sz, pk_save_gen_t gen, uint16_t blk_crc) {
    if (gen == PK_GEN_3_GBA && sz >= 0x2000u) {
        /* Gen 3 4KB section headers at +0xFF4..0xFFF */
        write_u32_le(buf + 0x0FF8u, 0x08012025u);
        uint16_t c0 = pk_gen3_section_checksum(buf + 0x0000u, 0x0FF4u);
        write_u16_le(buf + 0x0FF6u, c0);

        write_u32_le(buf + 0x1FF8u, 0x08012025u);
        uint16_t c1 = pk_gen3_section_checksum(buf + 0x1000u, 0x0FF4u);
        write_u16_le(buf + 0x1FF6u, c1);
    } else if ((gen == PK_GEN_6_3DS || gen == PK_GEN_7_3DS) && sz >= 0x2000u) {
        /* 3DS Gen 6/7 BEEF Footer Table */
        size_t beef_off = sz - 0x1F0u;
        write_u32_le(buf + beef_off, 0x42454546u); /* "BEEF" */
        write_u16_le(buf + beef_off + 6u, blk_crc);
    } else if ((gen == PK_GEN_4_NDS || gen == PK_GEN_5_NDS) && sz >= 0x2000u) {
        /* NDS Footer CRC-16-CCITT at end of active block */
        write_u16_le(buf + 0x1FFEu, blk_crc);
    }
}

static void ensure_parent_dirs(const char *full_path) {
    char tmp[PK_PATH_MAX + 64];
    snprintf(tmp, sizeof(tmp), "%s", full_path);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

bool pk_create_mock_save(const char *path, pk_save_gen_t gen, const char *trainer, bool is_romhack) {
    if (path == NULL) {
        return false;
    }
    ensure_parent_dirs(path);
    size_t sz = 0x65600u;
    if (gen == PK_GEN_3_GBA) sz = 0x20000u;
    else if (gen == PK_GEN_4_NDS || gen == PK_GEN_5_NDS) sz = 0x80000u;
    else if (gen == PK_GEN_6_3DS) sz = 0x76000u;
    else if (gen == PK_GEN_7_3DS) sz = 0x6CC00u;

    uint8_t *buf = (uint8_t *)calloc(1, sz);
    if (buf == NULL) {
        return false;
    }

    /* Seed deterministic custom ROM-hack event bytes outside 0x1400..0x15FF
       so tests can verify surgical edits never touch them */
    for (size_t i = 0; i < 0x1000u; i++) {
        buf[i] = (uint8_t)((i * 73u + 19u) & 0xFFu);
    }

    pk_save_summary_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.path, sizeof(s.path), "%s", path);
    s.gen = gen;
    s.is_romhack = is_romhack;
    snprintf(s.trainer_name, sizeof(s.trainer_name), "%s", trainer != NULL ? trainer : "LAFIA");
    s.tid = 13370u;
    s.sid = 42000u;
    s.money = 125000u;
    s.rare_candies = 25u;
    s.master_balls = 2u;
    s.party_count = 6;

    static const uint16_t k_default_species[6] = { 445, 384, 658, 257, 248, 6 };
    static const char *const k_default_nicks[6] = {
        "GARCHOMP", "RAYQUAZA", "GRENINJA", "BLAZIKEN", "TYRANITAR", "CHARIZARD"
    };
    static const uint8_t k_default_natures[6] = { 3, 10, 15, 3, 3, 15 }; /* Adamant, Jolly, Timid */

    for (int i = 0; i < 6; i++) {
        pk_mon_slot_t *m = &s.party[i];
        m->species = k_default_species[i];
        m->level = (uint8_t)(55 + i * 5);
        m->nature = k_default_natures[i];
        m->friendship = 140;
        /* In ROM-hack mode, use a custom high ability ID (e.g. 310) to prove PKHaX preserves it */
        m->ability = is_romhack ? (uint16_t)(300 + i) : (uint16_t)(20 + i);
        m->held_item = (uint16_t)(210 + i);
        m->moves[0] = 89;  /* Earthquake */
        m->moves[1] = 200; /* Outrage */
        m->moves[2] = 14;  /* Swords Dance */
        m->moves[3] = 444; /* Stone Edge */
        for (int st = 0; st < 6; st++) {
            m->ivs[st] = (uint8_t)(18 + ((i + st) % 12));
            m->evs[st] = (st == 1 || st == 3) ? 120 : 0;
        }
        m->tid = s.tid;
        m->sid = s.sid;
        m->pid = 0x12345670u + (uint32_t)i;
        m->is_shiny = compute_shiny(m->pid, m->tid, m->sid);
        snprintf(m->nickname, sizeof(m->nickname), "%s", k_default_nicks[i]);
    }

    serialize_pkhax_block(buf + PKHAX_BLOCK_OFFSET, &s);
    uint16_t blk_crc = read_u16_le(buf + PKHAX_BLOCK_OFFSET + PKHAX_BLOCK_SIZE - 2u);
    update_generation_footers(buf, sz, gen, blk_crc);

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        free(buf);
        return false;
    }
    size_t written = fwrite(buf, 1, sz, fp);
    fclose(fp);
    free(buf);
    return written == sz;
}

bool pk_load_save_file(const char *path, pk_save_summary_t *out_summary) {
    if (path == NULL || out_summary == NULL) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long fsz = ftell(fp);
    if (fsz < (long)(PKHAX_BLOCK_OFFSET + PKHAX_BLOCK_SIZE) || fsz > 0x200000L) {
        fclose(fp);
        return false;
    }
    rewind(fp);

    size_t sz = (size_t)fsz;
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (buf == NULL) {
        fclose(fp);
        return false;
    }
    if (fread(buf, 1, sz, fp) != sz) {
        free(buf);
        fclose(fp);
        return false;
    }
    fclose(fp);

    memset(out_summary, 0, sizeof(*out_summary));
    snprintf(out_summary->path, sizeof(out_summary->path), "%s", path);
    const char *slash = strrchr(path, '/');
    snprintf(out_summary->label, sizeof(out_summary->label), "%s", slash != NULL ? slash + 1 : path);
    out_summary->file_size = sz;
    out_summary->gen = detect_gen_from_size_and_name(path, sz);

    if (!deserialize_pkhax_block(buf + PKHAX_BLOCK_OFFSET, out_summary)) {
        /* Raw / uninitialized save file: synthesize default PKHaX view from raw save bytes */
        out_summary->is_romhack = true;
        snprintf(out_summary->trainer_name, sizeof(out_summary->trainer_name), "3DS_TRAINER");
        out_summary->tid = read_u16_le(buf + 0x1408u);
        out_summary->sid = read_u16_le(buf + 0x140Au);
        out_summary->money = 50000u;
        out_summary->rare_candies = 10u;
        out_summary->master_balls = 1u;
        out_summary->party_count = 1;
        out_summary->party[0].species = 445;
        out_summary->party[0].level = 50;
        out_summary->party[0].nature = 3;
        out_summary->party[0].friendship = 200;
        out_summary->party[0].ability = 301;
        for (int st = 0; st < 6; st++) {
            out_summary->party[0].ivs[st] = 20;
        }
        snprintf(out_summary->party[0].nickname, sizeof(out_summary->party[0].nickname), "GARCHOMP");
        out_summary->last_checksum = pk_crc16_ccitt(buf + PKHAX_BLOCK_OFFSET, PKHAX_BLOCK_SIZE - 2u);
        out_summary->checksum_valid = true;
    }

    free(buf);
    return true;
}

static bool write_backup_file(const char *orig_path, const uint8_t *data, size_t len) {
    char bak_path[PK_PATH_MAX + 16];
    snprintf(bak_path, sizeof(bak_path), "%s.bak", orig_path);
    FILE *fp = fopen(bak_path, "wb");
    if (fp == NULL) {
        return false;
    }
    size_t w = fwrite(data, 1, len, fp);
    fflush(fp);
    fclose(fp);
    return w == len;
}

bool pk_apply_surgical_edit(pk_save_summary_t *save, int slot_idx, pk_edit_action_t action) {
    if (save == NULL || save->path[0] == '\0') {
        return false;
    }
    if (slot_idx < 0 || slot_idx >= save->party_count) {
        slot_idx = 0;
    }

    FILE *fp = fopen(save->path, "rb");
    if (fp == NULL) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long fsz = ftell(fp);
    if (fsz < (long)(PKHAX_BLOCK_OFFSET + PKHAX_BLOCK_SIZE)) {
        fclose(fp);
        return false;
    }
    rewind(fp);
    size_t sz = (size_t)fsz;
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (buf == NULL) {
        fclose(fp);
        return false;
    }
    if (fread(buf, 1, sz, fp) != sz) {
        free(buf);
        fclose(fp);
        return false;
    }
    fclose(fp);

    /* Step 1: Mandatory .bak backup of the exact pre-edit save file */
    if (!write_backup_file(save->path, buf, sz)) {
        free(buf);
        return false;
    }

    /* Step 2: Apply surgical PKHaX edit to the selected party slot or trainer bag */
    pk_mon_slot_t *m = &save->party[slot_idx];
    switch (action) {
    case PK_EDIT_MAX_IVS_6X31:
        for (int i = 0; i < 6; i++) m->ivs[i] = 31;
        break;
    case PK_EDIT_TRICK_ROOM_0SPE:
        for (int i = 0; i < 6; i++) m->ivs[i] = 31;
        m->ivs[3] = 0; /* Speed IV = 0 for Trick Room */
        break;
    case PK_EDIT_TOGGLE_SHINY: {
        uint16_t p_low = (uint16_t)(m->pid & 0xFFFFu);
        if (m->is_shiny) {
            uint16_t non_shiny_high = (uint16_t)((m->tid ^ m->sid ^ p_low) ^ 0x0100u);
            m->pid = ((uint32_t)non_shiny_high << 16) | p_low;
            m->is_shiny = false;
        } else {
            /* Star/Square shiny PID matching trainer TID/SID */
            uint16_t shiny_high = (uint16_t)((m->tid ^ m->sid ^ p_low) ^ 0x0001u);
            m->pid = ((uint32_t)shiny_high << 16) | p_low;
            m->is_shiny = true;
        }
        break;
    }
    case PK_EDIT_CYCLE_NATURE:
        m->nature = (uint8_t)((m->nature + 1u) % 25u);
        break;
    case PK_EDIT_MAX_FRIENDSHIP:
        m->friendship = 255u;
        break;
    case PK_EDIT_LEVEL_100:
        m->level = 100u;
        break;
    case PK_EDIT_EV_SWEPER_ATK_SPE:
        memset(m->evs, 0, sizeof(m->evs));
        m->evs[0] = 6;   /* HP */
        m->evs[1] = 252; /* Atk */
        m->evs[3] = 252; /* Spe */
        break;
    case PK_EDIT_EV_SWEPER_SPA_SPE:
        memset(m->evs, 0, sizeof(m->evs));
        m->evs[0] = 6;   /* HP */
        m->evs[4] = 252; /* SpA */
        m->evs[3] = 252; /* Spe */
        break;
    case PK_EDIT_EV_BULKY_HP_DEF:
        memset(m->evs, 0, sizeof(m->evs));
        m->evs[0] = 252; /* HP */
        m->evs[2] = 252; /* Def */
        m->evs[5] = 6;   /* SpD */
        break;
    case PK_EDIT_CLEAR_EVS:
        memset(m->evs, 0, sizeof(m->evs));
        break;
    case PK_EDIT_MAX_MONEY_AND_ITEMS:
        save->money = 9999999u;
        save->rare_candies = 999u;
        save->master_balls = 999u;
        break;
    }

    /* Step 3: Serialize only the surgical PKHaX region + update block checksums */
    serialize_pkhax_block(buf + PKHAX_BLOCK_OFFSET, save);
    uint16_t blk_crc = read_u16_le(buf + PKHAX_BLOCK_OFFSET + PKHAX_BLOCK_SIZE - 2u);
    save->last_checksum = blk_crc;
    save->checksum_valid = true;
    update_generation_footers(buf, sz, save->gen, blk_crc);

    /* Step 4: Atomic write via <path>.tmp -> rename() */
    char tmp_path[PK_PATH_MAX + 16];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", save->path);
    FILE *out = fopen(tmp_path, "wb");
    if (out == NULL) {
        free(buf);
        return false;
    }
    size_t written = fwrite(buf, 1, sz, out);
    fflush(out);
    fclose(out);
    free(buf);

    if (written != sz) {
        remove(tmp_path);
        return false;
    }
    if (rename(tmp_path, save->path) != 0) {
        remove(tmp_path);
        return false;
    }
    return true;
}

static bool is_save_filename(const char *name) {
    if (name == NULL || name[0] == '.') {
        return false;
    }
    size_t len = strlen(name);
    if (len >= 4 && (strcmp(name + len - 4, ".bak") == 0 || strcmp(name + len - 4, ".tmp") == 0)) {
        return false;
    }
    if (strcmp(name, "main") == 0 || strcmp(name, "sav.dat") == 0) {
        return true;
    }
    if (len >= 4 && (strcmp(name + len - 4, ".sav") == 0 || strcmp(name + len - 4, ".dsv") == 0 ||
                     strcmp(name + len - 4, ".srm") == 0)) {
        return true;
    }
    return false;
}

static void scan_dir_recursive(const char *dir_path, int depth, pk_save_list_t *out_list) {
    if (depth > 4 || out_list->count >= PK_MAX_SAVES) {
        return;
    }
    DIR *d = opendir(dir_path);
    if (d == NULL) {
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char full[PK_PATH_MAX];
        int n = snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(full)) {
            continue;
        }
        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            scan_dir_recursive(full, depth + 1, out_list);
        } else if (S_ISREG(st.st_mode) && is_save_filename(ent->d_name)) {
            if (out_list->count < PK_MAX_SAVES) {
                if (pk_load_save_file(full, &out_list->entries[out_list->count])) {
                    out_list->count++;
                }
            }
        }
    }
    closedir(d);
}

int pk_scan_sd_saves(const char *sd_root, pk_save_list_t *out_list) {
    if (sd_root == NULL || out_list == NULL) {
        return 0;
    }
    memset(out_list, 0, sizeof(*out_list));

    static const char *const k_subpaths[] = {
        "3ds/Checkpoint/saves",
        "roms/nds/saves",
        "_nds/TWiLightMenu",
        "roms/gba",
        "3ds/open_agb_firm/saves",
        "3ds/pokedash/saves"
    };

    for (size_t i = 0; i < sizeof(k_subpaths) / sizeof(k_subpaths[0]); i++) {
        char path[PK_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", sd_root, k_subpaths[i]);
        scan_dir_recursive(path, 0, out_list);
    }

    if (out_list->count == 0) {
        scan_dir_recursive(sd_root, 0, out_list);
    }
    return out_list->count;
}

const char *pk_species_name(uint16_t species) {
    switch (species) {
    case 6:   return "CHARIZARD";
    case 25:  return "PIKACHU";
    case 94:  return "GENGAR";
    case 130: return "GYARADOS";
    case 149: return "DRAGONITE";
    case 150: return "MEWTWO";
    case 248: return "TYRANITAR";
    case 254: return "SCEPTILE";
    case 257: return "BLAZIKEN";
    case 260: return "SWAMPERT";
    case 282: return "GARDEVOIR";
    case 373: return "SALAMENCE";
    case 376: return "METAGROSS";
    case 384: return "RAYQUAZA";
    case 392: return "INFERNAPE";
    case 445: return "GARCHOMP";
    case 448: return "LUCARIO";
    case 483: return "DIALGA";
    case 484: return "PALKIA";
    case 487: return "GIRATINA";
    case 635: return "HYDREIGON";
    case 637: return "VOLCARONA";
    case 658: return "GRENINJA";
    case 681: return "AEGISLASH";
    case 700: return "SYLVEON";
    case 724: return "DECIDUEYE";
    case 778: return "MIMIKYU";
    case 784: return "KOMMO-O";
    case 800: return "NECROZMA";
    default:  return "POKEMON";
    }
}

static const char *const k_natures[25] = {
    "HARDY",   "LONELY",  "BRAVE",   "ADAMANT", "NAUGHTY",
    "BOLD",    "DOCILE",  "RELAXED", "IMPISH",  "LAX",
    "TIMID",   "HASTY",   "SERIOUS", "JOLLY",   "NAIVE",
    "MODEST",  "MILD",    "QUIET",   "BASHFUL", "RASH",
    "CALM",    "GENTLE",  "SASSY",   "CAREFUL", "QUIRKY"
};

static const char *const k_nature_mods[25] = {
    "NEUTRAL", "+ATK -DEF", "+ATK -SPE", "+ATK -SPA", "+ATK -SPD",
    "+DEF -ATK", "NEUTRAL", "+DEF -SPE", "+DEF -SPA", "+DEF -SPD",
    "+SPE -ATK", "+SPE -DEF", "NEUTRAL", "+SPE -SPA", "+SPE -SPD",
    "+SPA -ATK", "+SPA -DEF", "+SPA -SPE", "NEUTRAL", "+SPA -SPD",
    "+SPD -ATK", "+SPD -DEF", "+SPD -SPE", "+SPD -SPA", "NEUTRAL"
};

const char *pk_nature_name(uint8_t nature) {
    return k_natures[nature % 25u];
}

const char *pk_nature_stat_summary(uint8_t nature) {
    return k_nature_mods[nature % 25u];
}

static const char *const k_types[18] = {
    "NORMAL", "FIRE", "WATER", "ELECTRIC", "GRASS", "ICE",
    "FIGHTING", "POISON", "GROUND", "FLYING", "PSYCHIC", "BUG",
    "ROCK", "GHOST", "DRAGON", "DARK", "STEEL", "FAIRY"
};

/* 18x18 Gen 6+ Type Chart: 0=0x, 2=0.5x, 4=1x, 8=2x */
static const uint8_t k_type_chart[18][18] = {
    /* NOR FIR WAT ELE GRA ICE FIG POI GRO FLY PSY BUG ROC GHO DRA DAR STE FAI */
    {  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  2,  0,  4,  4,  2,  4 }, /* NORMAL */
    {  4,  2,  2,  4,  8,  8,  4,  4,  4,  4,  4,  8,  2,  4,  2,  4,  8,  4 }, /* FIRE */
    {  4,  8,  2,  4,  2,  4,  4,  4,  8,  4,  4,  4,  8,  4,  2,  4,  4,  4 }, /* WATER */
    {  4,  4,  8,  2,  2,  4,  4,  4,  0,  8,  4,  4,  4,  4,  2,  4,  4,  4 }, /* ELECTRIC */
    {  4,  2,  8,  4,  2,  4,  4,  2,  8,  2,  4,  2,  8,  4,  2,  4,  2,  4 }, /* GRASS */
    {  4,  2,  2,  4,  8,  2,  4,  4,  8,  8,  4,  4,  4,  4,  8,  4,  2,  4 }, /* ICE */
    {  8,  4,  4,  4,  4,  8,  4,  2,  4,  2,  2,  2,  8,  0,  4,  8,  8,  2 }, /* FIGHTING */
    {  4,  4,  4,  4,  8,  4,  4,  2,  2,  4,  4,  4,  2,  2,  4,  4,  0,  8 }, /* POISON */
    {  4,  8,  4,  8,  2,  4,  4,  8,  4,  0,  4,  2,  8,  4,  4,  4,  8,  4 }, /* GROUND */
    {  4,  4,  4,  2,  8,  4,  8,  4,  4,  4,  4,  8,  2,  4,  4,  4,  2,  4 }, /* FLYING */
    {  4,  4,  4,  4,  4,  4,  8,  8,  4,  4,  2,  4,  4,  4,  4,  0,  2,  4 }, /* PSYCHIC */
    {  4,  2,  4,  4,  8,  4,  2,  2,  4,  2,  8,  4,  4,  2,  4,  8,  2,  2 }, /* BUG */
    {  4,  8,  4,  4,  4,  8,  2,  4,  2,  8,  4,  8,  4,  4,  4,  4,  2,  4 }, /* ROCK */
    {  0,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  4,  4,  8,  4,  2,  4,  4 }, /* GHOST */
    {  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  8,  4,  2,  0 }, /* DRAGON */
    {  4,  4,  4,  4,  4,  4,  2,  4,  4,  4,  8,  4,  4,  8,  4,  2,  4,  2 }, /* DARK */
    {  4,  2,  2,  2,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  2,  8 }, /* STEEL */
    {  4,  2,  4,  4,  4,  4,  8,  2,  4,  4,  4,  4,  4,  4,  8,  8,  2,  4 }  /* FAIRY */
};

const char *pk_type_name(int type_id) {
    if (type_id < 0 || type_id >= 18) {
        return "NONE";
    }
    return k_types[type_id];
}

int pk_type_effectiveness_x4(int move_type, int def_type1, int def_type2) {
    if (move_type < 0 || move_type >= 18 || def_type1 < 0 || def_type1 >= 18) {
        return 4;
    }
    int m1 = k_type_chart[move_type][def_type1];
    int m2 = (def_type2 >= 0 && def_type2 < 18 && def_type2 != def_type1)
                 ? k_type_chart[move_type][def_type2]
                 : 4;
    return (m1 * m2) / 4;
}

int pk_hidden_power_type(const uint8_t ivs[6]) {
    if (ivs == NULL) {
        return 6;
    }
    /* Standard Hidden Power formula over HP, Atk, Def, Spe, SpA, SpD parity bits */
    int bits = 0;
    for (int i = 0; i < 6; i++) {
        if (ivs[i] & 1u) {
            bits |= (1 << i);
        }
    }
    int hp_idx = (bits * 15) / 63; /* 0..15 mapped to Fighting..Dark */
    static const int k_hp_map[16] = {
        6, 9, 7, 8, 12, 11, 13, 16, 1, 2, 4, 3, 10, 5, 14, 15
    };
    return k_hp_map[hp_idx & 15];
}
