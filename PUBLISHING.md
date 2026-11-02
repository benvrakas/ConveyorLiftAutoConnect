# Publishing checklist

This file is for the maintainer and can remain in the public repository.

## 1. Repository identity

Use this exact mod reference everywhere:

`ConveyorLiftAutoConnect`

Recommended GitHub repository name:

`ConveyorLiftAutoConnect`

The mod reference becomes effectively permanent once the SMR/ficsit.app mod page
is created, so do not rename it casually afterwards.

After creating the repository:

- set `CreatedByURL` to the preferred author/profile URL;
- set `DocsURL` to the repository or documentation URL;
- set `SupportURL` to the GitHub Issues URL;
- add the repository as the Source link on ficsit.app.

## 2. Source license

The repository is licensed under **GNU GPL v3.0**. Keep the root `LICENSE` file
in the public repository and release source archives.

## 3. Final version metadata

The public-release source uses:

```json
"Version": 1,
"VersionName": "1.0.0",
"SemVersion": "1.0.0",
"IsBetaVersion": false,
"IsExperimentalVersion": false
```

`Version` must equal the major number of `SemVersion`: ficsit.app rejects the upload
otherwise ("SemVer major version should match Version"). Keep it at 1 for every 1.x
release and change it only with a new major version.

Keep the SML dependency at the version range actually tested.

Use Alpakit's warning/update helper immediately before packaging to refresh
`GameVersion` if the tested game build has changed.

The source `.uplugin` intentionally omits `RemoteVersionRange` and
`RequiredOnRemote`; SML's conservative defaults require the mod remotely and
require exact mod-version equality.

## 4. Final functional validation

Before `1.0.0`:

- complete the full single-player fixture;
- save/reload with the mod enabled;
- disable the mod and reload a save containing generated bridges;
- verify generated lifts remain as ordinary vanilla Conveyor Lifts and still
  transport correctly;
- multiplayer/dedicated-server testing is intentionally deferred. Keep the
  public disclaimer that multiplayer is untested.

Do not describe multiplayer or dedicated servers as supported until they are
explicitly tested in a future release.

## 5. Release targets

Alpakit Release can package:

- Windows client;
- Windows dedicated server;
- Linux dedicated server.

For 1.0.0, enable the **Windows client** target. Do not advertise dedicated
server targets as supported because they have not been tested. If you later add
dedicated-server support, test and enable the corresponding target(s) in that
release.

The final upload file is the combined `ConveyorLiftAutoConnect.zip`
produced by Alpakit Release.

Inspect the archive before uploading.

## 6. Mod icon and screenshots

Prepare:

- a square PNG mod icon, at least 128x128, for the in-game mod list;
- a display image for ficsit.app;
- preferably one or two screenshots showing stacked blueprints and an automatic
  vertical bridge.

The same artwork can be reused where appropriate.

## 7. Suggested ficsit.app text

The current page texts are in `Docs/Release/` (ModPage.md and FullDescription.md); the
suggestions below are from the original mod.

### Name

Conveyor Lift Auto-Connect Plus

### Short description

Provides vanilla-consistent vertical conveyor auto-connect (until CSS implements it).

### Long-description highlights

- Floor Hole ↔ Floor Hole.
- Floor Hole ↔ Splitter/Merger vertical lift ports.
- Supports Smart/Programmable Splitters and Priority Mergers, and impliclty all
  attachments with vertical ports.
- Blueprint-side lift tier and orientation takes precedence; otherwise the 
  target-side tier is inherited.
- Follows vanilla Conveyor Lift constructibility, including soft clearance.
- Never tunnels through a nearer conveyor endpoint.
- Uses only vanilla Conveyor Lift buildables for generated bridges.
- Include the README "Rebuild seam connections after topology-changing
  blueprint edits" hint.

### Multiplayer disclaimer

> Multiplayer and dedicated servers have not been tested and are not currently
> advertised as supported. The implementation is designed around Satisfactory
> and SML's normal blueprint construction networking, but multiplayer behavior
> remains unverified.

### Bug reporting

Link to the GitHub Issues page and ask for the game/SML/mod versions plus
`FactoryGame.log`.

## 8. Required ficsit.app disclosures

### Network Activity Transparency

Suggested answer:

> This mod makes no external network connections. Multiplayer/game-to-game
> synchronization uses Satisfactory/SML's normal construction networking only.

### AI Usage Transparency

Suggested answer:

> Generative AI (OpenAI ChatGPT) was used extensively during development to
> assist with C++ design, debugging, code generation/refactoring, documentation,
> artwork generation and release preparation. Behavior and regression results were 
> iteratively tested in Satisfactory by the mod author.

Update this disclosure if AI-generated artwork, translations, or other content
is added later.

## 9. Controller compatibility

The mod adds no custom UI or input actions and operates through vanilla blueprint
placement controls.

A reasonable ficsit.app classification is `Supported (Implicit)` once you are
comfortable that normal controller blueprint placement exercises the feature
without a special input path. Otherwise leave it `Untested` initially.

## 10. SMR upload

Before uploading:

- create the mod page with the exact mod reference;
- optionally keep it Hidden while preparing the page;
- validate the `.uplugin`;
- run Alpakit Release;
- upload the combined release zip, not the source zip;
- add a concise changelog;
- set branch compatibility only for branches actually tested;
- make the mod visible before the first public version if you want normal
  discovery/update announcements.

C++ uploads go through SMR's automated approval process.
