#ifndef PKHEX_H
#define PKHEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PK_MAX_SAVES 32
#define PK_MAX_PARTY 6
#define PK_PATH_MAX 512

typedef enum {
    PK_GEN_UNKNOWN = 0,
    PK_GEN_3_GBA   = 3, /* 0x20000 (128 KB) GBA .sav + ROM hacks (Radical Red, Unbound, Emerald) */
    PK_GEN_4_NDS   = 4, /* 0x80000 (512 KB) NDS Gen 4 .sav (Plat, HGSS, Renegade Platinum) */
    PK_GEN_5_NDS   = 5, /* 0x80000 (512 KB) NDS Gen 5 .sav (BW, B2W2, Blaze Black) */
    PK_GEN_6_3DS   = 6, /* 0x65600 XY / 0x76000 ORAS Checkpoint 'main' save (BEEF footer) */
    PK_GEN_7_3DS   = 7  /* 0x6BE00 SM / 0x6CC00 USUM Checkpoint 'main' save (BEEF footer) */
} pk_save_gen_t;

typedef struct {
    uint16_t species;
    uint8_t level;
    uint8_t nature;       /* 0..24 */
    uint8_t friendship;   /* 0..255 */
    uint16_t ability;     /* Preserved in PKHaX mode even if custom ROM-hack ID */
    uint16_t held_item;
    uint16_t moves[4];
    uint8_t ivs[6];       /* HP, Atk, Def, Spe, SpA, SpD (0..31) */
    uint8_t evs[6];       /* HP, Atk, Def, Spe, SpA, SpD (0..252) */
    uint32_t pid;
    uint16_t tid;
    uint16_t sid;
    bool is_shiny;
    char nickname[24];
} pk_mon_slot_t;

typedef struct {
    char path[PK_PATH_MAX];
    char label[64];
    pk_save_gen_t gen;
    bool is_romhack;
    char trainer_name[24];
    uint16_t tid;
    uint16_t sid;
    uint32_t money;
    uint16_t rare_candies;
    uint16_t master_balls;
    int party_count;
    pk_mon_slot_t party[PK_MAX_PARTY];
    uint16_t last_checksum;
    bool checksum_valid;
    size_t file_size;
} pk_save_summary_t;

typedef struct {
    pk_save_summary_t entries[PK_MAX_SAVES];
    int count;
} pk_save_list_t;

typedef enum {
    PK_EDIT_MAX_IVS_6X31 = 0,
    PK_EDIT_TRICK_ROOM_0SPE,
    PK_EDIT_TOGGLE_SHINY,
    PK_EDIT_CYCLE_NATURE,
    PK_EDIT_MAX_FRIENDSHIP,
    PK_EDIT_LEVEL_100,
    PK_EDIT_EV_SWEPER_ATK_SPE,
    PK_EDIT_EV_SWEPER_SPA_SPE,
    PK_EDIT_EV_BULKY_HP_DEF,
    PK_EDIT_CLEAR_EVS,
    PK_EDIT_MAX_MONEY_AND_ITEMS
} pk_edit_action_t;

/* CRC-16-CCITT (0x1021, init 0xFFFF) used by 3DS Gen 6/7 BEEF blocks & PKHaX slots */
uint16_t pk_crc16_ccitt(const uint8_t *data, size_t len);

/* Gen 3 16-bit folded 32-bit word sum checksum (used at offset +0xFF6 of 4KB section) */
uint16_t pk_gen3_section_checksum(const uint8_t *section_data, size_t len);

/* Detect generation and parse save file without modifying any bytes */
bool pk_load_save_file(const char *path, pk_save_summary_t *out_summary);

/* Create a valid sample save file (Gen 3/4/5/6/7) with embedded PKHaX block for testing or demo */
bool pk_create_mock_save(const char *path, pk_save_gen_t gen, const char *trainer, bool is_romhack);

/* Apply a surgical PKHaX edit to party slot `slot_idx`, creating `<path>.bak` first,
   recomputing block + BEEF/Gen3 checksums, and writing atomically via `<path>.tmp` -> `rename()`. */
bool pk_apply_surgical_edit(pk_save_summary_t *save, int slot_idx, pk_edit_action_t action);

/* Scan SD root (`Checkpoint/saves`, `roms/nds/saves`, `_nds/TWiLightMenu`, `roms/gba`) */
int pk_scan_sd_saves(const char *sd_root, pk_save_list_t *out_list);

/* Pokémon Type/Nature/Hidden-Power/Shiny Companion Helpers */
const char *pk_species_name(uint16_t species);
const char *pk_nature_name(uint8_t nature);
const char *pk_nature_stat_summary(uint8_t nature);
const char *pk_type_name(int type_id);
/* Returns effectiveness x4 (0=0x, 1=0.25x, 2=0.5x, 4=1x, 8=2x, 16=4x) */
int pk_type_effectiveness_x4(int move_type, int def_type1, int def_type2);
int pk_hidden_power_type(const uint8_t ivs[6]);

#endif /* PKHEX_H */
