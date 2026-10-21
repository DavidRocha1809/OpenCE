/*
TOUCH_GAME.C

What the touch controls (port/linux/src/touch_input.c) need to know of the
game. The platform layer cannot see the game's types, so it asks here
(this file is compiled as the game's own sources are).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "input/input_abstraction.h"
#include "interface/ui_widget.h"
#include "game/players.h"
#include "game/local_players.h"
#include "units/units.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "interface/weapon_hud_interface_definition.h"
#include "interface/hud_definitions.h"
#include "tag_files/tag_files.h"
#include <string.h>

/* asks whether a cinematic is playing that A would skip
(player_control.c, player_control_action_test_check_reset_input_blob); tests
cinematic_globals first because the controller is read before the game exists
during input_initialize and shell_initialize, and game_initialize makes it
after the game time globals that game_in_progress reads (game.c) */
int touch_game_cinematic_skippable(void)
{
	return cinematic_globals && game_in_progress() && cinematic_in_progress() &&
		cinematic_can_be_skipped();
}

/* asks whether a cinematic is playing, skippable or not: the player has no
controls, so the on-screen touch controls hide (touch_input.c) */
int touch_game_cinematic_playing(void)
{
	return cinematic_globals && game_in_progress() && cinematic_in_progress();
}

/* asks whether a game is being played: not while the game starts, nor
between maps, nor at the main menu (whose ui.map is a game too), where the
on-screen touch controls do not show (touch_input.c); cinematic_globals
first, as touch_game_cinematic_skippable has it */
int touch_game_playing(void)
{
	return cinematic_globals && game_in_progress() && !main_menu_is_active();
}

/* (touch_input.c's TOUCH_BUTTONS and the overlay count 16 buttons) */
typedef char verify_touch_buttons[NUMBER_OF_GAMEPAD_BUTTONS == 16 ? 1 : -1];

/* the game control on each of player 1's 16 controller buttons, as the
gamepad's buttons are numbered (input.h), -1 for none: the touch controls
name their buttons by them, so a profile's own mapping shows */
void touch_game_button_controls(int *controls)
{
	short buttons[NUMBER_OF_GAMEPAD_BUTTONS];
	short index;

	input_abstraction_port_button_controls(0, buttons);
	for (index = 0; index < NUMBER_OF_GAMEPAD_BUTTONS; index++)
	{
		controls[index] = buttons[index];
	}
}

/* Compact type IDs, transmitted as three integers (two slots, active slot).
 * The native engine decides the icon identity from the equipped weapon tag;
 * no guesses about which gun is actually held on Android. */
static int touch_weapon_icon(const char *name)
{
    if (!name) return 0;
    if (strstr(name, "assault rifle")) return 1;
    if (strstr(name, "pistol") && !strstr(name, "plasma")) return 2;
    if (strstr(name, "plasma pistol")) return 3;
    if (strstr(name, "plasma rifle")) return 4;
    if (strstr(name, "needler")) return 5;
    if (strstr(name, "shotgun")) return 6;
    if (strstr(name, "sniper rifle")) return 7;
    if (strstr(name, "rocket launcher")) return 8;
    if (strstr(name, "flamethrower")) return 9;
    if (strstr(name, "fuel rod")) return 10;
    return 11; /* unknown/modded weapon: neutral rifle silhouette */
}

void touch_game_weapon_slots(int slots[16], int requested)
{
    long unit_index;
    long player_index;
    struct player_datum *player;
    struct unit_datum *unit;
    int i;
    for (i = 0; i < 16; i++) slots[i] = -1;
    /* Extra weapon occupancy mask: bit 0 = third slot, bit 1 = fourth.
     * Bit 8 tells the Android modern HUD that Halo is showing a weapon
     * pickup/swap prompt, so it can replace the Xbox X glyph with the
     * touch reload pictogram without changing the localized message. */
    slots[9] = 0;
    if (!touch_game_playing()) return;
    player_index = local_player_get_player_index(0);
    player = player_index == NONE ? NULL : player_get(player_index);
    if (player && (player->action_result == _player_action_result_pickup_weapon ||
        player->action_result == _player_action_result_swap_for_weapon ||
        player->action_result == _player_action_result_add_weapon_to_inventory))
    {
        slots[9] |= 1 << 8;
    }
    unit_index = player_control_get_unit_index(0);
    if (unit_index == NONE) return;
    unit = unit_get(unit_index);
    if (!unit) return;
    for (i = 0; i < 2; i++)
    {
        long weapon_index = unit_inventory_get_weapon(unit_index, (short)i);
        if (weapon_index != NONE)
        {
            struct weapon_datum *weapon = weapon_try_and_get(weapon_index);
            if (weapon)
            {
                struct weapon_definition *definition = weapon_definition_get(weapon->definition_index);
                long hud_index = definition->weapon.interface_definition.hud_interface.index;
                slots[i] = touch_weapon_icon(tag_get_name(weapon->definition_index));
                if (hud_index != NONE)
                {
                    /* Original messaging icon metadata, rather than an invented sprite ID.
                     * The bitmap itself belongs to hud_globals->messaging.messaging_icons.
                     * slots[3..4]: sequence, [5..6]: original RGBA packed color,
                     * [7..8]: original icon flags. */
                    const struct weapon_hud_interface_definition *hud =
                        (const struct weapon_hud_interface_definition *)tag_get('wphi', hud_index);
                    if (hud && hud->messaging_icon.sequence_index != NONE)
                    {
                        slots[3 + i] = hud->messaging_icon.sequence_index;
                        slots[5 + i] = (int)hud->messaging_icon.color;
                        slots[7 + i] = (int)hud->messaging_icon.flags;
                    }
                }
            }
        }
    }
    /* Respawn profiles and scripts can fill inventory slots 2 and 3 even
     * when ordinary pickup is limited to two weapons. */
    for (i = 2; i < 4; i++)
    {
        long extra_index = unit_inventory_get_weapon(unit_index, (short)i);
        if (extra_index != NONE)
        {
            struct weapon_datum *extra = weapon_try_and_get(extra_index);
            if (extra)
            {
                /* An inventory slot is occupied even if its HUD tag has no icon. */
                slots[9] |= 1 << (i - 2);
                struct weapon_definition *definition = weapon_definition_get(extra->definition_index);
                if (definition)
                {
                    long hud_index = definition->weapon.interface_definition.hud_interface.index;
                    if (hud_index != NONE)
                    {
                        const struct weapon_hud_interface_definition *hud =
                            (const struct weapon_hud_interface_definition *)tag_get('wphi', hud_index);
                        if (hud && hud->messaging_icon.sequence_index != NONE)
                        {
                            slots[10 + i - 2] = hud->messaging_icon.sequence_index;
                            slots[12 + i - 2] = (int)hud->messaging_icon.color;
                            slots[14 + i - 2] = (int)hud->messaging_icon.flags;
                        }
                    }
                }
            }
        }
    }
    if (unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index < 4)
        slots[2] = unit->unit.current_weapon_index;
    if (requested >= 0 && requested < 4 &&
        (requested < 2 ? slots[requested] >= 0 :
            (slots[9] & (1 << (requested - 2))) != 0) &&
        unit->unit.current_weapon_index != requested)
        player_control_set_desired_weapon(unit_index, (short)requested);
}
