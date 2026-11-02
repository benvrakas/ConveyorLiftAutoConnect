# Development notes

## Pre-amble (human-written):

This document was generated using ChatGPT. The basic ideas / principles of this mod are:
- Extend the auto-connect feature to vertical conveyor lifts until it gets implemented by CSS itself.
- Be consistent with vanilla behavior:
  - we can connect: floor-hole ↔ floor-hole, and floor-hole ↔ [splitter / merger]
  - vanilla does not allow building a lift between two open vertical splitter / merger ports
  - another mod may extend the real lift hologram to allow that placement; use
    the runtime capability without depending on the mod itself
  - we may soft-clip the auto-connection
- Inherit lift Mk. and orientation from already attached lift(s), where blueprint-side wins.
- At least one side must already have an attached belt.

Everything below this line is AI generated.
vvv

This document records the current architecture and invariants. It is not a
development diary.

## Core principle: reproduce vanilla semantics

The mod should create the state vanilla Conveyor Lift placement would create,
rather than inventing an independent conveyor-connection system.

A generated `AFGConveyorLiftHologram` is authoritative for placement validity.
After configuration, the normal vanilla update/validation path runs and the
bridge is eligible only when `CanConstruct()` is true.

Do not add arbitrary mod-defined distance, height, clearance, or angular
thresholds. Numerical tolerances are limited to engine equality semantics or
values supplied by vanilla/API data.

The one intentional validation-context correction is suppression of transient
`UFGCDInvalidFloor` on the tagged synthetic blueprint child. This is specific to
the synthetic child context; soft-clearance disqualifiers are not suppressed.

## Endpoint model

Supported endpoint kinds:

- Conveyor Floor Hole side;
- vanilla-compatible conveyor-attachment lift port.

A conveyor attachment is compatible when:

1. it derives from `AFGBuildableConveyorAttachment`;
2. it exposes both canonical
   `AFGConveyorAttachmentHologram::mLiftConnection_Bottom` and
   `mLiftConnection_Top` components;
3. those components are distinct vertical `UFGFactoryConnectionComponent`s.

`TryGetAttachmentLiftPorts()` is the single capability probe. Endpoint discovery
uses the exact validated components returned by that probe.

Supported topology:

- Floor Hole ↔ Floor Hole;
- one compatible attachment ↔ one Floor Hole;
- compatible attachment ↔ compatible attachment when the live Conveyor Lift
  hologram accepts the second attachment endpoint.

The manager must not infer attachment ↔ attachment support from an installed-mod
name or class list. It configures a real `AFGConveyorLiftHologram` and calls its
`CanConnectToConnection()` method. Vanilla currently rejects the topology. A
runtime hook such as `VerticalLogisticsQoL` can extend that method, which enables
the bridge implicitly; otherwise the candidate fails closed.

## Four independent concepts

Keep these separate:

1. **Ownership** — the endpoint originated in the blueprint or already existed in
   the world.
2. **Representation** — use BlueprintWorld preview transforms or the remapped
   constructed world actor.
3. **Transport direction** — input vs output.
4. **Vanilla placement order** — first vs second hologram placement endpoint.

`FEndpointRef::UsesBlueprintPreviewTransform` describes representation only.

For attachment ↔ Floor Hole, vanilla placement starts at the attachment
regardless of ownership or transport role.

For attachment ↔ attachment, placement starts at the transport-input attachment
and the second attachment is accepted only through the live hologram capability
check.

## Attachment placement state

Measured vanilla behavior:

- placement slot 0 = attachment vertical connection;
- placement slot 1 = Floor Hole;
- `mSnappedPassthroughs = [nullptr, floorHole]`;
- `mSnappedConnectionComponents[0] = attachmentPort`;
- slot 1 contains the connection across the Floor Hole when present;
- attachment as transport input:
  attachment port `FCD_OUTPUT`, `mArrowDirection=FCD_OUTPUT`,
  `mIsReversed=false`;
- attachment as transport output:
  attachment port `FCD_INPUT`, `mArrowDirection=FCD_INPUT`,
  `mIsReversed=true`;
- Floor Hole above attachment -> `TopConnection`;
- Floor Hole below attachment -> `BottomConnection`.

Port identity and transport direction are independent.

Capability-extended attachment ↔ attachment state keeps the same placement-slot
model:

