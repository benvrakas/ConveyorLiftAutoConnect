# Regression tests

Everything below this line is AI generated.
vvv

## Full U/D endpoint matrix

For each direction, use the physical `Bottom | Upper` endpoint matrix:

| # | Bottom | Upper | Expected |
|---|---|---|---|
| 1 | Floor Hole | Floor Hole | connect |
| 2 | Floor Hole | Splitter | connect |
| 3 | Floor Hole | Merger | connect |
| 4 | Splitter | Floor Hole | connect |
| 5 | Merger | Floor Hole | connect |
| 6 | Splitter | Splitter | reject in vanilla; connect with runtime capability |
| 7 | Splitter | Merger | reject in vanilla; connect with runtime capability |
| 8 | Merger | Splitter | reject in vanilla; connect with runtime capability |
| 9 | Merger | Merger | reject in vanilla; connect with runtime capability |

- `U1-U9`: intended transport direction is upward.
- `D1-D9`: intended transport direction is downward.
- `Bottom | Upper` describes physical geometry, not vanilla placement order.
- Every supported attachment ↔ Floor Hole case starts vanilla placement at the
  attachment.

Run `U6-U9` and `D6-D9` in both capability environments:

- without a lift-placement extension, all eight cases must remain rejected;
- with a working runtime extension such as `VerticalLogisticsQoL`, all eight
  cases must preview, construct, and transport items in the intended direction.

The compatibility test is behavioral. `ConveyorLiftAutoConnect` must not
gain a plugin dependency, installation check, or type reference to the extension.

For mixed supported cases also verify:

- Floor Hole above attachment -> `TopConnection`;
- Floor Hole below attachment -> `BottomConnection`;
- both blueprint-attachment/world-Floor-Hole and
  blueprint-Floor-Hole/world-attachment ownership paths work.

The blueprint-owned attachment path exercises the pre-`BeginPlay`
`mSavedDirections` restoration.

## Tier tests

- **T1:** blueprint Mk.2-backed Floor Hole + target Mk.5-backed Floor Hole ->
  Mk.2 bridge.
- **T2:** bare blueprint Floor Hole + target Mk.4-backed Floor Hole ->
  Mk.4 bridge.
- **T3:** target Mk.4-backed Floor Hole + bare blueprint Splitter ->
  Mk.4 bridge and correct flow.
- **T4:** target Mk.4-backed Floor Hole + bare blueprint Merger ->
  Mk.4 bridge and correct flow.
- **T5:** moving a bare blueprint endpoint between differently tiered targets
  updates the preview recipe.

If retained, the historical `T3*1/T4*1` columns are no-tunneling regressions:
the nearer attachment must block a farther otherwise-valid Floor Hole.

## Variant compatibility

Retain representative capability-compatible attachments:

- **V1:** Smart Splitter ↔ Floor Hole;
- **V2:** Floor Hole ↔ Programmable Splitter;
- **V3:** Priority Merger ↔ Floor Hole.

All must connect to the attachment itself and must not tunnel through it to a
farther Floor Hole.

## Rejections

Retain coverage for:

- no lift tier on either side;
- input ↔ input;
- output ↔ output;
- already-claimed nearest endpoint;
- occupied nearest endpoint;
- attachment ↔ attachment when the live lift hologram rejects the second port;
- incompatible nearest endpoint with a farther valid endpoint.

The invariant is: **never tunnel through the first physical endpoint in the
vertical column.**

## Clearance / constructibility

The vanilla Conveyor Lift hologram is authoritative.

- `CanConstruct()==true`, including soft-clearance/yellow -> bridge eligible.
- `CanConstruct()==false` -> reject.

R12 should remain a positive soft-clearance parity test.

For machine/belt obstruction cases, compare against an equivalent manually
placed vanilla Conveyor Lift rather than assuming "obstruction = reject".

## Initial discovery and representation

- **DR1:** place otherwise identical short and tall columns with their open
  endpoints at different offsets inside the blueprint bounds. Both must begin
  previewing at the same endpoint-to-endpoint distance.
- **DR2:** test an unusually long Floor Hole span. It must preview exactly when
  the configured vanilla Conveyor Lift hologram remains constructible; discovery
  must not add a second maximum height.
