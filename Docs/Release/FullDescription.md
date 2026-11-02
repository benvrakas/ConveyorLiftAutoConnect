Place a blueprint above or below another one, and **Conveyor Lift Auto-Connect Plus** connects the lined-up vertical conveyor endpoints with ordinary vanilla Conveyor Lifts: floor holes and the vertical ports of splitters and mergers. Stacked factory floors connect themselves.

It is based on [Vertical Conveyor Auto-Connect](https://ficsit.app/mod/VerticalConveyorAutoConnect) by ZeeOcho (version 1.0.4), with two problems fixed:

- **No more load crashes from stacked blueprints.** Each lift the original built at a seam stayed in a separate conveyor group from the blueprint that was already there. The save then held two conveyor chains on one connected belt, which can crash the game the next time the save loads ([details](https://github.com/ZeeOcho/VerticalConveyorAutoConnect/issues/4)). Here the new lift joins the existing side's group, the same way the game regroups belts when you place a splitter on one.
- **Fast placement.** The original built a full lift preview for every open vertical endpoint, twice per placement, so the freeze grew with the number of lifts (several seconds for a 32-lift blueprint). Lift previews are now made only for endpoints that actually connect, and unchanged previews are not rebuilt every frame: placing a 16-lift blueprint costs the mod about 25 ms.

**Do not install it together with Vertical Conveyor Auto-Connect.** If both are installed, this mod stays inactive and writes an error to `FactoryGame.log`.

## How it works

- Floor Hole ↔ Floor Hole, and Floor Hole ↔ the vertical lift ports of Splitters, Mergers, Smart and Programmable Splitters and Priority Mergers (and modded attachments that use the standard vertical ports).
- The lift tier comes from the adjacent lift on the blueprint side, otherwise from the structure below or above. If neither side has a lift, no bridge is built.
- Every bridge is a real vanilla Conveyor Lift that must pass vanilla placement checks, including soft clearance. The mod adds no length or clearance rules of its own.
- The first conveyor endpoint in a column ends the search, so it never tunnels through a nearer endpoint to reach a farther one.

## Settings

In the Mods menu (or Satisfactory Mod Manager), for the player placing the blueprint:

- **Auto-connect lifts**: turn generated lifts off without uninstalling.
- **Lift tier**: copy the tier from the blueprint side's lift first (default), from the faster of the two lifts, or from the world side's lift first.
- **Longest lift (m)**: skip lifts longer than this; 0 leaves the length to vanilla.
- **Detailed log**: write why each endpoint did or didn't connect to `FactoryGame.log`, for bug reports.

**Tip:** after editing a blueprint in a way that changes the conveyors at a future seam, dismantle and rebuild the lifts, floor holes and splitters right at that seam before saving the blueprint. Satisfactory can keep stale connection state that looks right but no longer connects.

## Switching from Vertical Conveyor Auto-Connect

Remove the original and install this mod; lifts already built stay as they are. Saves made with the original while stacking blueprints may already contain split chains, which this mod does not repair on load.

## Compatibility

- Satisfactory 1.2, SML 3.12. Windows client.
- Multiplayer: everyone needs this mod. Dedicated servers are untested.
- Removing the mod leaves the generated lifts as normal Conveyor Lifts.

## Credits and source

Original mod, design and most of the code: **ZeeOcho** ([source](https://github.com/ZeeOcho/VerticalConveyorAutoConnect)). Chain fix, speed-up and maintenance: benvrakas. Licensed GPL-3.0; source on [GitHub](https://github.com/benvrakas/ConveyorLiftAutoConnect).
