#pragma once
#include "djui.h"

#define THEMES_DIRECTORY "themes"
#define MAX_DJUI_THEME_NAME_LEN 64
#define MAX_DJUI_THEMES 64
#define DJUI_THEME_CENTERED_WIDTH 1.3f
#define DJUI_THEME_CENTERED_HEIGHT 0.921f

enum DjuiBuiltinThemes {
    DJUI_THEME_LIGHT,
    DJUI_THEME_DARK,
    DJUI_THEME_COUNT
};

enum DjuiThemeElements {
    DJUI_THEME_ELEMENT_PRIMARY,
    DJUI_THEME_ELEMENT_PRIMARY_HOVER,
    DJUI_THEME_ELEMENT_PRIMARY_DOWN,
    DJUI_THEME_ELEMENT_PRIMARY_DISABLED,
    DJUI_THEME_ELEMENT_PRIMARY_TEXT,
    DJUI_THEME_ELEMENT_PRIMARY_TEXT_DISABLED,

    DJUI_THEME_ELEMENT_PRIMARY_BORDER,
    DJUI_THEME_ELEMENT_PRIMARY_BORDER_HOVER,
    DJUI_THEME_ELEMENT_PRIMARY_BORDER_DOWN,
    DJUI_THEME_ELEMENT_PRIMARY_BORDER_DISABLED,

    DJUI_THEME_ELEMENT_SECONDARY,
    DJUI_THEME_ELEMENT_SECONDARY_HOVER,
    DJUI_THEME_ELEMENT_SECONDARY_DOWN,
    DJUI_THEME_ELEMENT_SECONDARY_DISABLED,
    DJUI_THEME_ELEMENT_SECONDARY_TEXT,
    DJUI_THEME_ELEMENT_SECONDARY_TEXT_DISABLED,

    DJUI_THEME_ELEMENT_SECONDARY_BORDER,
    DJUI_THEME_ELEMENT_SECONDARY_BORDER_HOVER,
    DJUI_THEME_ELEMENT_SECONDARY_BORDER_DOWN,
    DJUI_THEME_ELEMENT_SECONDARY_BORDER_DISABLED,

    DJUI_THEME_ELEMENT_INPUTBOX,
    DJUI_THEME_ELEMENT_INPUTBOX_HOVER,
    DJUI_THEME_ELEMENT_INPUTBOX_DOWN,
    DJUI_THEME_ELEMENT_INPUTBOX_DISABLED,
    DJUI_THEME_ELEMENT_INPUTBOX_TEXT,
    DJUI_THEME_ELEMENT_INPUTBOX_TEXT_PLACEHOLDER,

    DJUI_THEME_ELEMENT_INPUTBOX_BORDER,
    DJUI_THEME_ELEMENT_INPUTBOX_BORDER_HOVER,
    DJUI_THEME_ELEMENT_INPUTBOX_BORDER_DOWN,
    DJUI_THEME_ELEMENT_INPUTBOX_BORDER_DISABLED,

    DJUI_THEME_ELEMENT_CHECKBOX,
    DJUI_THEME_ELEMENT_CHECKBOX_HOVER,
    DJUI_THEME_ELEMENT_CHECKBOX_DOWN,
    DJUI_THEME_ELEMENT_CHECKBOX_DISABLED,

    DJUI_THEME_ELEMENT_CHECKBOX_BORDER,
    DJUI_THEME_ELEMENT_CHECKBOX_BORDER_HOVER,
    DJUI_THEME_ELEMENT_CHECKBOX_BORDER_DOWN,
    DJUI_THEME_ELEMENT_CHECKBOX_BORDER_DISABLED,

    DJUI_THEME_ELEMENT_SLIDER,
    DJUI_THEME_ELEMENT_SLIDER_HOVER,
    DJUI_THEME_ELEMENT_SLIDER_DOWN,
    DJUI_THEME_ELEMENT_SLIDER_DISABLED,

    DJUI_THEME_ELEMENT_SLIDER_BORDER,
    DJUI_THEME_ELEMENT_SLIDER_BORDER_HOVER,
    DJUI_THEME_ELEMENT_SLIDER_BORDER_DOWN,
    DJUI_THEME_ELEMENT_SLIDER_BORDER_DISABLED,

    DJUI_THEME_ELEMENT_TEXT,
    DJUI_THEME_ELEMENT_TEXT_DISABLED,

    DJUI_THEME_ELEMENT_SELECTIONBOX_IMAGE,
    DJUI_THEME_ELEMENT_SELECTIONBOX_IMAGE_DISABLED,

    DJUI_THEME_ELEMENT_THREE_PANEL,
    DJUI_THEME_ELEMENT_THREE_PANEL_BORDER,

    DJUI_THEME_ELEMENT_PANEL_HEADER_COLOR,

    DJUI_THEME_ELEMENT_COUNT,
};

struct DjuiDeprecatedTheme {
    PROPERTY(textColor,             djui_theme_get_text_color,               NULL);
    PROPERTY(defaultRectColor,      djui_theme_get_default_rect_color,       NULL);
    PROPERTY(cursorDownRectColor,   djui_theme_get_cursor_down_rect_color,   NULL);
    PROPERTY(hoveredRectColor,      djui_theme_get_hovered_rect_color,       NULL);
    PROPERTY(defaultBorderColor,    djui_theme_get_default_border_color,     NULL);
    PROPERTY(cursorDownBorderColor, djui_theme_get_cursor_down_border_color, NULL);
    PROPERTY(hoveredBorderColor,    djui_theme_get_hovered_border_color,     NULL);
    PROPERTY(rectColor,             djui_theme_get_rect_color,               NULL);
    PROPERTY(borderColor,           djui_theme_get_border_color,             NULL);
    PROPERTY(hudFontHeader,         djui_theme_get_hud_font_header,          NULL);
    bool unused;
};

struct DjuiTheme {
    char name[MAX_DJUI_THEME_NAME_LEN];
    struct DjuiColor elements[DJUI_THEME_ELEMENT_COUNT];
    unsigned int headerFont;
    bool useRainbowColor;
    bool gradients;
    struct DjuiDeprecatedTheme interactables;
    struct DjuiDeprecatedTheme threePanels;
    struct DjuiDeprecatedTheme panels;
};

extern struct DjuiTheme* gDjuiThemes[MAX_DJUI_THEMES];
extern struct DjuiTheme gDjuiThemeDark;

struct DjuiColor djui_theme_shade_color(struct DjuiColor color, f32 mult);
bool djui_theme_compare_theme_elements(struct DjuiTheme *themeOne, struct DjuiTheme *themeTwo);
bool djui_themes_save_current(bool setThemeArray);
bool djui_themes_save(struct DjuiTheme* theme, bool setThemeArray);
void djui_themes_load(void);
void djui_theme_delete(struct DjuiTheme* theme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_TEXT` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_text_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_PRIMARY` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_default_rect_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_PRIMARY_DOWN` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_cursor_down_rect_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_PRIMARY_HOVER` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_hovered_rect_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_PRIMARY_BORDER` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_default_border_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_PRIMARY_BORDER_DOWN` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_cursor_down_border_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_PRIMARY_BORDER_HOVER` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_hovered_border_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_THREE_PANEL` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_rect_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets the color of the `DJUI_THEME_ELEMENT_THREE_PANEL_BORDER` of the current menu theme|descriptionEnd| */
struct DjuiColor *djui_theme_get_border_color(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
/* |description|Gets whether header font of the theme is `FONT_HUD`|descriptionEnd| */
bool djui_theme_get_hud_font_header(UNUSED struct DjuiDeprecatedTheme *deprecatedTheme);