- **DR3:** confirm a nearer occupied/incompatible endpoint still blocks a farther
  endpoint found by the supplemental spatial query.
- **DR4:** after locking, dismantle or unload the target. A later candidate must
  require a new first click; the old lock must not transfer to it.
- **UI1:** while a valid vertical bridge is previewed, the source connection's
  ordinary direction indicator should use vanilla's automatic-link
  representation; it must return when the target is lost or reset.

Repeat DR2 after any game update that changes manual Conveyor Lift maximum-height
behaviour. The mod must follow `CanConstruct()` and must not retain an obsolete
independent length rule.

## Fixture health

A continuation lift that is only visually aligned with a Conveyor Floor Hole is
not a valid regression fixture.

When a continuation is expected, first verify that vanilla itself can extend
through the Floor Hole. If it cannot, rebuild the seam-local continuation before
diagnosing the mod.

After topology-changing blueprint edits, rebuild seam-local conveyor
connections before saving the final blueprint.

## Release smoke tests

Before a public release:

1. run the complete single-player fixture;
2. save/reload with the mod enabled and verify generated lifts still flow;
3. save, disable the mod, reload, and verify already-generated vanilla lifts and
   conveyor flow remain intact;
4. test a client-originated placement in host/client multiplayer;
5. if server targets will be advertised, test the packaged Windows/Linux
   dedicated-server targets that will be enabled.

## Finalization and persistence hardening

With `LogVerticalConveyorAutoConnect=Verbose`, every successful generated bridge
must log all three stages:

1. `bridge preflight accepted`;
2. `finalized` with each continuing side reported as `1` and each intentionally
   bare Floor Hole side reported as `-1`;
3. `post-construct audit passed` on the following tick.

Run the full U/D matrix and specifically repeat Floor Hole ↔ Floor Hole with:

- continuations on both sides;
- a bare source side and a continuing target side;
- a continuing source side and a bare target side;
- at least six bridges constructed by one stacked-blueprint placement.

For the multi-bridge case, save immediately after placement, quit to desktop,
reload, and verify flow. Repeat after allowing conveyor chains to run for at
least one autosave interval. Then save, disable the mod, reload, and verify the
generated vanilla lifts again.

A deliberately stale or conflicting seam must fail before bridge construction
with a specific `bridge preflight rejected` reason. It must not leave a generated
lift or change either existing continuation. Any `post-construct audit failed`
line is a regression artifact and should be retained with the save/log.

## Coincident/direct seam hardening

Direct seams are endpoint pairs at the same engine-equal location; they do not
construct a bridge lift. With `LogVerticalConveyorAutoConnect=Verbose`, every
successful direct seam must log all three stages:

1. `direct preflight accepted`;
2. `finalized direct connection`;
3. `direct post-construct audit passed` on the following tick.

Repeat exact-contact Floor Hole ↔ Floor Hole placement in both transport
directions, with valid continuation lifts on both sides. Cover all of the
following:

- an unedited saved blueprint;
- a blueprint whose seam Floor Hole and continuation were rebuilt before save;
- a stale saved source continuation mapping (must reject without changing the
  target or leaving either continuation partially linked);
- a target claimed or connected after preview lock but before construction
  (must reject without overwriting that connection);
- a blueprint-owned attachment port whose runtime direction has not yet been
  restored by `BeginPlay()`;
- at least six direct seams finalized by one blueprint placement, followed by
  immediate save, quit to desktop, reload, and item-flow verification.

Also test a 1 cm vertical offset and other short non-zero spans. They must remain
on the generated-bridge path; this patch must not widen direct-contact tolerance
or silently reinterpret nudge offsets as coincident geometry.

Any `direct post-condition failed`, `direct finalization failed`, or
`direct post-construct audit failed` line is a regression artifact and should be
retained with the save and log.

## Diagnostic logging

Normal warnings are always useful in bug reports.

For detailed diagnostics:

```ini
[Core.Log]
LogVerticalConveyorAutoConnect=Verbose
```

A bug report should include:

- Satisfactory build/version;
- SML version;
- mod version;
- endpoint topology and ownership;
- intended flow direction;
- expected and actual result;
- relevant `FactoryGame.log`.
