# UMG Tools

Tools for creating and editing UMG Widget Blueprints (HUDs, menus, in-game UI) programmatically.

Most tools identify the target widget blueprint by its content `path` (e.g. `/Game/Widgets/WBP_HUD`). Child widgets are addressed by name, and can be parented to a layout container via `parent_name` (defaults to the root Canvas Panel).

## Blueprint Management

### `create_umg_widget_blueprint`
Create a new UMG Widget Blueprint.
- `widget_name`, `parent_class` (optional, defaults to `UserWidget`), `path` (default `/Game/Widgets`)

### `add_widget_to_viewport`
Add a Widget Blueprint instance to the viewport at an optional `z_order`.

### `read_widget_layout`
Read the full widget tree layout (hierarchy, types, slot properties) of a Widget Blueprint. Discovery step before editing.

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
