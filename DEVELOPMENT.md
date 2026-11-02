# Development notes

This document describes how the mod is built and the invariants it relies on. Most of the vanilla behavior below was
measured by ZeeOcho for Vertical Conveyor Auto-Connect; the code was restructured for Conveyor Lift Auto-Connect Plus
2.x without changing that behavior.

## Source map

| File | Responsibility |
|---|---|
| `ConveyorLiftAutoConnectModule` | Registers the manager on every blueprint hologram, drops the transient InvalidFloor disqualifier of generated bridges, stays inactive next to the original mod. |
| `LiftEndpoint` | `FLiftEndpoint`: a floor-hole side or an attachment lift port, with its location, facing, continuation, adjacent lift and implied flow. Shared helpers for floor-hole snapping and directions. |
| `SeamSearch` | Pure search: column matching, nearest-plane candidates, no-tunneling, flow resolution, direct-link feasibility. |
| `LiftBridgeHologram` | Spawns and reuses child lift holograms and puts them in the snapped state manual vanilla placement creates. |
| `SeamLinker` | Construction: checks, links and verifies direct seams and generated lifts through `FSeamTransaction`, then regroups conveyor buckets. |
| `SeamAudit` | Verbose-only next-tick check of links, buckets and chain actors. |
| `LiftAutoConnectConfig` | The settings page (`UModConfiguration`) and the root game instance module that registers it. Properties are converted to SML's Blueprint widget classes before registration; the lift tier uses an enum dropdown whose names come from `SetEnumDisplayNameFn` in shipping builds. |
| `LiftAutoConnectManager` | The `FGBlueprintOpenConnectionManagerBase` implementation: one `FSeamState` per open blueprint endpoint, preview updates, snapping, the construct message and construction. |

Private game members are reached only by the classes listed in `Config/AccessTransformers.ini`, each for the reason
given there.

## Reproduce vanilla semantics

The mod creates the state manual Conveyor Lift placement would create; it is not an independent connection system.
A real `AFGConveyorLiftHologram` decides validity: after configuration the normal vanilla validation runs and the
bridge counts only when `CanConstruct()` is true. There are no mod-defined distance, height, clearance or angle limits.

