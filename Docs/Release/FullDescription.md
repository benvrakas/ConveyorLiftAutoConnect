**This is a temporary, unofficial fork** of [Conveyor Lift Auto-Connect](https://ficsit.app/mod/VerticalConveyorAutoConnect) by ZeeOcho (version 1.0.4). It exists so a few players can use two fixes while they are reviewed upstream. Once the original mod ships them, use the original again; this page will then be retired.

**Do not install both mods.** If the original is also installed, this fork stays inactive and writes an error to `FactoryGame.log`.

## What the mod does

When you place a blueprint above or below another one, it connects lined-up vertical conveyor endpoints (floor holes and vertical splitter/merger ports) with ordinary vanilla Conveyor Lifts. All features of the original are unchanged; see its page for details.

## What this fork changes

- **Fixes a load crash with stacked blueprints** ([issue #4](https://github.com/ZeeOcho/VerticalConveyorAutoConnect/issues/4)). Each lift the mod built at a seam stayed in a separate conveyor group from the blueprint that was already there. The save then held two conveyor chains on one connected belt, which can crash the game the next time the save loads. The new lift now joins the existing side's group, the same way the game regroups belts when you place a splitter on one.
- **Faster blueprint placement.** The original built a full lift preview for every open vertical endpoint, twice per placement, so the freeze grew with the number of lifts. Lift previews are now made only for endpoints that actually connect, and unchanged previews are not rebuilt every frame.

Saves made with the original mod while stacking blueprints may already contain split chains. This fork does not repair those on load.

## Compatibility

- Same features and settings as Vertical Conveyor Auto-Connect 1.0.4.
- Windows client. Multiplayer: everyone needs this fork instead of the original; dedicated servers are untested.
- Removing the mod leaves the generated lifts as normal Conveyor Lifts.

## Credits and source

Original mod and almost all code: **ZeeOcho** ([source](https://github.com/ZeeOcho/VerticalConveyorAutoConnect)). Fork changes: benvrakas. Licensed GPL-3.0; this fork's source is [here](https://github.com/benvrakas/VerticalConveyorAutoConnect/tree/release/conveyer-lift-autoconnect).
