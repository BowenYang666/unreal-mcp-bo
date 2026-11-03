# Unreal MCP Project Tools

## Overview

Project tools provide cross-cutting functionality that isn't tied to a specific asset type — input mappings, data asset reading, and class property discovery.

Calling convention: **Asset** operations below are internal in default grouped
mode. Discover with `asset_search(tool="<operation>")`, then use the returned
read/write endpoint with `tool` and `arguments` matching `input_schema`; reuse
contracts. Examples describe operation inputs, not direct public MCP names.
`MCP_TOOL_MODE=direct` keeps original names. Project-category readers such as
`get_class_properties`, `read_data_asset` and `read_state_tree` remain direct.
See the [calling guide](README.md#grouped-mode-default).

## Project Tools

### create_input_mapping

Legacy project `UInputSettings` mapping, **not an Enhanced Input IMC row**.
For InputAction assets and Mapping Contexts use the dedicated tools below;
do not create a second Legacy mapping as an Enhanced Input workaround.

**Parameters:**
- `action_name` (string) - Name of the mapping (e.g. "Jump", "MoveForward")
- `key` (string) - Key or axis to bind (e.g. "SpaceBar", "Gamepad_FaceButton_Bottom")
- `input_type` (string, optional) - `"Action"` or `"Axis"` (default: `"Action"`)

## DataAsset Text References

Two **Asset** operations; grouped by default, original names in direct mode.
`MCP_ASSET_ENABLED=0` hides both;
read-only mode retains only `read_data_asset_text_references`. Source implementation
and isolated repository UE5.7.4 acceptance passed on 2026-11-03. **Deployed to NF
on 2026-11-03; SkillTest has not received this addition.** NF's existing T1 and
user-completed three T3 references passed actual native-identity read, same-identity
preview and stale refusal through configured stdio, without production writes.
Full authoring/Undo/cold/localization proofs remain repository-only, not NF tests.
Shared Python discovery alone does not update DLLs.
This does not authorize business text selection, translation edits or migration.

### read_data_asset_text_references

Required: canonical `/Game/...` DataAsset/PrimaryDataAsset package `asset_path`
and `property_paths`, 1..64 unique exact FText paths. For example:

```json
{"asset_path":"/Game/Data/DA_Tower","property_paths":["DisplayName","A.Name","A.Description"]}
```

Existing protected path rules apply: editable roots, ordinary non-Instanced
structs and supported existing array indices, no UObject traversal, class/CDO,
arbitrary asset type, map-key path or instanced graph editing. Each selected
field returns actual native `identity` (`kind`, read-only serialized `history`,
and `table_id`/`key` for String Table histories), display `text`, `writable` and
any condition restriction. Unsupported paths fail the request, not silent omission.
The successful selected scope has `text_reference_contract=1`, `complete=true`,
`baseline`, class/package identity and `package_dirty`. This is not a full asset export.

The baseline hashes sorted path identities, target class/package and saved-package
bytes, **not culture-dependent display strings**. Different Key with identical
source text, or another Table with the same Key/text, is distinct. Read the exact
path scope intended for the batch; re-read after saves/edits. Reads do not dirty,
compile, save, change culture or interpret serialized histories as commands.

### bind_data_asset_text_references

Required: `asset_path`, `expected_baseline` from the exact selected-path reader,
and `bindings` with 1..64 rows. Defaults: `dry_run=true`, `save=false`.

```json
{
  "asset_path":"/Game/Data/DA_Tower",
  "expected_baseline":"<reviewed exact-path baseline>",
  "bindings":[{
    "path":"DisplayName",
    "table_path":"/Game/Text/ST_Towers",
    "key":"Tower.Name",
    "expected_table_id":"/Game/Text/ST_Towers.ST_Towers",
    "expected_namespace":"Towers"
  }]
}
```

Get the actual ID, Namespace and keys from `read_string_table`; never infer them
from an asset name. Both asset paths are bounded canonical package paths, without
object suffixes. Every table must exist as a saved clean UStringTable, have the
exact expected ID/Namespace and already contain the specified Key. No auto-create,
rename, source-text/translation copying, `ImportText` or `LOCTABLE` parsing.

The whole batch is validated/staged before mutation. Dirty targets (even when
`save=false`), dirty/unsaved tables, stale baselines, duplicate paths, wrong types,
unsupported edit conditions and invalid last rows refuse before any target edit.
Requires an idle editor outside PIE/saving/GC/asset compilation; it never completes
unrelated assets. Apply uses native `FText::FromStringTable`, one transaction and
formal root-property notifications. Unspecified struct siblings remain intact;
readback checks selected identities, complete staged roots and table file hashes.
Save, when requested, affects only this DataAsset. A true identity no-op neither
modifies nor saves, even with `save=true`; equal display text alone is not a no-op.

Inspect actual `before`/`after`, `proposed_identities`, `would_modify`, `modified`,
`saved`, `package_dirty`, `dry_run`, `stage` and complete `overflow.file_path`.
Preview's `after` remains the actual original state, not a fictional applied result.
Readback/save failure can leave applied changes; receipts retain those flags and
after-state, not rollback claims. Transport timeout has UNKNOWN execution/save
state with no false `modified=false`/`saved=false` assertion and no automatic retry.
Review/read back before any further writes. Generic plain-string reads/writes are
unchanged; legitimate literal `LOCTABLE(...)` remains literal, never a command.

Verification: 89 Python tests, isolated native TextRead/TextBind plus existing
Validation/EditConditions passed, including three fields, same-display identity
conflicts, invalid-last-row atomic refusal, dirty/type/table/key/namespace guards,
Undo/Redo, no-op and private save-failure injection (disk unchanged, memory edited).
The saved fixture passed independent cold baseline/sibling checks and real native
Gather -> PO Import -> locres Compile -> en/zh-Hans/en FText switching, with the
plugin enabled. Language validation explicitly loads fixture locres, not business
localization configuration or manual font/UI acceptance. The existing
[localization harness](../../Python/scripts/localization/verify_string_table_localization.py)
supports `--asset-text --verify-language --root /Game/__Dev/AssetText_<unique>`.
Fresh public stdio on a new repository __Dev copy verified preview, native saved
three-field binding, invalid batch/stale refusal and identity no-op, source/config
hashes unchanged and zero dirty packages. No NF T1/T2/T3 or translation was edited.

## Animation Assets

Three **Asset** operations; grouped by default, original names in direct mode.
`MCP_ASSET_ENABLED=0` hides all
three; read-only mode retains only `read_animation_asset`. Requires the matching
native plugin, including the AnimationBlueprintLibrary module dependency.
**Deployed to SkillTest and NF on 2026-11-03, UE5.7.4.** SkillTest's native
isolated authoring/playback/cold tests and configured public smoke passed; NF's
transient reader plus complete four-asset read/preview/refusal smoke passed
without production writes. Python schema visibility alone does not update DLLs.

### read_animation_asset

Required: exact `/Game/...` package `asset_path` of an existing AnimSequence or
AnimMontage (not derived assets, redirectors, object paths or Windows paths).
Returns `animation_contract=1`, object/class path, Skeleton identity, length,
RateScale, Sequence Additive fields/source DataModel GUID/sampling bounds, and
Montage ordered slots/segments/sections/blend settings. This is a stored
**authoring scope**, not a full raw animation reconstruction/export contract.

`notify_tracks` retains native index/name/color. Ordered `notifies` retain index,
GUID, exact track, authored/actual trigger/end times, offsets, link information,
duration, filtering/probability/weight/dedicated-server/follower flags, and actual
Notify/NotifyState class/path/outer/typed fields. Referenced Sound/Niagara assets
are references, while the notify objects' own configuration is expanded.
`MontageTickType` is stored data; `effective_branching_point` also considers the
class's native forcing flag. Changing the stored field cannot override that flag.

`complete=false` is explicit partial data, never an empty-notify inference;
`baseline=null` prevents authoring. Unknown native child data without opt-in,
unsupported map/set/static array/storage, nonfinite/lossy values and bounds are
reported in `unsupported` (128 details max) with full `unsupported_count`.
Bounds: 4096 array/events, 256 tracks/expanded child objects, 50000 reflected
values, depth limits and bounded strings. The baseline includes the selected
authoring scope, source/reference Sequence data identity and related Skeleton
identity; it excludes transient/deprecated runtime caches and dirty flags.
Indices are not stable event identities. Read full `overflow.file_path` before
reviewing large snapshots; the file retains complete responses and failure flags.
Reads do not play, scrub, dirty or save, and do not wait for global compilation.

### set_anim_sequence_additive

Required: `asset_path`, `expected_baseline`, `settings`. Optional:
`dry_run=true`, `save=false`. Only these fields may be patched:
`AdditiveAnimType`, `RefPoseType`, `RefPoseSeq`, `RefFrameIndex`.

```json
{
  "asset_path": "/Game/Animations/A_Recoil",
  "expected_baseline": "<complete reviewed baseline>",
  "settings": {
    "AdditiveAnimType": "AAT_LocalSpaceBase",
    "RefPoseType": "ABPT_LocalAnimFrame",
    "RefPoseSeq": null,
    "RefFrameIndex": 0
  }
}
```

This example does not select an artistically correct neutral pose. The author
must approve the strategy/frame/reference. Omitted fields are retained and the
whole merged combination is validated. Enums use exact native symbolic names;
`RefPoseSeq` is null or an existing saved non-additive `/Game` Sequence with the
**same exact Skeleton** (not arbitrary compatible-skeleton/retarget resolution).
Self references, dirty references/Skeleton, invalid ranges/combinations and
implicit engine clamping/clearing refuse. LocalAnimFrame samples this Sequence;
AnimFrame samples the reference; AnimScaled uses the reference's normalized time.
Explicitly clear an obsolete reference instead of silently losing it.

Preview creates no temporary animation and does not invalidate source caches.
Apply uses one transaction, native property-change notifications and target-only
derived-data completion, then full scope readback before optional target save.
Source bone/curve data identity, existing events and unrelated selected settings
are preserved. Same settings are a no-op, including with `save=true`.
Writers conservatively require an idle asset compilation queue before starting;
they do not finish unrelated assets. After `save=false`, explicitly review/save
the draft separately; a later writer call refuses an already-dirty target.

### add_montage_native_notify

Required: `asset_path`, `expected_baseline`, `notify_class`, `event_guid`,
`time_seconds`, `track`, `tick_type`. Optional: `event_settings={}`,
`dry_run=true`, `save=false`.

```json
{
  "asset_path": "/Game/Animations/AM_Attack",
  "expected_baseline": "<complete reviewed baseline>",
  "notify_class": "/Script/MyGame.AnimNotify_Release",
  "event_guid": "aa7e2aaa-227a-4a2e-8ff1-4ad340e0a837",
  "time_seconds": 0.5,
  "track": {"index": 0, "name": "1"},
  "tick_type": "BranchingPoint"
}
```

The class path is an example, **not an existing NF class**. The project must
compile/register its real native subclass first. The finite allowlist extension
is explicit C++ class metadata `MCPAnimationNotifyAuthoringSafe="true"` on a
nonabstract native UAnimNotify class with no additional saved fields beyond
UAnimNotify. Native authors attest that no unreflected stored state is omitted.
No arbitrary class load, Blueprint classes/construction/Notify execution, CDO
mutation, NotifyState writer or generic instanced-object editor is offered.
The plugin's MCPAnimationTestNotify is an editor test fixture, NOT a game release
implementation or a class to integrate into NF. Typed custom-field support needs
its own reviewed extension rather than adding arbitrary properties to this API.

`track` is exactly `{index:int,name:str}` from the reader; duplicate names refuse,
and tracks are never created. Grouping the identity preserves numeric-looking
names such as `"1"` under the current MCP library's top-level JSON pre-parser.
Time must fit the native float timeline in `[0,length)`, without implicit clamping.
`tick_type` is explicitly `Queued` or `BranchingPoint`; a native-forced branching
class refuses Queued. `event_settings` allows only TriggerWeightThreshold,
NotifyTriggerChance, NotifyFilterType, NotifyFilterLOD, bCanBeFilteredViaRequest,
bTriggerOnDedicatedServer and bTriggerOnFollower with typed/range validation.

Generate a new hyphenated UUID once, review it in preview, and keep it for readback:
`event_guid` becomes the actual stored event GUID. An existing GUID is explicitly
**refused**, never replayed/no-op guessed from class/time; old missing/ambiguous
GUIDs also refuse. Multiple legitimate events of the same class are not merged.
Preview reads the existing CDO only and creates no child; `proposed.object_path`
is pending, not a fictional saved instance. Apply uses the native library to
create an owned transactional child, Link/offset/track/cache updates, applies
the confirmed GUID/event fields, and verifies all prior events and their
relative order/ownership/configuration plus slots/segments/sections/blends.
If native sorting would reorder old events, preflight refuses rather than
normalizing existing author data. Resulting indices may shift; use GUIDs.

Both writers default to preview/no save, reject dirty/stale/incomplete targets,
and save ONLY the reviewed target after readback. Inspect `before`, `proposed`,
`after`, `modified`, `saved`, `package_dirty`, `stage` and complete spill files.
Readback/save failure can leave applied changes, not rollback. Timeout means
UNKNOWN execution state; no automatic retry, Save All, runtime injection, PIE,
Skeleton changes, imports, retargeting or gameplay decisions.

Verification: isolated native reader/Additive/Notify, actual delta pose sampling,
FAnimMontageInstance branching dispatch/instance payload, old Sound/Niagara
configuration, Undo/Redo, failure/negative paths and independent full cold load
passed. Public stdio read/preview/save/refusal/no-op workflows passed on new
repository __Dev copies. This does not validate NF's Cannon class, release time,
muzzle/target snapshot/cooldown behavior or game integration.

## Enhanced Input Assets

Two **Asset** operations; grouped by default, original names in direct mode.
`MCP_ASSET_ENABLED=0` hides both;
read-only mode keeps only `read_input_mapping_context`. Requires the matching
native plugin and its EnhancedInput dependency. Repository UE5.7.4 native and
public stdio acceptance passed. **Deployed to NF on 2026-11-02**: native reader,
public complete read/default five-row preview/stale refusal and Blueprint smoke
passed without saving NF mappings in that deployment. SkillTest received this
addition on 2026-11-03; native read/append/independent cold and public read passed.
Python visibility is not proof that another editor supports it.

### read_input_mapping_context

Required: exact `/Game/...` package `asset_path`, e.g.
`/Game/Features/UI/Input/IMC_UI_Menu`, without `.AssetName` or `.uasset`.
Overlong paths and redirected/noncanonical resolved package paths are refused.
Returns `mapping_contract=1`, object/class paths, default rows in native order,
actual `profile_overrides` (including empty overrides), Action settings,
context settings, `package_dirty`, `complete`, `unsupported` and `baseline`.

Each row contains native `scope` and `index`, Action object path, internal key
and validity, ordered triggers/modifiers with class/path/outer/typed fields,
raw `setting_behavior`, and raw **owned** Mappable settings. Referenced objects
may use `reference_only`; their first expansion carries fields. `actions`
separately exposes ValueType, consumption/pause flags, inherited triggers,
modifiers and mappable settings. Empty mapping-local arrays do not mean the
Action has no behavior. Profile identifiers are sorted; rows are not reordered.

The baseline covers defaults, profiles, child object ownership/values, context
and referenced Action settings, excluding deprecated/transient fields. Indices
are baseline-relative, not stable GUIDs. `success=true` is a successful query,
not a completeness claim. Unsupported fields/custom child classes are partial;
`complete=false` returns `baseline=null` and blocks the writer. Limits: 4096
total rows, 1024 profiles, 256 expanded child/Action objects, 256 triggers or
modifiers per row, 50000 reflected values, bounded recursion and string budget.
`unsupported_count` is the full count; detailed errors are capped at 128.
Oversized replies preserve **full JSON** at `overflow.file_path`; read that
file, not just its preview. Reads do not dirty/save, resolve an active Profile,
evaluate runtime input routing or apply contexts to LocalPlayers.

### append_input_mapping_context_mappings

Required: `asset_path`, `expected_baseline` from a complete reviewed read,
`mappings` (1..64 exact `{action_path,key}` objects). Optional:
`dry_run=true`, `save=false`. Only the **default** scope is supported.

```json
{
  "asset_path": "/Game/Features/UI/Input/IMC_UI_Menu",
  "expected_baseline": "<complete reader baseline>",
  "mappings": [
    {"action_path": "/Game/Features/UI/Input/IA_UI_Back", "key": "Escape"},
    {"action_path": "/Game/Features/UI/Input/IA_UI_Back", "key": "Gamepad_FaceButton_Right"},
    {"action_path": "/Game/Features/UI/Input/IA_UI_Accept", "key": "Enter"},
    {"action_path": "/Game/Features/UI/Input/IA_UI_Accept", "key": "SpaceBar"},
    {"action_path": "/Game/Features/UI/Input/IA_UI_Accept", "key": "Gamepad_FaceButton_Bottom"}
  ]
}
```

All Action paths must resolve to saved InputActions; keys must be valid,
canonical internal FKey names (no labels, deprecated keys or AnyKey). Entire
key names are length-checked before FName construction; Action package paths
must be canonical, not redirector aliases. Entire
batch preflight rejects missing/stale baselines, incomplete snapshots, dirty
IMCs/requested Actions, extra fields, invalid rows and duplicate request pairs.
One uniquely equivalent existing simple pair is a no-op; multiple exact old
candidates or a complex same-pair row refuse, never select the first or append
a duplicate. Same Action/different keys and different Actions/same key are valid.

`before`, `proposed`, `after`, `would_modify`, `modified`, `saved`,
`package_dirty`, added/existing indices report actual execution state. Preview
does not mutate or save. Applying appends in request order through UE5.7's
formal `DefaultKeyMappings`, one Undo/Redo transaction; old row order, objects,
values, all profile overrides and context settings are retained. New rows have
no mapping-local Trigger/Modifier/owned Mappable and inherit Action settings.
No deprecated Mappings fallback or clear/replace/complex/Profile writer exists.
Readback verifies the full proposal before optional **target-only** saving.
Already-present batches do not dirty or save, even with `save=true`.

Save/readback failure may leave applied changes: inspect `stage`, `after`,
`modified/saved` and dirty state, including full spill files. No rollback or
automatic retry is promised; timeout/connection loss means **unknown** state,
not cancellation. This does not change Actions, project InputSettings, user
mapping saves, Profile/input modes, LocalPlayer contexts, PIE or gameplay IMCs.
It does not call runtime `MapKey`/mapping rebuild. General DataAsset instanced
and EditCondition restrictions remain unchanged. After `save=false`, explicitly
review/save the draft separately; the writer refuses already-dirty assets.

Five-row ordering/cold persistence, duplicate/no-op/ambiguity, invalid batch,
dirty/stale refusal, instanced object/Profile preservation, Undo/Redo and
injected save failure are covered by isolated native tests. Actual public
stdio creation/preview/save/no-op/refusal and independent cold read passed.
CommonUI pause/Back/Accept routing and menu-context removal remain game-side
integration tests, not proven by saved rows.

## Project Inspection

### read_data_asset

Read all properties from a DataAsset (or any UObject asset) via Unreal reflection.

**Parameters:**
- `asset_path` (string) - Asset path, e.g. "/Game/Data/TowerConfig/DA_HoneyBarrel"

**Returns:**
- Dict with `asset_name`, `asset_path`, `class_name`, and `properties` object containing all BlueprintVisible UPROPERTY fields.

**Example:**
```json
{
  "command": "read_data_asset",
  "params": {
    "asset_path": "/Game/Data/EnemyConfig/DA_Goblin"
  }
}
```

### get_class_properties

Discover reflected properties of any resolved UClass or loaded UObject asset. Transient/deprecated fields are skipped.

Provide either `class_name` or `asset_path`:
- `class_name` returns property metadata plus class-default-object (CDO) values
- `asset_path` loads the asset and also returns current values

**Parameters:**
- `class_name` (string, optional) - UClass name, e.g. "BlendSpace1D", "StaticMeshActor", "PlayerController"
- `asset_path` (string, optional) - Asset path to load and inspect, e.g. "/Game/Player/Animations/BS_Locomotion"
- `category` (string, optional) - Filter to only return properties in this category (e.g. "Physics", "Rendering")
- `structured` (boolean, default false) - Opt into the DataAsset write contract described below. Requires an existing `/Game/...` DataAsset instance, not `class_name`.
- `property_paths` (array of strings, optional) - Exact structured paths, e.g. `["Stats.Health", "Attacks[0]"]`; requires `structured=true`. At most 64 selections. Omit to inspect top-level editable fields. When provided, this selection takes precedence over `category`.

**Returns:**
- `class` - Class name
- `parent_class` - Parent class name
- `asset_path` - (only when asset_path was provided)
- `property_count` - Number of properties returned
- `properties` - Array of property objects, each containing:
  - `name` - Property name
  - `type` - Human-readable type string (e.g. "float", "enum (ECollisionChannel)", "struct (FVector)", "array (object (UStaticMesh))")
  - `category` - Editor category
  - `editable` - Whether the property is editable
  - `tooltip` - Editor tooltip text (if available)
  - `defined_in` - Which class in the hierarchy defines this property
  - `value` - Current value (only when asset_path was provided)

**Examples:**

Discover all properties of a class:
```json
{
  "command": "get_class_properties",
  "params": {
    "class_name": "BlendSpace1D"
  }
}
```

Inspect a specific asset with current values:
```json
{
  "command": "get_class_properties",
  "params": {
    "asset_path": "/Game/Player/Animations/BS_MyLocomotion1d"
  }
}
```

Filter by category:
```json
{
  "command": "get_class_properties",
  "params": {
    "asset_path": "/Game/Player/Animations/BS_MyLocomotion1d",
    "category": "InputInterpolation"
  }
}
```

## String Table Assets

These four operations belong to the **Asset** category: grouped by default,
original names directly exposed in direct mode.
`MCP_ASSET_ENABLED=0` hides all four; read-only mode keeps only `read_string_table`.
Requires the matching native plugin, not just a Python restart. Asset paths are
`/Game/...` package paths without `.AssetName` suffixes. Reflected UObject reads
cannot expose String Table contents; use the specialized reader.

### create_string_table

Required: `asset_path`, `namespace`. Optional: `dry_run=true`, `save=false`.
Creates an empty table with its Namespace; an existing asset/package is always
refused, never overwritten. Preview does not create an asset. Apply with
`dry_run=false`; explicitly opt into `save=true` to save this asset only.

### read_string_table

Required: `asset_path`. Returns reusable `asset_path`, full `object_path`, actual
registered `table_id`, `namespace`, `entry_count`, `package_dirty`, and **all**
`entries`, each containing `key`, `source_string`, and `metadata`. No mutation or save.
The returned strings are source text, not the current culture's translations.

### update_string_table_entries

Required: `asset_path`, `expected_namespace`. Optional: `entries=[]`,
`delete_keys=[]`, `dry_run=true`, `save=false`.

```json
{
  "asset_path": "/Game/Data/Localization/Text/ST_UI_Status",
  "expected_namespace": "ST_UI_Status",
  "entries": [
    {"key": "Status.Slowed", "source_string": "Slowed", "metadata": {"Comment": "HUD label"}},
    {"key": "Status.Stunned", "source_string": "Stunned"},
    {"key": "Format.ContributionCount", "source_string": "{Status} x{Count}"}
  ],
  "dry_run": true
}
```

Unspecified entries and metadata are retained. New keys require `source_string`;
existing keys may have metadata-only edits. Metadata values are strings, or JSON
null to explicitly remove that named field. Whole-entry deletion must use
`delete_keys`; duplicate keys and update/delete overlaps reject the whole batch.
No Namespace or Key rename API is provided. `expected_namespace` must match,
and source changes may still require translation review after the next Gather.

### import_string_table_csv

Required: `asset_path`, `expected_namespace`, absolute `csv_path`, and explicit
`mode="merge"` or `"replace"`. Optional: `allow_delete=false`, `dry_run=true`,
`save=false`. Create the table separately first.

```csv
Key,SourceString,Comment
Status.Slowed,Slowed,HUD label
Status.Stunned,Stunned,HUD label
Format.ContributionCount,{Status} x{Count},Named format placeholders
```

The CSV is validated, frozen to a temporary snapshot, and imported using UE's
native `FStringTable::ImportStrings` on a temporary table before live mutation.
Malformed quotes/rows, duplicate decoded keys or metadata headers, invalid names,
or wrong parameter types reject before editing. The input file is never rewritten.
Limits: CSV 1 MiB, 4096 rows/final entries, 65536 characters per cell, bounded keys
and metadata names. Complete reads are not reduced to counts.

`merge` updates imported sources and nonempty imported metadata, retaining other
keys/metadata; native empty metadata cells are ignored. `replace` uses only the
CSV keys and metadata, removing omitted data. Preview reports exact
`added_keys`, `updated_keys`, `deleted_keys`; apply that deletes any key requires
`allow_delete=true`. Replacement can also remove metadata: review the documented
replace semantics. There is no implicit identity migration.

All CSV columns after `Key`/`SourceString` are **metadata, not languages**.
Keep the English source here. Chinese translations belong in PO Import/Compile;
do not add a Chinese language column to this table.

### Write Safety And Verification

Apply refuses a pre-existing dirty table even with `save=false`; save or otherwise
resolve those user edits separately. One editor transaction supports Undo/Redo.
No Save All or implicit compile. Validation failures do not partially edit; save
or readback failures report `success=false`, actual `modified`/`saved` and `after`
state. Unsaved applied changes may remain after a save failure; inspect before
retrying. Transport timeout/reset is unknown outcome, never an automatic retry.

2026-10-28: 76 Python regressions passed. Repository, SkillTest and NF verified
read/write, dirty/identity/duplicate protections, native CSV merge/replace, metadata,
Undo and controlled save failure. A saved Widget Blueprint TextBlock contains a
real String Table `FText` reference. With UnrealMCP enabled, actual Gather,
PO Import and Compile each exit 0; a fresh process reads Namespace, all three
source entries and metadata, then verifies en -> zh-Hans -> en text/format changes.
English sources remain unchanged. The language test explicitly loads the compiled
fixture locres; it does not install a business localization target or claim manual UI acceptance.

The runnable isolated harness is
[verify_string_table_localization.py](../../Python/scripts/localization/verify_string_table_localization.py).
It requires `polib` (run with `uv run --with polib`) and an existing unique
`/Game/__Dev/StringTables_...` fixture; outputs are under project Saved, not business PO paths.

The reported `NativeGameplayTags.cpp` ensure was traced to NF's **local** test
customization, not the current repository module: an Editor-module static
`UE_DEFINE_GAMEPLAY_TAG_STATIC`. That definition was removed in the authorized NF
update; the positive tag test now loads a Saved ini only inside test execution and
cleans it up. Existing test coverage and other NF customizations were retained.
NF Gather/Import/Compile with the plugin enabled now exit 0 without that ensure;
no `-DisablePlugins=UnrealMCP`, log suppression or driver/RHI change is used.

## AI / Asset Reading

### Structured Asset Inspection

Legacy `get_class_properties` output is unchanged unless `structured=true`.
Structured output contains `asset_path`, `class_path`, `package_dirty` and
`properties` entries with `path`, `writable`, typed `value`, and a `schema` with
C++ type, reference class constraints, enum choices and numeric metadata.
Unsupported/noneditable values have `writable=false` and a `reason`. A writable
field is not a promise that every proposed value is valid or that saving will
succeed. `EditDefaultsOnly` fields are allowed on these DataAsset assets; this
does not enable editing Actor instances or class-default objects. The schema
reports `edit_defaults_only`. Additional reference-picker constraints remain
conservatively refused, and the implementation respects `CanEditChange`.

`EditCondition` supports a sibling boolean property or its negation, such as
`bEnabled` or `!bEnabled`, resolved in the containing UObject/struct, including
array-element structs. Parent conditions apply to nested paths too. False or
unsupported conditions report `writable=false` with a reason, while retaining
the typed value/schema when otherwise readable. Complex expressions (comparisons,
logical combinations, function calls and dotted references) fail closed; this
is not a full implementation of the UE Details expression language.

```json
{
  "asset_path": "/Game/Data/DA_Enemy",
  "structured": true,
  "property_paths": ["Stats.Health", "AttackMontage"]
}
```

This opt-in contract supports DataAsset instances and the allowlisted fields of
exact `UPhysicalMaterial` assets. It is not an arbitrary UObject writer.
This reader does not follow object references or mutate the target. Bounds:
8 levels, 1024 elements per array/map, 8192 visited values per request. Select a
narrower path when a large value cannot be represented. Legacy reflection reads
remain available for other asset classes and unsupported DataAsset fields.

### set_asset_properties

Patch one existing `UDataAsset`/`UPrimaryDataAsset` instance, including instances
of Blueprint-derived DataAsset classes, or allowlisted `UPhysicalMaterial` fields.
This is an **Asset-category** operation, grouped by default and directly exposed in direct mode,
in both MCP modes (`MCP_ASSET_ENABLED`), not part of the Project category. It is
removed in read-only mode. Requires the updated C++ plugin; restart alone does
not add the native command to an older deployed DLL.

Parameters: `asset_path: string`, `changes: array of objects`, `save: boolean=true`.
Use a full `/Game/...` package path, not a Windows path, object suffix, class or CDO.
Each change has `path`, `value`, and optional `expected_value`; unknown keys and
overlapping paths are rejected. Exact reflected field names are case-sensitive.

```json
{
  "asset_path": "/Game/Data/DA_Enemy",
  "changes": [
    {"path": "Stats.Health", "value": 150, "expected_value": 100},
    {"path": "Attacks[0].Damage", "value": 25},
    {"path": "AttackMontage", "value": "/Game/Animations/AM_Attack.AM_Attack"}
  ],
  "save": true
}
```

Supported values and boundaries:

- Boolean, numeric, string, name, plain text, declared enum names, ordinary
  reflected structs, arrays, hard/soft object or class references and GameplayTags.
  Numeric values must be finite and obey the property's storage range and
  `ClampMin`/`ClampMax`; integers are restricted to JSON's safe range
  `[-9007199254740991, 9007199254740991]`. No silent integer truncation.
- A struct value replaces all its fields and must supply every field. Use a
  dotted path for partial edits. An array value replaces the array; `[index]`
  edits an existing element. No append/remove operations in v1. No traversal
  through object references; editing a reference never edits the referenced asset.
- Maps use a complete array of `{"key": typed_key, "value": typed_value}` entries.
  `[]` clears the map; single-key property paths are not supported. Hashable enum,
  integer, name and string keys are supported; duplicate typed keys are rejected.
  `expected_value` compares typed map contents independent of entry order.
  Whole struct values include all inherited reflected fields, not only fields
  declared in the derived struct. Schema uses `format="map_entries"`, `key` and
  `value`; enum choices include stable names and separate `enum_display_names`.
- Reference strings must resolve to compatible assets/classes, including soft
  references. Class references use `/Script/Module.Class` or a Blueprint generated
  class path such as `/Game/Data/BP_Config.BP_Config_C`, not the Blueprint asset.
  `null` explicitly clears nullable references; empty string is not a clear token.
- GameplayTag uses a registered tag name (empty string clears); TagContainer
  uses an array of registered names (empty array clears). No tag registration.
- FText writes use plain strings and do not preserve localization identity.
  For true native String Table histories use the separate
  [DataAsset Text References](#dataasset-text-references) contract above.
  Sets, instanced objects, delegates, static C arrays, unsupported conditions and
  unsupported nested structures are rejected. Texture/Mesh/Material/Blueprint
  graph editing, MI Parent and class reparenting are excluded. Creation uses the
  separate bounded tools below.
- 1..64 patches; Python JSON request limit 256 KiB; reflected traversal bounds
  are the same as the structured reader. Underlying native TCP accepts at most
  1 MiB per request; this is not an unlimited bulk asset API.

All changes and expected values are converted/validated in temporary property
storage before touching the target. A mismatch returns stage `conflict`, with
`modified=false` and `saved=false`. This is not an asset-wide revision lock.
Supported edit conditions are checked against the complete proposed batch after
conversion, independent of patch order. For example, if enabling a feature is
explicitly authorized, both `BulletHits.bEnabled=true` and
`BulletHits.HeadRootBones=["head"]` may be submitted together. Without that
explicit enabling change, a false condition rejects the write with stage
`edit_condition`; the tool never toggles a controller automatically. A batch
that disables the condition and edits its guarded value is also rejected.
Whole-struct/array/map replacement checks changed descendants, so it cannot bypass
these rules; unchanged guarded values can be retained while disabling a feature.
An entirely new struct-array element has no previous values and checks all its
guarded fields. Read-time writability reflects current state, not every possible
future batch; use exact child paths to inspect or patch controllers separately.
The game-thread edit uses a transaction and root-property editor notifications;
project-specific edit hooks can change actual values, which are read back into
the response. No claim of rollback across arbitrary project hooks is made.

With `save=true`, a pre-existing dirty target package is rejected before writes,
so unrelated user edits are not silently saved. With `save=false`, edits remain
in memory; use existing `save_asset` explicitly when ready (it saves the package,
including all of its existing edits). Only the target is automatically saved.

Result: `success`, `modified`, `saved`, `asset_path`, `package_dirty`, and `changes`
with typed `before`/`after` values. Validation failure leaves the target unchanged.
Save/readback failure may leave modified state; inspect `stage` and `error` or
the Python `message`. Save success is checked against the save API, dirty flag
and package file existence. This is not a forced package reload inside the call.
A transport timeout is **indeterminate**: do not blindly retry a mutation.

Acceptance in the source test project covers C++ and Blueprint DataAsset
instances, batch prevalidation, conflict/type/range rejection, references/tags,
dirty protection, save and fresh-process reload. Test assets live under
`/Game/__Dev/AssetProperties_*`. Native tests can run with `-UnrealMCPNoServer`
to avoid competing with another editor's MCP listener. These are not a claim
that SkillTest or NF already has the new plugin.

### create_data_asset

Create a new instance of an existing concrete DataAsset-derived class. Parameters:
`asset_path: string`, `class_path: string`, `save: boolean=true`. Destination must
be a new `/Game/...` package, including the asset name. Existing loaded, unsaved,
registered or on-disk packages are refused; no overwrite or automatic suffixing.
The class must derive from `UDataAsset` and cannot be abstract, deprecated or
superseded. This does not create a class or permit arbitrary UObject creation.

```python
create_data_asset(asset_path="/Game/Data/DA_Impact",
          class_path="/Script/NodeFall.NFWeaponImpactProfile")
get_class_properties(asset_path="/Game/Data/DA_Impact", structured=True,
           property_paths=["Default", "Surfaces"])
```

Native class paths use `/Script/Module.Class`; Blueprint-derived classes use
their generated class path, e.g. `/Game/Data/BP_Profile.BP_Profile_C`.
Creation copies class defaults; inspect then use `set_asset_properties` for
separately validated initialization. Returns `created`, `modified`, `saved`,
`package_dirty`, `asset_path`, `class_path` and `success`. Only the new package is
saved. A save failure can leave a created asset in memory; a timeout is unknown,
not permission to repeat creation or overwrite the destination.

For an enum-keyed impact map, copy the complete inspected `Default` structure
for each entry, modify the desired fields, then submit the complete entries as
`Surfaces`. Keep every inherited/nested field from the readback. Use JSON `null`
for disabled System, Sound or Decal.Material references, never a missing field.
For NF, `SurfaceType1` and `SurfaceType2` are stable enum names; Metal and Concrete
are project display labels, not accepted replacement identifiers.

### create_physical_material

Parameters: `asset_path: string`, `save: boolean=true`. Creates exactly
`UPhysicalMaterial`, using engine defaults and the same no-overwrite/save rules
as `create_data_asset`. Both creation tools belong to the Asset category and are
unavailable in read-only mode.

```python
create_physical_material(asset_path="/Game/Physics/PM_Metal")
get_class_properties(asset_path="/Game/Physics/PM_Metal", structured=True,
           property_paths=["SurfaceType", "Friction"])
set_asset_properties(asset_path="/Game/Physics/PM_Metal", changes=[
  {"path": "SurfaceType", "value": "SurfaceType1",
   "expected_value": "SurfaceType_Default"}])
```

The exact editable whitelist is `SurfaceType`, `Friction`, `StaticFriction`,
`Restitution`, `Density`, `FrictionCombineMode`, `RestitutionCombineMode`,
`bOverrideFrictionCombineMode`, `bOverrideRestitutionCombineMode`. Omitted fields
are preserved. Numeric and edit-condition constraints still apply. This does
not rebuild mass/inertia or edit mesh BodySetup/collision-complexity settings.

Assign the asset through [Material/MI assignment](material_tools.md#set_material_physical_material),
[level component overrides](editor_tools.md#set_component_physical_material) or
the existing [Blueprint setter](blueprint_tools.md#set_component_property).
Validate collision with [trace_physical_material](editor_tools.md#trace_physical_material).
Deferred decals use existing `create_material(material_domain="DeferredDecal",
blend_mode="Translucent")`, not a separate bullet-decal tool.

The source project's `UnrealMCP.SurfaceFeedback.EndToEnd` test creates isolated
assets and uses real simple/complex collision queries. Supply a unique
`-MCPSurfaceTestRoot=/Game/__Dev/SurfaceFeedback_<unique>`; a second process with
the same root and `-MCPSurfaceReload` verifies saved references and surface types
without writing. Both runs use `-UnrealMCPNoServer`. The optional
`-MCPImpactProfileClass=/Script/NodeFall.NFWeaponImpactProfile` requires that actual
class and the matching plugin in the target project. Fixture coverage in this
repository is not acceptance of the real NF class or authorization to deploy.

### read_behavior_tree

Read the full structure of a Behavior Tree asset (root, composite/decorator/service/task nodes, hierarchy).

**Parameters:**
- `asset_path` (string) - Behavior Tree asset path, e.g. `/Game/AI/BT_Enemy`

### read_blackboard

Read all keys from a Blackboard data asset (key names, types, parent blackboard).

**Parameters:**
- `asset_path` (string) - Blackboard asset path, e.g. `/Game/AI/BB_Enemy`

### read_state_tree

Read the full structure of a StateTree asset (states, tasks, transitions, evaluators).

**Parameters:**
- `asset_path` (string) - StateTree asset path, e.g. `/Game/AI/ST_Enemy`
