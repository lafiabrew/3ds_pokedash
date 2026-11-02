#ifndef PK_APP_H
#define PK_APP_H

#include "pk_platform.h"
#include "pkhex.h"

typedef enum {
    PK_TAB_PKHAX = 0,
    PK_TAB_TYPE_CALC = 1,
    PK_TAB_SHINY_EV = 2
} pk_tab_t;

typedef struct {
    char sd_root[PK_PATH_MAX];
    pk_save_list_t saves;
    int selected_save;
    int selected_slot;
    int selected_action;
    pk_tab_t active_tab;
    int calc_move_type;
    int calc_def_type1;
    int calc_def_type2;
    int shiny_chain;
    bool masuda_method;
    bool shiny_charm;
    char status_msg[128];
} pk_app_t;

void pk_app_init(pk_app_t *app, const char *sd_root);
void pk_app_step(pk_app_t *app, const pk_input_t *input);
void pk_app_render(const pk_app_t *app);

#endif /* PK_APP_H */
