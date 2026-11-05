Place a blueprint above or below another one, and **Conveyor Lift Auto-Connect Plus** connects the lined-up vertical conveyor endpoints with ordinary vanilla Conveyor Lifts: floor holes and the vertical ports of splitters and mergers. Stacked factory floors connect themselves.

- **Saves stay safe.** Every generated lift joins the conveyor chain of the floor it connects to, the same way the game regroups belts when you place a splitter on one, so stacked blueprints don't leave split chains that can crash the next load.
- **Fast placement.** Lift previews are made only for endpoints that actually connect, and unchanged previews are not rebuilt every frame: placing a 16-lift blueprint costs the mod about 25 ms.
- **Works with everything already built.** Blueprints from earlier sessions connect too, even though the game rounds positions slightly when it saves.
- **Multiplayer.** Joining players can place large blueprints onto other blueprints.

**Do not install it together with Vertical Conveyor Auto-Connect.** If both are installed, this mod stays inactive and writes an error to `FactoryGame.log`.

## How it works

- Floor Hole ↔ Floor Hole, and Floor Hole ↔ the vertical lift ports of Splitters, Mergers, Smart and Programmable Splitters and Priority Mergers (and modded attachments that use the standard vertical ports).
- The lift tier comes from the lift next to one of the two endpoints (see Settings). If neither side has a lift, no lift is generated.
- Every generated lift is a real vanilla Conveyor Lift that must pass vanilla placement checks, including soft clearance. The mod adds no length or clearance rules of its own.
- The first conveyor endpoint above or below ends the search, so a lift never passes through a nearer endpoint to reach a farther one.

## Settings

In the Mods menu (or Satisfactory Mod Manager), for the player placing the blueprint:

- **Auto-connect lifts**: turn generated lifts off without uninstalling.
- **Lift tier**: copy the tier from the blueprint side's lift first (default), from the faster of the two lifts, or from the world side's lift first.
- **Longest lift (m)**: skip lifts longer than this; 0 leaves the length to vanilla.
- **Detailed log**: write why each endpoint did or didn't connect to `FactoryGame.log`, for bug reports.

**Tip:** after editing a blueprint in a way that changes the conveyors at a future seam, dismantle and rebuild the lifts, floor holes and splitters right at that seam before saving the blueprint. Satisfactory can keep stale connection state that looks right but no longer connects.

## Switching from Vertical Conveyor Auto-Connect

Remove it and install this mod; lifts already built stay as they are. Saves made with it while stacking blueprints may already contain split chains, which this mod does not repair on load.

## Compatibility

- Satisfactory 1.2, SML 3.12. Windows client, and Windows and Linux dedicated servers.
- Multiplayer: everyone needs this mod. Tested on player-hosted games; dedicated servers are new in 1.1.7 and not yet tested on a running server.
- Removing the mod leaves the generated lifts as normal Conveyor Lifts.

## Source

By benvrakas. Licensed GPL-3.0; source on [GitHub](https://github.com/benvrakas/ConveyorLiftAutoConnect). Originally based on [Vertical Conveyor Auto-Connect](https://ficsit.app/mod/VerticalConveyorAutoConnect) by ZeeOcho; rewritten since 1.1.0.
