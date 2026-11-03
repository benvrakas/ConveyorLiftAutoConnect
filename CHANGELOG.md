# Changelog

All notable changes to this project will be documented here.

## [Unreleased]

## [Plus 1.1.2] - 2026-11-03

### Fixed

- Endpoint positions count as equal within 2 cm instead of 1 mm. Endpoints of
  stacked blueprints were off by up to 2.4 mm, so most of their
  lift seams found no column and only some lifts connected.

## [Plus 1.1.1] - 2026-11-03

### Fixed

- The construct message carries only seams with a target and no longer the bridge
  hologram reference. With every open endpoint and its bridge written as object
  paths, large blueprints pushed `SerializedHologramData` past the 64 KB
  replication limit, `Server_ConstructHologram` was dropped, and the blueprint
  could not be placed onto another blueprint.

## [Plus 1.1.0] - 2026-11-02

### Fixed

- Endpoint positions count as equal within 1 mm. Positions loaded from a save are
  single precision, so exact comparison found no column for blueprints stacked
  onto anything from an earlier session at most map positions.

### Added

- Settings page (`ULiftAutoConnectConfig`): enable, lift tier source (blueprint
  side first, faster lift, world side first), longest lift, detailed log.
- With Verbose logging, each endpoint logs why it found no seam.

### Changed

- Rewritten into separate parts for endpoints, search, bridge holograms, seam
  linking and auditing (see `DEVELOPMENT.md`). Placement behavior is unchanged.
- Attachment lift ports have a fixed facing, so no per-span side rewriting.
- Direct seams and generated lifts are linked through one transaction that undoes
  every change on failure.
- One next-tick seam audit replaces the separate bridge and direct audits.
- The log category is now `LogConveyorLiftAutoConnect`, with shorter messages.
- Spans are checked against blueprint endpoints located once per preview update.

## [Plus 1.0.0] - 2026-11-02

Released as Conveyor Lift Auto-Connect Plus (`ConveyorLiftAutoConnect`).

### Fixed

- Generated bridges and direct seams are re-registered with the buildable
  subsystem after linking, so they join the existing side's conveyor bucket and
  chain instead of saving split chains (upstream issue #4).
- Blueprint placement no longer spawns a bridge hologram for every open
  endpoint; bridges are spawned on demand and unchanged previews are reused.

### Added

- Inactive with an error log when `VerticalConveyorAutoConnect` is installed.
- Verbose timing lines for initialization, construction and preview updates.

## [1.0.4] - 2026-09-22

### Fixed

- Coincident/direct blueprint seam connections now receive final geometry,
  continuation-remap, connection-state, and transport-direction validation
  after the blueprint actors have been constructed.
- Direct connection finalization is transactional: Floor Hole bookkeeping,
  temporary direction restoration, and a newly created reciprocal link are
  rolled back when any immediate post-condition fails.
- Blueprint-owned attachment ports in the direct path now restore and verify
  their persisted pre-`BeginPlay` transport role, matching generated bridges.
- Plugin version metadata now reports `1.0.4` instead of the stale `1.0.2`.

### Diagnostics

- Added explicit direct preflight/finalization reasons and a verbose next-tick
  audit of reciprocal links, Floor Hole backreferences, conveyor buckets, and
  saved/tick-group chain ownership.

## [1.0.3] - 2026-09-18

### Fixed

- Generated Conveyor Lift finalization now preflights both endpoints before the
  lift is constructed and revalidates both sides before either factory
  connection is committed.
- A rejected or incomplete finalization can no longer leave a returned bridge
  with only one reciprocal factory connection or stale Floor Hole bookkeeping.
- Blueprint-owned Floor Hole continuations are explicitly remapped by buildable
  index and physical connection slot and checked against the constructed Floor
  Hole's passthrough reference before linking.

### Diagnostics

- Added a read-only next-tick audit of reciprocal links, Floor Hole
  backreferences, conveyor bucket membership, and saved/tick-group chain actor
  ownership for every generated bridge.

## [1.0.2] - 2026-08-15

### Fixed

- Attachment ↔ attachment capability checks now evaluate blueprint-owned
  vertical attachment ports at their resolved preview-world transforms when
  vanilla has not created a duplicated preview connection. Compatible placement
  extensions therefore receive the same endpoint geometry used for final
  construction.

## [1.0.1] - 2026-08-15

### Changed

- Attachment ↔ attachment candidates now use the real Conveyor Lift hologram's
  runtime connection capability instead of being rejected categorically.
- Compatible placement extensions such as `VerticalLogisticsQoL` can therefore
  enable attachment ↔ attachment blueprint bridges without a hard dependency;
  vanilla-only behavior remains fail-closed.
- Pre-`BeginPlay` direction restoration now handles a blueprint-owned attachment
  in either lift placement slot.
- Initial target discovery now searches complete vertical endpoint columns,
  eliminating blueprint-bound-dependent reach while leaving length validity to
  the real vanilla Conveyor Lift hologram.
- Vertical connection states now drive the vanilla blueprint automatic-link
  representation and reset it when a candidate is lost.

## [1.0.0] - 2026-08-09

Initial public release.

### Added

- Automatic Conveyor Lift bridges across blueprint boundaries.
- Floor Hole ↔ Floor Hole support.
- Floor Hole ↔ vanilla-compatible conveyor-attachment support.
- Capability-based support for normal, Smart/Programmable Splitters, normal
  Mergers, Priority Mergers, and compatible future/modded variants.
- Blueprint-side-first lift-tier selection.
- Upward and downward flow support.
- Symmetric no-endpoint-tunneling behavior.
- Vanilla Conveyor Lift constructibility/soft-clearance parity.
- Pre-`BeginPlay` restoration of blueprint-owned attachment connection roles.
- Construct-message serialization for deterministic bridge intent.
- Detailed regression fixture and diagnostic logging.

### Known limitations

- Multiplayer and dedicated servers have not been tested and are not advertised
  as supported in this release.