- placement slot 0 = transport-input attachment vertical connection;
- placement slot 1 = transport-output attachment vertical connection;
- `mSnappedPassthroughs = [nullptr, nullptr]`;
- both `mSnappedConnectionComponents` entries contain the corresponding direct
  attachment ports;
- after applying transforms and connection directions, slot 1 must pass the
  live lift hologram's `CanConnectToConnection()` check.

## Capability-probe transform context

`CanConnectToConnection()` is queried during blueprint preview, before the final
blueprint actors exist. Both connection components supplied to that call must
therefore represent the same world-space geometry that final lift placement will
use.

The synthetic Lift hologram does not automatically move its slot-1 connection
component to `mTopTransform`. In addition, vanilla may not create a duplicated
preview connection for a hidden vertical attachment port. In that fallback case,
the original component remains attached to the BlueprintWorld buildable and its
connector geometry is still blueprint-local.

For the capability probe:

- prefer an existing vanilla duplicate from
  `mDuplicateConnectionToOriginalMap` when one represents the attachment port;
- stage the synthetic Lift's slot-1 connection at the resolved endpoint location;
- only when no duplicate exists, stage the BlueprintWorld attachment component
  at the endpoint's resolved preview-world transform;
- call the live hologram method, then immediately restore every component
  transform that was staged.

Staged preview components are made movable as required and remain movable for the
hologram lifetime to avoid component re-registration on every preview tick. World
attachments already expose world-space geometry and must not be staged. This is a
validation-context correction only; it must not substitute a mod-defined
acceptance rule for the live capability result.

## Locked transport intent

Direction is resolved while the preview state is valid and stored as normalized
physical-lower-endpoint intent.

Final construction must not rediscover that already-locked intent from newly
spawned blueprint actors: they can still be pre-`BeginPlay`.

## Pre-BeginPlay attachment lifecycle

Blueprint bridge construction can happen before newly spawned blueprint
attachments receive `BeginPlay()`.

`AFGBuildableConveyorAttachment` persists dynamic connection roles in
`mSavedDirections`. Before `BeginPlay`, a selected port can expose `FCD_ANY` or
a concrete default/stale direction.

For a blueprint-owned bridge, restore every selected attachment port from
`mSavedDirections` before constructing the bridge child. Attachment ↔ attachment
can put the blueprint-owned attachment in either placement slot. Resolve each
persisted index with `UFGFactoryConnectionComponent::SortComponentList`.

The persisted direction must be concrete and exactly match the transport role
already locked during preview. Otherwise fail closed. Existing world attachments
are not modified.

## Floor-hole passthrough state

A geometrically aligned lift mouth is not necessarily snapped through a Conveyor
Floor Hole.

When finalizing a bridge, capture any continuation across the Floor Hole before
mutating snapped bookkeeping. `SetTop/BottomSnappedConnection` changes
passthrough state, so resolving the continuation afterwards can lose the
pre-existing transport relationship.

Do not manufacture a continuation for a stale fixture where vanilla does not
report one.

## Tier policy

Recipe precedence is:

1. blueprint-side adjacent Conveyor Lift recipe;
2. target-side adjacent Conveyor Lift recipe;
3. no recipe -> reject.

Tier choice is independent of transport direction and attachment placement order.

## Candidate discovery

Vanilla `AFGBlueprintHologram` has one clearance detector. Its overlap callbacks
feed nearby actors to every registered open-connection manager. The stock
factory/pipe/rail managers therefore discover actors relative to the blueprint
bounds, not relative to each open connection.

That detector is a broadphase, not a semantic connection-distance contract. Its
effective reach varies with an endpoint's position inside the blueprint bounds.
Using it alone therefore causes a terminal near the top of a tall blueprint to
discover much less vertical space than the same terminal near the centre.

The vertical manager preserves the ordinary overlap feed but supplements it with
one buildable-subsystem query through the complete world-height prism containing
its source endpoint columns. Exact XY matching and no-endpoint-tunneling reduce
that broadphase result to physical candidates. The generated, real
`AFGConveyorLiftHologram` remains authoritative for length and constructibility;
candidate discovery itself imposes no height limit.

This intentionally allows arbitrarily long Floor Hole bridges when vanilla's
Lift hologram allows them. If CSS later adds a maximum-height disqualifier to the
normal Lift validation path, `CanConstruct()` will reject those candidates and
the mod will inherit that shorter limit without its own range update.

If the buildable subsystem is unavailable, fail closed to the ordinary vanilla
nearby-actor feed rather than scanning all world buildables or introducing a
fallback distance.

