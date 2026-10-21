# UMG Tools

Tools for creating and editing UMG Widget Blueprints (HUDs, menus, in-game UI) programmatically.

Default grouped mode exposes `umg_search`, `umg_call_read` and `umg_call_write`.
The operation names/parameters below are passed to these entry points, or called
directly with `MCP_TOOL_MODE=direct`.
See the [calling guide](README.md#grouped-mode-default).

Most tools identify the target widget blueprint by its content `path` (e.g. `/Game/Widgets/WBP_HUD`). Child widgets are addressed by name, and can be parented to a layout container via `parent_name` (defaults to the root Canvas Panel).

## Blueprint Management

### `create_umg_widget_blueprint`
Create a new UMG Widget Blueprint.
- `widget_name`, `parent_class` (optional, defaults to `UserWidget`), `path` (default `/Game/Widgets`)

### `add_widget_to_viewport`
Add a Widget Blueprint instance to the viewport at an optional `z_order`.

### `read_widget_layout`
Read the full widget tree layout (hierarchy, types, slot properties) of a Widget Blueprint. Discovery step before editing.
Each node also includes `placement`: original UObject path, parent, index and
structured `slot` (`class_path`, `properties`, `schema`). Existing `slot` output remains available.

### `reparent_widget`
Move the **original Widget object**, or reorder it within its parent. Names,
fonts, brush/texture references, property bindings and Widget animation references
are not recreated. Requires the updated native plugin; Python discovery alone
does not update a running editor's DLL.

- Required: `path`, `widget_name`, `new_parent_name`, `insert_index`.
- Optional: `slot_properties` (reflected layout fields), `dry_run=true`.
- Exact local WidgetTree names only. Root, inherited/nested-tree moves and PIE are refused.
- Supported native source/target panels: CanvasPanel, HorizontalBox, VerticalBox,
	Overlay, SizeBox, Border. Occupied single-child containers are never replaced.
- `insert_index` is the final zero-based index **after removal**. Same-parent:
	`0..count-1`; cross-parent: `0..target_count` (last value appends). No clamping.
- Same-parent moves preserve the Slot. Cross-parent moves create the target's
	correct Slot type; same Slot classes preserve layout, different types use native defaults.
- Cross-parent moves with Slot animation bindings are rejected before mutation.
	Widget animation bindings are preserved; Slot-track conversion is not implemented.
- `slot_properties` uses names and typed values from `placement.slot.schema` or
	the dry-run receipt's `requested.slot.schema`. Canvas supports `LayoutData`,
	`bAutoSize`, `ZOrder`; boxes support `Padding`, `Size`, `HorizontalAlignment`,
	`VerticalAlignment`; Overlay/SizeBox/Border support padding and alignments.
	Structs must include every field in the schema, not partial/legacy lowercase values.
- Cycles, capacity, index and layout values are checked on a temporary Slot before
	removal. Apply failures attempt restoration and report `rolled_back`, actual
	`after` placement and `modified`; never assume rollback succeeded after an error.

```text
umg_search(tool="reparent_widget")
umg_call_write(tool="reparent_widget", arguments={
	"path": "/Game/UI/WBP_Key", "widget_name": "KeyText",
	"new_parent_name": "KeyOverlay", "insert_index": 2,
	"slot_properties": {"HorizontalAlignment": "HAlign_Center", "VerticalAlignment": "VAlign_Center"},
	"dry_run": true
})
```

Review `before`, `requested`, `after`, `would_modify`, `modified`, `saved` and
`package_dirty`, then explicitly repeat with `dry_run=false`. One Editor transaction
supports Undo/Redo; no Scene ticket, implicit save or explicit full compile.
Compile/check and `save_asset` separately. Timeout means unknown outcome: read
layout before retrying. `wrap_widgets` is not implemented.

## Designer Preview

### `get_widget_preview_settings`
Required: `path`. Reads the compiled UserWidget CDO's editor-only
`DesignSizeMode` and `DesignTimeSize`, returning `settings` and `supported_modes`
without dirtying, compiling or saving.

### `set_widget_preview_settings`
Required: `path`, `mode`; optional `size=[width,height]`, `dry_run=true`.
Modes: `FillScreen`, `Custom`, `CustomOnScreen`, `Desired`, `DesiredOnScreen`.
Size is accepted only for Custom/CustomOnScreen, with finite positive dimensions
up to 100000 Designer units; omitting it preserves stored custom dimensions.
Apply with `dry_run=false` uses one undoable transaction and Blueprint change
notification to refresh the Designer. Does not change runtime slots, anchors,
font sizes or SizeBox overrides, and never compiles or saves automatically.

Stored custom size is **not** measured live geometry, an embedded card's bounds,
the per-user screen-resolution preset or DPI-adjusted runtime resolution.
`size_applies=false` means the current mode ignores the stored custom size.
Do not infer the cause of a visual mismatch solely from these settings.

### Verification (2026-10-21)

- Python: 73 independent regression tests passed, including grouped pagination
	schema bounds, permission filtering, finite values and partial-error receipts.
- Native `UnrealMCP.UMG.ReparentWidget` and `PreviewSettings` passed in the repository
	and deployed SkillTest plugin: six panel types, original Widget/Slot identity,
	binding/animation records, fonts/brushes, refusal cases and Editor Undo/Redo.
- `UnrealMCP.UMG.Persistence` explicitly compiled/saved a unique SkillTest
	`/Game/__Dev/UMGVerified_46ec8bf04c564a4d99be562deb8d5902/WBP_Test` fixture;
	a separate process verified hierarchy/order, centered OverlaySlot, text/font,
	texture reference and Custom 320x120 preview. Binding/animation preservation
	was checked in memory, not functionally evaluated or cold-loaded in this fixture.
- SkillTest and NodeFall UE 5.7.4 builds and fresh-stdio smoke tests passed.
	NF received only the UMG header/implementation and Bridge update; local plugin
	customizations were retained. All 4379 protected NF files were unchanged.
- NF smoke was read-only: the existing tower action button returned `FillScreen`
	with stored size `[100,100]`, `size_applies=false`. This is not its effective
	preview geometry and does not establish the cause of the text alignment report.
	No business Widget was moved or resized. Interactive Designer appearance was
	not manually accepted; exceptional apply-failure rollback was not fault-injected.
- NF was restored to its original map on normal port 13090; temporary SkillTest
	port 13091 was closed. Client configurations/permissions were not changed.
	Existing clients must reconnect; fresh-stdio smoke does not grant client trust.

## Content Widgets

### `add_text_block_to_widget`
Add a Text Block. Params: `text_block_name`, `text`, `position`, `size`, `font_size`, `color`, `parent_name`.

### `add_button_to_widget`
Add a Button (with optional label text). Params: `button_name`, `text`, `position`, `size`, `font_size`, `color`, `background_color`, `parent_name`.

### `add_progress_bar_to_widget`
Add a ProgressBar. Params: `progress_bar_name`, `percent`, `fill_color`, `position`, `size`, `fill_type`, `parent_name`.

### `add_image_to_widget`
Add an Image. Params: `image_name`, `texture_path`, `tint_color`, `position`, `size`, `parent_name`.

### `add_slider_to_widget`
Add a Slider. Params: `slider_name`, `min_value`, `max_value`, `value`, `step_size`, `orientation`, `slider_color`, `handle_color`, `position`, `size`, `parent_name`.

### `add_combobox_to_widget`
Add a ComboBox (String) dropdown. Params: `combobox_name`, `options`, `selected_option`, `position`, `size`, `parent_name`.

### `add_spacer_to_widget`
Add a Spacer for padding/spacing between elements. Params: `spacer_name`, `size`, `position`, `parent_name`.

## Layout Containers

### `add_vertical_box_to_widget`
Add a VerticalBox layout container. Params: `box_name`, `position`, `size`, `parent_name`.

### `add_horizontal_box_to_widget`
Add a HorizontalBox layout container. Params: `box_name`, `position`, `size`, `parent_name`.

### `add_overlay_to_widget`
Add an Overlay container (stacks children). Params: `overlay_name`, `position`, `size`, `parent_name`.

### `add_size_box_to_widget`
Add a SizeBox to constrain child dimensions. Params: `size_box_name`, `width_override`, `height_override`, `position`, `size`, `parent_name`.

### `add_border_to_widget`
Add a Border widget. Params: `border_name`, `background_color`, `position`, `size`, `parent_name`.

## Properties, Layout & Binding

### `set_widget_property`
Set any property on an existing widget via reflection. Params: `widget_name`, `property_name`, `property_value`.

### `set_widget_anchor`
Set anchor, alignment, offset, and size on a widget inside a CanvasPanel. Params: `widget_name`, `anchor`, `alignment`, `offset`, `position`, `size`.

### `set_widget_slot_property`
Set slot properties on any widget; auto-detects slot type (CanvasPanel, HorizontalBox, VerticalBox, Overlay). Params: `widget_name`, `size_rule`, `fill_size`, `padding`, `h_align`, `v_align`, `anchor`, `alignment`, `offset`, `position`, `size`.

### `bind_widget_event`
Bind an event on a widget component (e.g. a Button's `OnClicked`) to a Blueprint function. Params: `widget_component_name`, `event_name`, `function_name`.

### `set_text_block_binding`
Set up a property binding for a Text Block (dynamic text). Params: `text_block_name`, `binding_property`, `binding_type` (default `Text`).
