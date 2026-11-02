# Conveyor Lift Auto-Connect Plus

Automatically bridges aligned vertical conveyor endpoints across blueprint
boundaries with vanilla Conveyor Lifts.

The mod is intended for vertically stacked blueprints where a conveyor path
would otherwise need to be reconnected manually after every placement.

Mod reference `ConveyorLiftAutoConnect`. Based on
[Vertical Conveyor Auto-Connect](https://github.com/ZeeOcho/VerticalConveyorAutoConnect)
1.0.4 by ZeeOcho, whose history this repository keeps. Changes since then:

- generated bridges and direct seams join the conveyor group of the side that
  was already in the world, so saves no longer contain split conveyor chains
  that crash the next load
  ([upstream issue #4](https://github.com/ZeeOcho/VerticalConveyorAutoConnect/issues/4));
- bridge holograms are spawned only for endpoints that have a target, and
  unchanged preview bridges are not reconfigured every frame, which removes the
  placement freeze that grew with the number of lifts;
- the mod stays inactive when `VerticalConveyorAutoConnect` is also installed.

These changes were written with Anthropic Claude (Claude Code) and tested in
game by benvrakas.

## Notes from the original author (ZeeOcho)

Disclaimer: Generative AI (ChatGPT) was used extensively by me to generate pretty
much all code, documentation and artwork. I still in-game-tested the functionality
to make sure it works correctly. I also made my best efforts to check all generated
text and code (I am not a C++ developer).

I neither have much time nor any knowledge of Satisfactory Modding. And since I expect
CSS to eventually implement this kind of feature themselves, I decided to throw AI at it.
That way, I will still have this functionality until that happens.

What you also should know: attachment connections are still a little finnicky even in
vanilla itself. For example: when you are trying to build a lift between a floor hole and
an open vertical attachment (*-splitter, *-merger), you can only start at the attachment side,
not at the floor hole (the lift will not snap to the attachment). This means:
1. With vanilla alone, we cannot auto-connect (or manually build) between two open attachments.
   If another mod extends the actual Conveyor Lift hologram to support that placement, this mod
   can use the additional capability automatically.
2. Mind this when building blueprints. Be especially careful when deleting connections.
   Double-check the remaining connections to make sure they still transport items. You might
   need to re-build them.

Everything below this line is AI generated.
vvv

## Supported connections

| Blueprint/world boundary endpoints | Result |
|---|---|
| Conveyor Floor Hole ↔ Conveyor Floor Hole | Supported |
| Conveyor Floor Hole ↔ compatible Splitter/Merger lift port | Supported |
| Compatible Splitter/Merger lift port ↔ Conveyor Floor Hole | Supported |
| Compatible Splitter/Merger lift port ↔ compatible Splitter/Merger lift port | Capability-dependent |

"Compatible Splitter/Merger" is capability-based rather than a hard-coded class
list. Normal Splitters/Mergers, Smart and Programmable Splitters, and Priority
Mergers are supported. Modded or future variants are supported automatically
when they use Satisfactory's standard `AFGBuildableConveyorAttachment` vertical
lift-port contract.

Attachment ↔ attachment bridges are attempted only when the generated, real
Conveyor Lift hologram reports that it can connect to the second attachment.
Vanilla currently rejects that capability, so behavior without another mod is
unchanged. A runtime extension such as `VerticalLogisticsQoL` can enable it
without a hard dependency or installation check in this mod.

For blueprint-owned attachments, the capability check uses the resolved preview
position rather than blueprint-local component geometry. This compatibility path
has been regression-tested with a Satisfactory 1.2-compatible build of
`VerticalLogisticsQoL` across all Splitter/Merger pairings and both transport
directions.

## Lift tier selection

When a bridge needs a Conveyor Lift tier:

1. the adjacent lift on the **blueprint side** wins;
2. otherwise the adjacent lift on the **world/target side** is used;
3. if neither side provides a lift tier, no bridge is created.

This lets a blueprint define its preferred lift tier while still allowing a bare
blueprint endpoint to inherit the tier of the structure it is stacked onto.

## Placement and collision behavior

The generated bridge uses a real vanilla Conveyor Lift hologram for placement
validation.

- Soft-clearance/yellow placements remain constructible when vanilla allows them.
- Hard-invalid placements are rejected when vanilla rejects them.
- The mod does not add guessed maximum heights, distances, clearance rules, or
  angular tolerances.

Initial target discovery examines the complete vertical column through each
open endpoint. This is only candidate discovery: every bridge is still configured
as a real vanilla Conveyor Lift hologram and accepted only when vanilla reports
that it can construct. The mod therefore adds no independent lift-length rule.

The first physical vertical conveyor endpoint in a column terminates the search.
The mod never skips through an occupied, unsupported, claimed, or incompatible
endpoint to connect to a farther one.

## General hints

### Rebuild seam connections after topology-changing blueprint edits

Satisfactory can preserve serialized conveyor connection state on surviving
buildables when part of an already-connected conveyor graph is removed from a
blueprint. A seam-adjacent Conveyor Lift, Conveyor Floor Hole, Splitter, Merger,
or variant can therefore still look geometrically correct while its saved
connection/snap state no longer matches the edited blueprint.

**General rule:** after an edit changes the conveyor topology at a future
blueprint seam, dismantle and rebuild the conveyor connections immediately
adjacent to that seam before saving the final blueprint.

This is especially relevant to a "master blueprint" workflow:

1. build and save one fully connected combined blueprint;
2. save copies for the upper/lower variants;
3. delete the opposite part from each copy;
4. rebuild the seam-local conveyor connections in each resulting blueprint;
5. save the final variants.

You normally do **not** need to rebuild the entire conveyor network. The
seam-local Lift / Floor Hole / Splitter / Merger relationships are the important
ones.

This is a general Satisfactory blueprint-editing precaution rather than a
requirement invented by this mod: visual alignment alone does not guarantee that
vanilla still considers two conveyor elements semantically connected.

## Compatibility

- Satisfactory 1.2
- Satisfactory Mod Loader 3.12.x
- No custom buildables or items are added.
- Capability-compatible conveyor-attachment variants from other mods may work
  automatically; non-standard attachment implementations fail closed.
- Attachment ↔ attachment placement is capability-driven. The mod calls the
  live Conveyor Lift hologram API, so compatible runtime extensions can enable
  it without being referenced as dependencies.

### Multiplayer / dedicated servers

**Multiplayer has not been tested and is not currently advertised as supported.**

The implementation uses Satisfactory/SML's blueprint construction-message path
and avoids custom replicated actors or RPCs, so the architecture is intended to
be multiplayer-compatible. However, no host/client or dedicated-server test has
been performed. Users should therefore treat multiplayer behavior as unverified.

By default, SML requires the same mod version on both sides. Dedicated-server
release targets should not be advertised as supported unless they are tested in
a future release.

## Troubleshooting

If an endpoint looks aligned but does not auto-connect:

1. verify that vanilla can manually continue the conveyor through that endpoint;
2. for Floor Holes, verify the existing continuation is actually snapped through
   the hole rather than only geometrically aligned;
3. after splitting or heavily editing a previously connected blueprint, rebuild
   the seam-local conveyor connections and save it again;
4. compare the same endpoint pair with a manually configured vanilla Conveyor
   Lift if an unusually long span is rejected.

For detailed bug reports, include the Satisfactory version, SML version, mod
version, endpoint topology, intended flow direction, and a game log.

## Development

See:

- [`DEVELOPMENT.md`](DEVELOPMENT.md) for architecture and invariants;
- [`TESTING.md`](TESTING.md) for the regression matrix;
- [`CHANGELOG.md`](CHANGELOG.md) for release history;
- [`PUBLISHING.md`](PUBLISHING.md) for the GitHub/ficsit.app release checklist.

## License

This project is licensed under the GNU General Public License v3.0. See
[`LICENSE`](LICENSE).