## Automatic-connection representation

Vanilla managers broadcast target/validity transitions through
`mOnConnectionStateChanged`. `AFGBlueprintHologram` uses that callback to replace
ordinary connection-direction indicators with its automatic-link
representation.

The vertical manager follows the same delegate contract. A blueprint-world
factory connection may have a duplicated preview component attached to the
hologram, so resolve and broadcast that existing duplicate from
`mDuplicateConnectionToOriginalMap`; do not create a separate mod-owned icon.
Floor Hole sides have no connection component of their own, so their
representation uses the adjacent Conveyor Lift connection when one exists.

## No endpoint tunneling

A physical endpoint terminates a vertical path even when it is not usable for an
automatic bridge.

Target side:
- gather physical candidates including occupied endpoints;
- identify the nearest physical endpoint plane;
- validate only candidates on that plane;
- never examine farther targets if the nearest plane cannot produce a bridge.

Blueprint/source side:
- a farther source cannot form a bridge through another physical blueprint
  endpoint in the same XY column;
- the source buildable itself is explicitly excluded from blocker detection.

This rule is independent of endpoint openness. An unsupported or occupied
intervening endpoint still blocks tunneling.

## Bridge hologram lifetime

Child holograms are cached per connection state and lift recipe for the lifetime
of the parent blueprint hologram.

Do not destroy and respawn a child with the same stable name while the parent is
alive: Unreal's deferred destruction can leave the old child registered and
trigger duplicate-child assertions.

## Final connection linking

Blueprint actors may not have run `BeginPlay()` when bridge construction occurs.
Finalization therefore performs the required reciprocal factory connection
linking explicitly.

Finalization is transactional at the manager boundary:

- before constructing the bridge, resolve both continuations and require every
  non-bare connection to be valid, vertical, direction-compatible, and open;
- for a blueprint-owned Floor Hole continuation, remap its owning buildable and
  physical connection slot explicitly, then require the constructed Floor
  Hole's saved passthrough pointer to agree with that mapping;
- after construction, revalidate both endpoints before calling `SetConnection`
  on either side;
- expected bare Floor Hole endpoints remain valid and produce link result `-1`;
- any real link conflict, non-reciprocal result, or failed post-condition rolls
  back bridge-owned connection/snap state and rejects the fresh bridge actor.

A coincident pair takes the direct path and creates no bridge actor, but it must
follow the same discipline. Preview resolves and serializes a concrete direction
for each connection. Final construction then rechecks exact geometry, verifies
the blueprint Floor Hole continuation against its explicit buildable/connection
remap, restores a blueprint attachment's persisted pre-`BeginPlay` direction,
and rejects conflicting or partially reciprocal connection state. Both Floor
Hole backreferences are staged before `SetConnection()`. Any failed reciprocal
or bookkeeping post-condition restores the original backreferences and
directions and clears only the link created by that transaction.

Do not attempt to repair conveyor chains directly from this manager. The
next-tick audit reads bucket and chain ownership for diagnosis, but chain
invalidation and rebuilding remain owned by the game subsystem.

For generated lifts, do not assume `GetConnection0()` is always transport input
and `GetConnection1()` output. Attachment-first placement can reverse physical
slot order. Resolve input/output from the constructed component directions and
verify that `GetConveyorLiftFlowDirection()` agrees with the locked transport
intent before linking.

## Networking

`FConnectionState::operator<<` serializes the snapped state required for
construction:

- geometry-derived blueprint side;
- target buildable/kind/side/connection identity;
- lift recipe;
- normalized lower-endpoint direction;
- preview-resolved blueprint/target directions for a direct connection;
- snap/direct-connect state.

`PostConstructMessageDeserialization()` reconstructs the preview child from that
state rather than searching for a new target.

Before claiming multiplayer support, regression-test:

- host/client preview agreement;
- client-originated blueprint placement;
- save/reload;
- Windows dedicated server;
- Linux dedicated server if that release target is enabled.

## Building from source

Use the Satisfactory Modding Starter Project matching the game/SML version.
Clone this repository into the Starter Project's `Mods/` directory so the folder
containing `ConveyorLiftAutoConnect.uplugin` is named exactly:

`ConveyorLiftAutoConnect`

Regenerate project files/build as needed, then use Alpakit for development or
release packaging. The repository intentionally contains only mod/plugin source
and test fixtures, not generated binaries.
