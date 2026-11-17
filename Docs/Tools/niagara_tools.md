# Niagara Tools

Tools for creating and editing Niagara particle systems programmatically.

Default grouped mode exposes `niagara_search`, `niagara_call_read` and
`niagara_call_write`. The operation names/parameters below are passed to these
entry points, or called directly with `MCP_TOOL_MODE=direct`.
See the [calling guide](README.md#grouped-mode-default).

## Execution And Save Failures

The 2026-10-22 frame-boundary dispatcher has been **built and deployed to SkillTest
and NodeFall**. It addresses the execution-timing defect in the
[Niagara save reentrancy report](../reported-issues/niagara-save-reentrancy-crash.md).
Python updates or visible schemas alone do not update a running native plugin.

With the matching native build, external commands enter a bounded FIFO queue and
run one at a time from the main loop's `OnBeginFrame`, not a pumpable GameThread
task. Saving, GC, slow tasks and modal windows defer execution. Pending requests
expire after 20 seconds without editing; shutdown releases pending waiters before
joining the server thread. A command already executing is not rolled back merely
because its connection or server stops.

- Dispatch refusal/cancellation: `stage=dispatch`, `execution_state=not_started`,
	`modified=false`, `saved=false`. No late execution of the cancelled request.
- Stop after execution started: `execution_state=unknown`; no false claim that
	nothing changed or nothing saved. Read back before any retry.
- Niagara automatic-save failure: `stage=save`, `modified=true`, `saved=false`,
	`error_code=niagara_save_failed`, `asset_path`, `package_dirty`. Changes may
	remain in memory. Automatic saving is retained, not silently removed.
- Transport timeout/reset/no response: failure with unknown outcome, not proof of
	rollback or nonexecution. Python preserves available native receipts and never
	retries these authoring operations automatically.

Six relevant native tests and 74 Python regressions passed in the repository
test project. The deployed SkillTest plugin also passed those six native tests,
a separate-process emitter-copy cold reload and real MCP reads; NF built and
passed read-only MCP smoke tests without changing business assets. Existing
client configurations/permissions were retained; reconnect clients to load the
shared Python changes. NF Claude project trust approval remains user-controlled.
Real validation/render-wait reproduction, the equivalent continuous authoring
batch and its cold reload are still pending SkillTest acceptance; deployment
alone is not a claim that the reported production D3D12 crash has been eliminated.

## System Management

### `create_niagara_system`
Create an empty Niagara system asset.
- `asset_full_path` — Content path (e.g. `/Game/Effects/NS_MyFire`)
- `template_system_path` — optional source Niagara system to duplicate

### `list_niagara_systems`
List Niagara systems with optional `path`, `include_engine_content`, and `name_filter` filters.

### `read_niagara_system`
Read a Niagara system's emitters, module stacks, rapid iteration parameters, and renderer properties. Identify it with `asset_full_path`.

Native `reader_contract=2` retains all legacy fields and adds `scalability` on the
System, each Emitter and each Renderer. These are **loaded authored settings**,
including defaults, not disk-forensic values or resolved runtime eligibility.
Check `package_dirty` when inspecting unsaved authoring. Top-level
`scalability_complete` aggregates the supported layers; each section provides
`contract_version=1`, `complete`, `unsupported`, `field_states` and
`matches_engine_default`. Equality to a class/struct default does not prove a
field was explicitly serialized or its override is active. Null with state
`unbound` is distinct from an unsupported field; false and empty arrays are
actual readable values, not missing-data substitutes.

System fields include `EffectType` reference/null, `effect_type_details`,
`Platforms`, `bOverrideScalabilitySettings`, ordered `SystemScalabilityOverrides`,
and the local-player culling override switch/value. The expanded Effect Type
remains its saved rules, not a System's final effective settings.

Emitter fields retain display `name` and legacy authoring `enabled`. Added
`handle_id`, `scalability.display_name`, `unique_name`, `mode`, object path,
selected/resolved version GUIDs and exposed-default status identify the actual
Handle instance. `GetEmitterData` uses that Handle's selection, never a forced
latest-version upgrade. Stateful Platforms and every ordered ScalabilityOverride
row include both switches and values. Stateless rules explicitly report
unsupported/`complete=false`; no false defaults are synthesized.

Renderer `enabled`, materials, bindings and legacy `platforms` remain. Added
`renderer_index` is the original source slot, not a filtered list index;
`renderer_slot_count` and `null_renderer_indices` preserve gaps. Disabled
renderers are retained. The canonical `renderer.scalability.Platforms` uses the
same structure as every other layer; it never replaces parent constraints.

All Platform sets retain native `QualityLevelMask`, `DeviceProfileStates` and
`CVarConditions`. Signed negative masks such as -1/-29/-4 are valid and preserved.
`quality_levels` expands current engine quality names/indices, not a hardcoded
five-level list. Device entries retain uint32 masks and add saved
`quality_states` Default/Enabled/Disabled. Default means inheritance, not false.
Conditions retain native names, bounds, switches and Pass/Fail responses without
evaluating CVars or selecting device profiles. Mask bits alone are not eligibility.

Module records add call node/graph paths and GUIDs, full script path, saved and
resolved version GUIDs, version numbers and exposed-default status. Unversioned,
unresolved and no-script cases are explicit. `input_pins` records saved defaults,
types and link identities, not evaluated input bindings or symbolic data flow.
Use the existing static-switch/input readers for their inventories. Graph call
enumeration is not a proof of output-stack execution order or of whether a
module consumes SpawnCountScale. No module-name behavior table is embedded.

### `read_niagara_diagnostics`

Discover `niagara_search(tool="read_niagara_diagnostics")`, then use its returned
read endpoint. This operation remains available in read-only mode and follows the
independent Niagara filter. Updated native `diagnostics_contract=1` is required;
an old DLL reports a capability error, never a graph-wide fallback.

| Parameter | Default / Meaning |
| --- | --- |
| `asset_path` | Required exact `/Game` Niagara System package; no inline subobject path |
| `emitter_handle_id` | `""`; exact nonzero Handle GUID when selecting a Usage/module |
| `script_type` | `""`; `spawn`, `update`, `emitter_spawn`, `emitter_update`, `event`, `simulation_stage` |
| `script_usage_id` | `""`; exact nonzero Usage GUID, essential for distinguishing event handlers/stages |
| `module_node_guid` | `""`; exact call-node GUID in the selected stack, never display-name selection |
| `include_inputs` | `true`; bounded input/static-switch and override-source diagnostics |

The system read lists selected emitter versions, authored enabled flags, renderer
slots, event handlers and ordered output-linked stacks. Events retain owner Handle,
Usage ID, source GUID/name, execution mode and authored spawn/event limits. A zero
native SourceEmitterID means self; missing/ambiguous sources are unresolved, not
guessed by display name. No Renderer on a source emitter is legal. Authored limits
are not runtime event or particle counts.

Stack traversal follows a unique parameter-map chain to an exact output Usage ID,
using the engine's normal/interpolated Spawn equivalence only. Disconnected,
ambiguous or cyclic chains are unsupported; shared graph node enumeration is not
execution order. Missing/ambiguous selected identities refuse instead of reading
another handler. Module records include call identity, function and selected/resolved
version; event module selection requires Handle, script type and Usage ID.

Inputs distinguish explicit overrides, rapid iteration, linked parameters, dynamic
inputs, local pins, data-interface/unresolved expressions and script defaults.
Dynamic chains are bounded (depth 6, 8192 inputs); typed enums retain native paths,
numeric values and canonical/display names. Unexported script-default pin access is
explicitly unresolved, not a fabricated effective value. Time domain/units are also
unresolved unless actually available; names do not establish business semantics.

`coverage` separates authored data, execution stacks, editor preview, runtime and
external dependencies. Current `isolation_enabled`/per-Handle `isolated` and
`render_excluded_by_isolation` are distinct from authored enabled or simulation
state. Existing non-template component identities/worlds/active/registered flags
are observed only; an absent component is not a zero-particle measurement. Full
runtime culling, event counts and CPU/GPU particle snapshots remain **unsupported**.
External materials/textures/conversion logs are not fully decoded here.

`success` does not imply `complete`: unsupported stack/source/configuration paths
set `complete=false` with exact reasons. Even `complete=true` covers only the
declared supported domains, not runtime simulation. Large responses spill the full
JSON and retain contract/coverage metadata; read `file_path`, not only its preview.
The reader never sets Solo/isolation, spawns/ticks/resets simulation, compiles or
saves assets, changes global quality or clears user drafts. Editor restart itself
does not guarantee preservation of transient preview/Solo state.

2026-11-17: fully built/deployed to SkillTest and NF, preserving local plugin
customizations and client settings. Both actual DLLs passed the two native tests
and original-profile grouped-read acceptance; SkillTest's reported Converted asset
read complete and resolved TrailCore's LocationEvent to Electricity. All 14905
SkillTest/5914 NF protected original files were unchanged; each project added only
one isolated diagnostic fixture. This is diagnostic read acceptance, not runtime
particle sampling or visual reconstruction. Reconnect existing MCP services.

Legacy `list_module_inputs` and all existing input writers retain their four-stage
scope; use this operation for event/simulation-stage reads, not guessed inline
UObject paths. See the [feedback and acceptance record](../reported-issues/particle-system-inspection-event-ribbon-preview.md).

### `read_niagara_effect_type`

Required: `asset_path`, e.g. `/Game/Effects/EffectTypes/EffectType_World_Portal`.
Discover through `niagara_search(tool="read_niagara_effect_type")`, then the
returned read endpoint. This is read-only and respects Niagara/category filters.

Returns the actual asset/class, UpdateFrequency, CullReaction, local-player policy,
SignificanceHandler reference/class/readable settings and all ordered System and
Emitter settings rows. Native reflected field names retain every supported
flag/value, platform condition, visibility/pre-culling/proxy/budget structure and
curve keys. External float curves expand to their typed keys; unbound references
are null. Unsupported types or bounds are listed explicitly and make the section
incomplete; no ExportText fallback masquerades as typed data.

### Read Boundaries And Acceptance

The saved-configuration contract does not implement arbitrary requested-context
resolution or instance diagnostics. It does not set/restore global quality CVars,
Device Profiles, authoring enabled flags, preview quality or culling state.
`read_context` samples active profile and quality CVar values/source flags;
`editor_preview_override_bound` records only delegate presence, not a preview
quality value. The reader does not execute that delegate. Load-triggered engine
work is possible, but the reader never explicitly compiles, saves or modifies assets.

Large results use reliable JSON spill (`overflow`, `file_path`, real counts).
The full file includes all rules, field states and curve keys. A preview is not
the complete result. Older native plugins preserve legacy reads but explicitly
report missing scalability capability; Python schemas alone do not update a DLL.

2026-10-31: 77 Python regressions and three native tests passed in the repository
and the deployed SkillTest plugin: Platforms, SavedRules, ReadCompatibility.
Lyra's six fixed systems and four Effect Types passed public grouped-read
acceptance, including signed masks, Sparkle 0.1/disabled-default rows and SpawnRate
selected identity. Dirty state, package/config hashes and quality/Profile context
were unchanged. This used the existing UI/export oracle, not a new text export
substituted for MCP or a manual post-fix UI/performance/particle-count test.

Later the same day, NF's matching native reader was built and deployed, preserving
its local plugin customizations and client settings. Five fixed systems and
NET_Gameplay_Burst passed public grouped reads: complete layers, selected emitter
identity, three actual Effect Type associations, signed masks and saved
0.3/0.6/0.8 rows. Dirty state, sample hashes and sampled quality/Profile context
were unchanged. These are saved settings, not proof of runtime spawn scaling.
Existing clients need reconnecting; Claude project approval remains user-controlled.

See the [resolution record](../reported-issues/niagara-scalability-reading.md)
and [repeatable acceptance script](../../Python/scripts/niagara/verify_scalability_read.py).

## Guarded Niagara Authoring

2026-11-01: seven internal operations reuse the existing Niagara grouped endpoints.
Discover each exact operation with `niagara_search(tool=...)`; call its returned
read/write endpoint. Updated native plugin required; never fall back to legacy
writers or raw UObject edits. Read-only mode exposes only the new authoring reader.

| Operation | Contract |
| --- | --- |
| `read_niagara_authoring` | Exact `/Game` package, target `system` / `effect_type` / `emitter` / `renderer` / `module`; returns `authoring_contract=1`, saved fields, identity and `baseline` |
| `create_niagara_effect_type` | New exact package, no overwrite; default dry_run=true/save=false; reports created/modified/saved/dirty |
| `set_niagara_scalability` | expected_baseline + changes of `{field,value}`; System association/null, Platforms/Overrides/local-player flags, selected Emitter rules, Renderer Platforms, Effect Type rules and Age/Distance/None significance |
| `save_niagara_authoring` | Explicit whole-target-package save of a reviewed draft, with expected baseline and compilation validation; never Save All |
| `set_niagara_user_parameters` | System default declarations/updates `{name,type,value}` for User Float/Int/Bool/Color; same type/value no-op; duplicate/wrong types refuse |
| `bind_niagara_module_input` | Exact Handle, script phase, module GUID and module baseline; canonical input name to a declared type-compatible User variable; fresh/Local or identical linked no-op only |
| `set_niagara_material_bindings` | Exact Renderer identity/baseline; patch `{material_parameter,user_parameter}` AttributeBindings, User Float->Scalar / Color->Vector, preserving other bindings/constants |

All new edits default to dry_run=true/save=false. Existing dirty packages refuse
editing, even with save=false. Review the draft through the reader and explicitly
save with its fresh baseline. The latter saves all current edits in that package;
approval of an individual field is not approval of unrelated user draft changes.
Baseline covers the selected target identity/fields and User declarations; module
targets also cover saved graph pins/links and rapid values, Renderer targets include
material parameters/sources. It is not a general package revision or runtime state.

Emitter/Renderer targets require `emitter_handle_id`; Renderer uses the actual
source `renderer_index`, including disabled slots. Module targets add `script_type`
(`spawn`, `update`, `emitter_spawn`, `emitter_update`) and `module_node_guid`.
Selected/resolved versions and object/graph paths are part of the read baseline;
GUIDs may repeat across cloned assets and are not globally unique selectors.

Struct patches recursively preserve unspecified fields. Arrays replace ordered
complete saved rows, including every inherited field; use `fields` from the
authoring reader, not decoded `quality_levels` annotations from the scalability
reader. Unknown/duplicate fields, invalid types, bounds and stale baselines reject
before mutation. Signed masks and uint32 profile masks retain their native meaning.
No global quality/Profile/preview switching, eligibility resolution or runtime
culling diagnosis. Stateless authoring is explicitly unsupported.

```json
{
	"asset_path": "/Game/__Dev/VFX/NS_Test",
	"target": "emitter",
	"emitter_handle_id": "<Handle GUID>",
	"expected_baseline": "<read_niagara_authoring baseline>",
	"changes": [{"field": "Platforms", "value": {"QualityLevelMask": -29}}]
}
```

User Color defaults use linear HDR `[r,g,b,a]`. This edits authorized NS defaults,
not running component values. Instance playback still uses component setters;
it must not mutate shared NS/MI assets. Declaring a variable does not bind or prove
visual consumption. Static/object/DI module inputs and replacement of existing
different linked/dynamic/expression overrides are refused, preserving old curves.
Same linked User is a no-op; converting Local removes only its corresponding
rapid parameter on apply. Canonical names come from the authoring reader.

`read_niagara_system` now also returns `renderer.material_parameters`: complete
typed AttributeBindings (declared/resolved/child variables), Scalar/Vector/Texture
constants, nullable StaticBool values, saved material parameter names/associations
and truthful supported/complete/unsupported state. Runtime component overrides and
actual MID values are not claimed by this asset reader. The writer currently patches
User Float/Color mappings only, not arbitrary constant/texture/static/DI bindings.
Every saved material must expose the unambiguous compatible global parameter;
constants which would override the requested mapping cause rejection. Existing
DynamicParameter channels and shared MI assets are never overwritten.

Apply uses an Editor transaction, then compilation validation and optional target
save. Receipts retain before/after, modified/compiled/saved/package_dirty, per-script
compile status/events and separate failure stages. Compile/save failures can leave
applied dirty state; no disk rollback or automatic retry. Transport failures report
unknown execution state without fabricating modified=false/saved=false.

Verified in repository/SkillTest UE5.7.4 isolated fixtures: typed negative masks,
ordered Profile/CVar rows, significance, User types/no-op, exact Linked input,
material mappings, dirty/stale guards, Undo and injected compile/save rejection.
Fresh-process reload retained rules/links/material references; two real components
used distinct engine MIDs with Scalar 2/5 and Color red/green, shared assets unchanged.
Renderer updates required nonzero particles and end-of-frame submission; early
tests with too-short simulation/default material getters failed and remain logged.
This is runtime parameter reception, not post-fix visual scaling/performance proof.

Public grouped acceptance: [verify_authoring.py](../../Python/scripts/niagara/verify_authoring.py),
using existing SkillTest authoring permission and only new `__Dev` copies. Full
JSON/receipts stay in `Saved/NiagaraAuthoringAcceptance`. SkillTest deployment is
five scoped native files. NF received the same five files later on 2026-11-01;
NodeFallEditor UE5.7.4 built successfully in 286.25 seconds, preserving local
plugin customizations and client settings. Public grouped read/dry-run/rejection
smoke confirmed authoring contract1 and all seven native routes, unchanged baseline,
quality/Profile context and zero dirty packages. No NF assets were created, edited
or saved; actual write/cold/MID acceptance remains the SkillTest evidence above.
NF receipt: `Saved/UnrealMCPDeployment/Authoring_20261101_cd296e29/receipt.json`.
Existing clients need reconnecting; Claude project approval remains user-controlled.

### Limited Existing-Link And Curve Extensions

2026-11-01 addition: three new internal write operations reuse grouped Niagara
endpoints. They require the newer native implementation; the earlier seven-operation
authoring deployment does not include them. Defaults remain dry_run=true/save=false;
dirty/stale/type/identity checks, Undo, compilation and partial-failure receipts apply.

- `redirect_niagara_user_link`: explicit opt-in operation for one simple same-graph
	ParameterMapGet User link. Pass the exact module identity/baseline, canonical input,
	expected_linked_parameter and new user_parameter. Both declared types must match.
	Wrong old link, multiple sources, dynamic/expression/curve or missing types refuse.
	Only the target edge changes; old User declaration/node/other consumers remain.
	Current already-desired link is no-op. Ordinary bind still refuses differing links.
- `read_niagara_authoring(target="dynamic_input")`: retain module_node_guid for the
	parent, add parent_input_name and function_node_guid. The function must be reachable
	from that exact parent's override chain; selected/resolved GUID, script path/change
	ID, canonical typed inputs and graph/pin/link/rapid baseline are returned. Depth16/
	64-node bounds apply. No name/first-node/latest-version substitution or expression
	editing. Non-static typed child inputs can use `bind_niagara_dynamic_input`, which
	retains old surrounding curves and still refuses different existing overrides.
- `multiply_niagara_float_input`: wrap an existing dynamic Float result as preserved
	result times User.Float. Pass target module/dynamic_input, canonical input,
	expected_source_node_guid/expected_source_pin_id, fresh baseline and explicit
	multiplier_version_guid from standard_float_multiplier in the reader. The exact
	standard Multiply_Float script is read-only preflighted; apply gives the wrapper
	a dedicated standard ParameterMapSet so shared old curve sets do not become cyclic.
	Old output node/pin, curve keys/tangents/version and other consumers stay connected.
	Same script/resolved version, source A and User B identify an existing wrapper:
	no-op, never another nested multiplier. Different User/version refuses replacement.
	This does not replace curve/time inputs or support arbitrary vector/color math.

Set the User multiplier default to1 for the original result; broader Sprite/Mesh,
velocity/beam/world-position/physics and whole-effect scale semantics remain the
project's responsibility. The authoring reader exposes source node/pin/script/version
identity for linked inputs. Prefer inspecting actual canonical child input names,
not assuming conversion functions all use the same channel names.

Repository verification: 81 Python regressions; seven native authoring/scalability
regressions and an actual separate-process DynamicBindingsReload passed. Isolated
FloatFromCurve keys/tangents, shared source witness, Undo, no-op and runtime1/.5/2
consumption passed. Fixed-rate particle counts use independent integer/time-step
expectations (initial frame consumes no spawn step), not multiplication of an already
truncated integer count. A1 preserved exact original runtime output. Logs are local:
`Saved/Logs/Niagara-LinkExtensions-Regression.log`, `Niagara-LinkExtensions-Cold.log`,
`Niagara-LinkExtensions-LUTVerified.log`. Earlier parameter discovery, graph-cycle
and oracle failures remain recorded; temporary diagnostics and unsafe cloned-graph
preflight were removed. No engine/global quality/production asset changes.

These additions were subsequently deployed to SkillTest and NF on 2026-11-01,
updating only Bridge, Niagara Commands and Authoring cpp/header. SkillTestEditor
built in 315.77 seconds; seven native regressions, actual independent cold read and
public grouped exact-read/no-op/rejection smoke passed. NodeFallEditor built in
80.52 seconds; its existing working copy passed old Color User redirect and preserved
FloatFromCurve result multiplier dry-run, with exact dynamic child identities.
No NF working/production asset was edited or saved. Its original map/port and
client permissions were retained; actual writes remain the isolated SkillTest tests.
Existing clients need reconnecting; Claude approval remains user-controlled.
Backups are local `Saved/UnrealMCPBackups/LinkExtensions_20261101_*`;
full public smoke receipts are in each project's `Saved/UnrealMCPDeployment`.
Python discovery alone is still not deployment verification; no commit/push is automatic.

## Particle Attribute Readers

`read_niagara_particle_reader` reads a canonical module input without creating an
override. Require exact `/Game` `asset_path`, `emitter_handle_id`, `script_type`,
`module_node_guid` and `input_name` from `read_niagara_authoring`. The returned
`reader_contract=1` distinguishes default/unconfigured, local, shared/external and
unsupported overrides, and reports DI class/path/owner, BindingMode, EmitterName,
resolved source Handle/unique name/version and an optimistic baseline. Read complete
spill JSON before editing. The baseline includes actual Reader fields and every
emitter's Handle/name/selected/resolved version, so renames invalidate stale reads.

`bind_niagara_particle_reader` adds `expected_baseline` and an exact
`source_emitter_handle_id` from that same system. Defaults are `dry_run=true` and
`save=false`. Only distinct stateful CPU source/target emitters are supported.
Preflight rejects stale/dirty targets, self/cycles, missing/wrong inputs and existing
different/shared/external/complex overrides. It creates a target-local DI with
BindingMode Other and the source's exact unique name; no suffix guessing, source
setting edits or implicit rebind. Same binding is a no-op, including no save.
Apply uses an Undo transaction, compiles, verifies actual local ownership/binding,
then optionally saves only the approved target. Failures retain real
modified/compiled/saved/dirty, before/after and stage; timeout never auto-retries.

The standard `SpawnParticlesFromOtherEmitter` module owns the canonical Reader
input in Emitter Update. `SampleParticlesFromOtherEmitter` consumes its
`Emitter.SpawnParticlesAttributeReader` output in Particle Spawn; it is not an
independent local Module Reader slot. Discover actual schemas through
`niagara_search`, then use the returned read/write endpoint. The old Renderer/Asset
DI binder is not the Particle Reader authoring contract.

2026-11-05 source verification: 100 Python tests and 11 actual native regressions
passed, plus an explicit separate-process Reader cold read. The isolated standard
module pair produced 49 live CPU child particles after 45 steps, sampling displaced
moving/separated parents rather than an independent origin burst. Disabling the
parents and advancing simulation produced no further children and no residue.
Save-failure disk protection, Undo/Redo, no-op, stale/dirty/self/missing-source
refusal, explicit renamed-source mapping and active-preview failed-copy cleanup
also passed. These are bounded contract/runtime tests, not production art approval,
Low/Medium budget acceptance, general GPU/rebind support or a proof of every
possible Niagara crash. SkillTest and NF have now been built and deployed.

SkillTest's deployed DLL passed the same 11 actual regressions and explicit cold
read. Its original bound VSCode profile exercised exact Reader read/preview/no-op/
stale refusal, then created and saved a real new local Reader through grouped MCP
on a fresh `/Game/__Dev/ParticleReaderPublic_2702ddf2a08e43fea642a7b722f69343/NS_Test`.
Original test source hash, map/viewport and dirty0 remained unchanged by acceptance.
Backup/receipts: SkillTest Saved/UnrealMCPBackups and Saved/UnrealMCPDeployment,
under ParticleReader_20261105_201849. Six incremental build actions succeeded in
871.83s; its locally different Bridge retained everything outside two new routes.

NF's four intended source files matched the pre-fix baseline. An external NF restart
occupied DLLs during the first link; after a new user-approved maintenance window,
fresh dirty0 checks and normal close saved0, the four link actions succeeded in
4.79s. Original L_map01/13090 was restored. The actual original bound Claude profile
confirmed authoring/Reader-copy/removal native contracts, both Reader routes' strict
invalid-identity preflight rejection, unchanged sample hash and context/dirty0,
plus Blueprint list/read. No NF production Reader was created, rebound or deleted;
actual authoring/runtime/cold evidence is from the repository and SkillTest.
NF backup/receipts use ParticleReader_20261105_211436 under its Saved directories.

Both projects retain ports/binding/permissions and plugin customizations. Later
protection audits found external/concurrent changes: SkillTest's 40 old ArrowTrail
paths missing, and NF's four Content differences plus two T4 Blender source files.
They were preserved, not restored; full hash/addition manifests remain in deployment
receipts. Do not claim all project assets stayed unchanged throughout this interval.
NF/SkillTest Git was not managed. Existing clients reconnect; Claude trust remains
user-controlled. No commit/push is automatic.

Final runtime observation is separate from completed acceptance: an external
session later normally closed SkillTest (21:51) and restarted NF as PID43756
(21:54). NF still listened on13090, but the final bound read-only context request
returned no response; that new session's readiness was not proven. The maintenance
did not close/relaunch either again. Saved receipts mark readiness false rather
than claiming the original accepted PIDs remain active; the current owner should
restore readiness before use. This does not erase the earlier completed target
builds, native/public acceptance or recorded source/DLL identities.

## Emitter Management

### `add_emitter_to_system`
Add an emitter to a system. Three modes:
- **Template**: specify `template_name` (e.g. `Fountain`, `Smoke`). Use `list_niagara_emitter_templates` to see available templates.
- **Duplicate**: specify `source_emitter_name` within the same system.
- **Cross-system copy**: specify `source_emitter_name` + `source_asset_full_path`.

Optional `new_emitter_name` to rename the emitter after adding.

Cross-system copies refuse preexisting dirty targets. Reader dependencies must
already resolve to one exact target emitter, or supply
`reader_source_bindings=[{"source_handle_id":"<source GUID>","target_handle_id":"<target GUID>"}]`
for explicitly renamed sources. Python checks native `reader_copy_contract=1`
before sending mapped copies; old DLLs cannot silently ignore the mapping.
Only the newly copied author's current Reader objects are remapped; orphan/default
DI instances and nested merge snapshots are not mistaken for active authoring.
Remaining private source-cache references are localized to the uniquely equivalent
current target Reader after compilation. The save preflight still refuses private
source references reachable through the target's serialized object graph; removed,
unreachable Handle snapshots are not treated as active exports. No PNG/asset deletion
or unsafe global cache clear is used. Receipts include created_handle_id/new_emitter
and actual dirty state, including reference-preflight failure.

Cross-system copying now creates an **independent snapshot**, not an inheritance link
to the source System's private embedded Emitter. The native add utility still performs
graph synchronization; the newly created version's parent/previous-merge parent are
removed with the engine API before compilation. The source's enabled state is retained.
Template addition and same-system duplication keep their existing behavior.

After compilation, the target package is checked for private source-package references
before saving. Residual references (including unsupported external scratch-pad cases)
return `success=false`, `stage=reference_preflight`, `modified=true`, `saved=false` and
`private_references`; they are not passed to the fatal save path. Invalid compilation
also prevents saving. Target user-parameter values are not automatically imported or
renamed: configure required bindings explicitly. This is not a full replacement for
every Niagara editor clipboard feature, and copies no longer inherit future changes
from the source embedded emitter.

The successful cross-system receipt reports `inheritance_mode=independent_snapshot`
and actual `success`/`modified`/`saved`. Failure can leave the added emitter in memory;
inspect it before retrying, and never treat a connection reset as a harmless network
retry. Python and Bridge preserve partial failure details. Same-system and template
paths have not acquired these additional receipt guarantees.

2026-10-03 regression: `UnrealMCP.Niagara.CrossSystemEmitterCopy` passed synthetic
embedded-inheritable tests, actual save and separate-process reload. SkillTest also
passed using an isolated copy of
`/Game/Features/Enemies/E2_cannonDog/VFX/Impact/NS_CannonImpact_Normal` and its `Refraction`
layer, preserving the layer's enabled/local-space settings. It copied into a fresh
default-system fixture, NOT either existing `NS_BomberImpact_Stylized` asset. Fixture:
`/Game/__Dev/EmitterCopy_82d6ac9cfe8c4fb489b1c0199f5ff598`. Target-native test logs:
`Saved/Logs/Emitter-Material-Deployment.log` and `Emitter-Material-ColdReload.log`.
No original explosion asset was changed or failing call replayed. The UE 5.7.4
view-model clipboard and some graph helpers are not exported to plugins; this uses
exported APIs without modifying the engine or making private objects public.

Both fixes were initially built and deployed to SkillTest (UE 5.7.4). Backup:
`SkillTest/Saved/UnrealMCP-before-emitter-material-20261003-091139`. All 43 plugin
source/descriptor files matched the repository; 111 protected asset/map/config hashes
and the 8879-file existing Content inventory were unchanged. Only four fixture assets
were added under the isolated roots above and in the material deletion test. A fresh
MCP session read the copied test system and previewed material deletion with no new
dirty packages. Python regression: 67 tests passed; grouped public tools remain 70.
Existing clients must reconnect for the new material operation. Source changes are
not committed automatically.

Subsequent NF deployment on 2026-10-03 was explicitly authorized. `NodeFallEditor`
built successfully in 120.01 seconds (exit 0). Only Niagara commands, Material commands,
the Material command header and Bridge were updated; NF's independent navigation/tests,
build rules and migration implementation were preserved. Backup:
`NodeFall/Saved/UnrealMCP-before-emitter-material-20261003-092737`.
All 2374 protected file hashes, including existing Content and remaining plugin files,
were unchanged; no Content files were added. This does not deploy the separate GoodSky
texture-migration fix to NF.

SkillTest was closed with explicit permission after live checks found no dirty packages
or active task; close returned `saved_count=0`. NF then launched on its normal port
13090 without changing client configuration. At verification it was PID 39876 on
`/Game/Dev/Enemies/Maps/L_cyberpunk_start1_dev`. A fresh session using NF's existing
Claude profile discovered 62 filtered public tools, read `M_CommonPBR_Rough`, previewed
deletion with `modified=false`/`saved=false` and all 12 nodes unchanged, and read
`BP_NFPlayerController`; dirty packages stayed empty. Emitter copying was not executed
on NF assets: its behavior was verified in the prior isolated SkillTest tests.
Claude CLI still reported Pending approval; the user must approve the project server
in Claude. Fresh stdio verification does not replace client trust approval.

### `remove_emitter_from_system`
Remove an emitter by name from a system.

Requires a full `/Game` target and matching native `emitter_removal_contract=1`,
checked before mutation. `expected_handle_id` optionally guards the exact GUID
from a read/copy receipt. Default `save=true` preserves automatic saving but refuses
preexisting dirty packages; explicit `save=false` supports inspected unsaved cleanup
without persisting a prior user draft. Sources still used by another Particle Reader
are refused; remove dependents first. Existing Niagara ViewModels are reused, or a
data-processing-only ViewModel is created; native DeleteEmitters resets handles and
instances and rebuilds execution/overview graphs before compile. Receipts report
removed GUID/counts, modified/compiled/saved/dirty; failed saves retain actual state.

### `list_niagara_emitter_templates`
List all available engine emitter templates (Fountain, Smoke, etc.).

## Module Management

### `add_module_to_emitter`
Add a Niagara module script to an emitter's stack (e.g. adding GravityForce to Particle Update).

### `remove_module_from_emitter`
Remove a module from an emitter's stack. Automatically bridges pin connections to prevent stack corruption.

## Parameter Editing

### `set_niagara_rapid_parameter`
Set a rapid iteration parameter on an emitter. Key parameters:
- `asset_full_path` — identify the Niagara system
- `emitter_name` — which emitter
- `parameter_name` — full or partial rapid parameter name (e.g. `InitializeParticle.Lifetime Min`)
- `value` — new value (scalar, vector, color)
- `script_type` — required: `"spawn"`, `"update"`, `"emitter_spawn"`, or `"emitter_update"`. It must match the module's stack stage.

### `set_niagara_parameter`
Set a runtime Niagara component parameter on a placed actor. Parameters: `actor_name`, `parameter_name`, `parameter_type`, `value`.

### `get_niagara_parameters`
Get all exposed parameter values from a Niagara component on a placed actor (`actor_name`).

### `modify_emitter_properties`
Modify emitter-level properties (sim target, determinism, local space, etc.).

## Module Input Inspection & Editing

### `list_module_inputs`
List a module's input pins with their type and current value mode. Discovery step before editing inputs.
- `asset_full_path`, `emitter_name`, `module_name`, `script_type` (`"spawn"`/`"update"`/`"emitter_spawn"`/`"emitter_update"`)

Each input reports `name`, `type`, `is_static`, `is_hidden`, `can_enable_local`, `can_bind_datainterface`, `rapid_parameter_name`, and `current_mode`:
- `"Default"` — not exposed / using the module default.
- `"Local"` — has a local (rapid-iteration) value, returned in `value`.
- `"DynamicInput"` — driven by a dynamic input sub-function; a `dynamic_input` object reports its `name` (script), `node`, and local `values` (e.g. `RandomRangeVector` → `Minimum`/`Maximum`).
- `"Linked"` — bound to another parameter (`linked_parameter`).
- `"Expression"` — driven by a custom HLSL expression.

### `enable_module_input`
Expose a module input as a Local Value (creates a rapid-iteration parameter so `set_niagara_rapid_parameter` can write it). Optional `initial_value`. Constant types only (not data interfaces).

### `list_module_static_switches`
List a module's static switch pins and their current values (e.g. `Unset` / `Direct Set` / `Random`).

### `set_module_static_switch`
Set a module static switch by name. Accepts display name, raw name, or numeric index.

### `bind_module_input_datainterface`
Bind a data-interface-typed module input (e.g. a Sprite/Mesh Renderer info, Curve, or Static Mesh sampler) to an asset or renderer.

### `set_module_dynamic_input`
Attach a dynamic input (e.g. `FloatFromCurve`, `VectorFromCurve`, `RandomRangeVector`) to a module input so it's driven by a sub-function. After attaching a `...FromCurve`, call `set_ns_curve_keys` on the same input to author its curve. Only fresh inputs are supported (replacing an existing override isn't yet).

