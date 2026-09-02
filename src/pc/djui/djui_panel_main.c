#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_host.h"
#include "djui_panel_join.h"
#include "djui_panel_options.h"
#include "djui_panel_menu.h"
#include "djui_panel_confirm.h"
#include "pc/controller/controller_sdl.h"
#include "pc/pc_main.h"
#include "pc/update_checker.h"

extern ALIGNED8 u8 texture_coopdx_logo[];

struct DjuiText *gUpdateMessageText = NULL;
bool gDjuiPanelMainCreated = false;

static void djui_panel_main_quit_yes(UNUSED struct DjuiBase* caller) {
    game_exit();
}

static void djui_panel_main_quit(struct DjuiBase* caller) {
    djui_panel_confirm_create(caller,
                              DLANG(MAIN, QUIT_TITLE),
                              DLANG(MAIN, QUIT_CONFIRM),
                              djui_panel_main_quit_yes);
}

static bool djui_panel_main_on_initialize(struct DjuiBase *caller) {
    djui_base_set_enabled(caller, gGameInited);
    return false;
}

void djui_panel_main_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(configExCoopTheme ? "\\#ff0800\\SM\\#1be700\\64\\#00b3ff\\EX\n\\#ffef00\\COOP" : "", false);
    {
        struct DjuiBase* body = djui_three_panel_get_body(panel);
        {
            if (!configExCoopTheme) {
                struct DjuiImage* logo = djui_image_create(body, texture_coopdx_logo, 2048, 1024, G_IM_FMT_RGBA, G_IM_SIZ_32b);
                if (configDjuiThemeCenter) {
                    djui_base_set_size(&logo->base, 550, 275);
                } else {
                    djui_base_set_size(&logo->base, 480, 240);
                }
                djui_base_set_alignment(&logo->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
                djui_base_set_location_type(&logo->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
                djui_base_set_location(&logo->base, 0, -30);
            }

            struct DjuiButton* button1 = djui_button_create(body, DLANG(MAIN, HOST), DJUI_BUTTON_STYLE_NORMAL, djui_panel_host_create);
            if (!configExCoopTheme) { djui_base_set_location(&button1->base, 0, -30); }
            djui_cursor_input_controlled_center(&button1->base);

            struct DjuiButton* button2 = djui_button_create(body, DLANG(MAIN, JOIN), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_create);
            djui_base_set_enabled(&button2->base, gGameInited);
            djui_base_hook_on_changed(&button2->base, &gGameInited, sizeof(gGameInited), djui_panel_main_on_initialize);
            if (!configExCoopTheme) { djui_base_set_location(&button2->base, 0, -30); }
            struct DjuiButton* button3 = djui_button_create(body, DLANG(MAIN, OPTIONS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_options_create);
            if (!configExCoopTheme) { djui_base_set_location(&button3->base, 0, -30); }
            struct DjuiButton* button4 = djui_button_create(body, DLANG(MAIN, QUIT), DJUI_BUTTON_STYLE_BACK, djui_panel_main_quit);
            if (!configExCoopTheme) { djui_base_set_location(&button4->base, 0, -30); }
        }

        // due to the nature of three panels, create an empty djui rect here and
        // never use it :thumbsup:
        struct DjuiRect *emptyRect = djui_rect_create(&panel->base);
        djui_base_set_color(&emptyRect->base, 0, 0, 0, 0);

        struct DjuiFlowLayout *versionFlowLayout = djui_flow_layout_create(&panel->base);
        djui_flow_layout_set_flow_direction(versionFlowLayout, DJUI_FLOW_DIR_DOWN);
        djui_base_set_alignment(&versionFlowLayout->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_BOTTOM);
        djui_base_set_size_type(&versionFlowLayout->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&versionFlowLayout->base, 1.0f, 96.0f);
        djui_base_set_color(&versionFlowLayout->base, 0, 0, 0, 0);

        gUpdateMessageText = djui_text_create(&versionFlowLayout->base, DLANG(NOTIF, CHECKING_FOR_UPDATE));
        djui_base_set_size_type(&gUpdateMessageText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&gUpdateMessageText->base, 1.0f, 32.0f);
        djui_base_set_color(&gUpdateMessageText->base, 50, 50, 50, 255);
        djui_text_set_alignment(gUpdateMessageText, DJUI_HALIGN_CENTER, DJUI_VALIGN_BOTTOM);

        struct DjuiText *version = djui_text_create(
            &versionFlowLayout->base,
            #ifdef COMPILE_TIME
                get_version_with_build_date()
            #else
                get_version()
            #endif
        );
        djui_base_set_size_type(&version->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&version->base, 1.0f, 32.0f);
        djui_base_set_color(&version->base, 50, 50, 50, 255);
        djui_text_set_alignment(version, DJUI_HALIGN_CENTER, DJUI_VALIGN_BOTTOM);

        update_update_information(false);
    }

    djui_panel_add(caller, panel, NULL);
    gInteractableOverridePad = true;
    gDjuiPanelMainCreated = true;
}