Endpoint positions count as equal within 1 mm (`PositionTolerance` in `SeamSearch.cpp`): column match, touching
seams, level ports and the nearest plane. Saves store positions in single precision, so buildables loaded from a save
sit up to about 0.03 cm (half a float step at the map's largest coordinates) from where they were built, while
blueprint previews and newly built buildables are exact. Exact comparison made stacking onto anything loaded from a
save fail at most map positions; measured offsets were 0.002 to 0.011 cm. Distinct endpoints are tens of centimetres
apart, so 1 mm cannot pair the wrong ones. Normals still use the engine's default `Equals` tolerance.

The one correction is dropping `UFGCDInvalidFloor` from holograms tagged as synthetic bridges whose parent is a
blueprint hologram: the blueprint placement context makes the child inherit it transiently, and a finished lift needs
no floor. Soft-clearance disqualifiers are never touched.

## Endpoints

- A **floor hole** is an `AFGBuildablePassthrough` whose connection class is a factory connection. Its top side faces
  up, its bottom side down. A side is open when nothing is snapped to it; its continuation is the connection snapped
  to the opposite side.
- An **attachment** is an `AFGBuildableConveyorAttachment` with two distinct vertical components named
  `AFGConveyorAttachmentHologram::mLiftConnection_Bottom` and `mLiftConnection_Top`. Measured vanilla placement uses
  the `mLiftConnection_Bottom` component for a lift going up from the attachment and `mLiftConnection_Top` for one
  going down, so the first faces up and the second down. A port is open when unconnected; its continuation is the
  port itself.

There is no class allow-list: modded attachments with the standard lift ports work. Attachment-to-attachment lifts
are attempted only through the live hologram's `CanConnectToConnection()` (see below).

Ownership (blueprint or world), representation (preview transform or constructed actor), transport direction and
vanilla placement order are independent. `FLiftEndpoint::Preview` covers representation only.

## Search

Each preview update gathers world endpoints once from the hologram's nearby-actor feed plus one buildable-subsystem
query over the full-height prism above and below all source endpoints. The overlap feed is only vanilla's broadphase
and its reach depends on where an endpoint sits in the blueprint bounds; the prism query removes that bias. If the
subsystem is missing, only the overlap feed is used. Length is left to the lift hologram, so if the game ever adds a
maximum lift height the mod inherits it.

For each open source endpoint (`SeamSearch::FindNearestPlane`):

1. Keep world endpoints in the same column (X and Y nearly equal).
2. An attachment port must face the other endpoint unless both are level.
3. Touching endpoints (`Delta.IsNearlyZero()`) with opposing outward normals form a **direct** seam. Otherwise the
   source must face the target and the target the source: a **span**.
4. A span may not cross another blueprint endpoint of a different buildable in the same column (strictly between the
   two heights). Identity, not position, excludes the source's own buildable, because preview transforms move.
5. Only candidates on the nearest plane are tried, occupied ones included. If none of them works, the state gets no
   target: a lift never passes through a closer endpoint.

Candidates are then filtered in order: target open, not claimed by an earlier state, a lift tier exists (blueprint
side's adjacent lift first, then the target side's), and the direction checks below. The first one left wins.

## Flow

Each endpoint with a continuation implies a flow (`FLiftEndpoint::GetImpliedFlow`): at an upward-facing endpoint the
lift's lower end meets the continuation and needs the opposite direction, at a downward-facing one the lift's lower
end needs the continuation's direction; a lower end that is an input means items flow up. Both implied flows must
agree, and at least one must exist.

The flow is decided during preview and sent with the construct message. Construction never rediscovers it from the
new blueprint actors, which have not run `BeginPlay()` yet.

## Bridge hologram state

`FLiftBridgeHologram::Configure` places the lift from its start to its end with the heading of the adjacent lift
(blueprint side first), else the floor hole's valid attach direction, else the blueprint buildable's forward vector.

Vanilla starts a lift at the attachment when one end is an attachment, otherwise at the transport input. Measured
attachment-first state:

- slot 0 is the attachment port, slot 1 the floor hole (or the second attachment);
- `mSnappedPassthroughs = [nullptr, floorHole]`, `mSnappedConnectionComponents = [port, continuation across the hole]`;
- attachment as transport input: port `FCD_OUTPUT`, `mArrowDirection = FCD_OUTPUT`, `mIsReversed = false`;
- attachment as transport output: port `FCD_INPUT`, `mArrowDirection = FCD_INPUT`, `mIsReversed = true`;
- `UpdateConnectionDirections()` and the `OnRep_*` calls reset this for a synthetic lift, so the lift's own
  connection directions, arrow and reversal are applied again afterwards.

Floor-hole-only lifts keep the transport-input-first state: both passthroughs, no snapped components, arrow input.

The second end of an attachment-first lift is accepted through `CanConnectToConnection()` on the real hologram.
Vanilla rejects attachment-to-attachment; a runtime extension of that method (for example VerticalLogisticsQoL)
enables it without a dependency. The method is called during preview, so both components must sit where the lift
will end: the lift's slot-1 component is moved to the end, the hologram's duplicate of a blueprint port is preferred,
and only without one is the blueprint-world port moved to its preview transform. Everything is restored right after
the call. Components moved this way stay movable for the hologram's lifetime.

A preview configuration is reused while the bridge, recipe, target, flow and both endpoint transforms are unchanged.
Final construction always configures again.

Child holograms are registered by name on the parent and `Destroy()` does not release the name, so each state and
recipe keeps one child for the parent's lifetime (named with a hash of the recipe path) and unused ones are disabled.

## Construction

Construction runs before the blueprint's new actors receive `BeginPlay()`, so the mod links its seams itself.
Every change goes through an `FSeamTransaction`; on any failure it is undone newest first, and a generated lift is
also detached from anything vanilla construction linked it to and then destroyed.

**Generated lift** (`PlanBridge`, `LinkBridge`):

1. Before construction, each end must be a conveyor floor hole with a free side (or an attachment port), and its
   continuation must be unlinked, vertical (attachments), a lift connection (floor holes) and compatible in
   direction. The blueprint floor hole's continuation must be the one remapped from the blueprint, and a blueprint
   hole that had none must still have none. Both ends may not share a continuation.
2. A blueprint attachment port is first set to its saved direction (`mSavedDirections`, indexed after
   `SortComponentList`), which must equal the previewed role; before `BeginPlay` a port can report `FCD_ANY` or a
   stale value. World attachments are not modified.
3. After construction, input and output are told apart by the lift's connection directions (attachment-first
   placement can swap the slots), and the lift's flow must match the preview.
4. For both ends, before either links: set an `FCD_ANY` continuation to the required direction and snap the floor
   hole side to the lift. Then link each end, refresh the lift's passthroughs and verify every link, direction and
   floor hole side.

**Direct seam** (`LinkDirect`): the endpoints must still touch, both continuations must be unlinked (or already
linked to each other), each floor hole's continuation must be a lift, and each floor hole side must be free. The
blueprint attachment's saved direction is restored, `FCD_ANY` directions are set, each floor hole side is snapped to
the other conveyor, the conveyors are linked and everything is verified.

Floor hole continuations are captured before any snapping changes, because setting a side changes what the opposite
side reports.

## Conveyor buckets

`AFGBuildableSubsystem::AddConveyor` merges a conveyor's bucket only with neighbors linked at that moment. A generated
lift registers inside `Construct()`, before it is linked, so it keeps its own bucket. Blueprint lifts registering later
merge into it, but the conveyor that was already in the world never does, and the save then holds two chain actors on
one connected path; the next load can crash in the parallel factory tick (upstream issue #4). After linking, every
seam conveyor whose bucket differs from a linked neighbor's is re-registered: `RemoveConveyor`, `AddConveyor`, then
`mConveyorBucketGroupsDirty`, as vanilla `SplitConveyorGroupFromAttachment` does. `AddConveyor` only acts on the
server.

## Networking

The construct message carries each state's target endpoint, recipe, flow, direct-link directions, direct and snapped
flags, and bridge hologram. `PostConstructMessageDeserialization` rebuilds the preview from that instead of searching.
States are created in the same order on every machine because the blueprint's buildables are.

Before claiming multiplayer support, test host/client preview agreement, client-originated placement, save/reload,
and each dedicated server target that will be enabled.

## Building from source

Clone this repository into the Starter Project's `Mods/` directory as `ConveyorLiftAutoConnect`, build the editor
(`FactoryEditor Win64 Development`) so packaging can load the module, then package with Alpakit or `PackagePlugin`.