## Curves

### `read_ns_curve`
Read a curve on a module input (multi-channel supported, e.g. a Color curve's R/G/B/A). Dives through a dynamic input when present.

### `set_ns_curve_keys`
Author a curve's keys on a module input. Works on direct curve data interfaces and on `...FromCurve` dynamic inputs.

## Renderer Management

### `set_niagara_renderer_property`
Set a property on an emitter's renderer via reflection (e.g. `Material`, `SubImageSize`, `Alignment`, `FacingMode`). Select the renderer by `renderer_type` (class-name substring) or `renderer_index`.

### `add_renderer_to_emitter`
Add a renderer to an emitter. `renderer_type` is one of `Sprite`, `Mesh`, `Ribbon`, `Light`, `Decal`, `Component`. Returns `renderer_index` and `renderer_class_name`.

### `remove_renderer_from_emitter`
Remove a renderer from an emitter by `renderer_index`. Use `read_niagara_system` to see indices.

### `list_renderer_types`
List the renderer type strings accepted by `add_renderer_to_emitter`.

### `set_mesh_renderer_mesh`
Assign a static mesh and/or override material to a Mesh renderer. Provide at least one of:
- `static_mesh_path` → fills `Meshes[mesh_slot]` (optional per-slot `scale`).
- `override_material_path` → fills `OverrideMaterials[material_slot]` and enables `bOverrideMaterials`.

Selects the Mesh renderer by `renderer_index`, or the first Mesh renderer when omitted. A Mesh renderer added by `add_renderer_to_emitter` has an empty `Meshes[]` and renders nothing until a mesh is assigned.
