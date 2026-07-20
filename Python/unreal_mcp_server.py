"""
Unreal Engine MCP Server

A simple MCP server for interacting with Unreal Engine.
"""

import logging
import socket
import sys
import json
from contextlib import asynccontextmanager
from typing import AsyncIterator, Dict, Any, Optional
from mcp.server.fastmcp import FastMCP

# Configure logging with more detailed format
logging.basicConfig(
    level=logging.DEBUG,  # Change to DEBUG level for more details
    format='%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s',
    handlers=[
        logging.FileHandler('unreal_mcp.log'),
        # logging.StreamHandler(sys.stdout) # Remove this handler to unexpected non-whitespace characters in JSON
    ]
)
logger = logging.getLogger("UnrealMCP")

# Configuration
UNREAL_HOST = "127.0.0.1"
UNREAL_PORT = 13090

class UnrealConnection:
    """Connection to an Unreal Engine instance."""
    
    def __init__(self):
        """Initialize the connection."""
        self.socket = None
        self.connected = False
    
    def connect(self) -> bool:
        """Connect to the Unreal Engine instance."""
        try:
            # Close any existing socket
            if self.socket:
                try:
                    self.socket.close()
                except:
                    pass
                self.socket = None
            
            logger.info(f"Connecting to Unreal at {UNREAL_HOST}:{UNREAL_PORT}...")
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(30)  # 30 second timeout
            
            # Set socket options for better stability
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            
            # Set larger buffer sizes
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
            
            self.socket.connect((UNREAL_HOST, UNREAL_PORT))
            self.connected = True
            logger.info("Connected to Unreal Engine")
            return True
            
        except Exception as e:
            logger.error(f"Failed to connect to Unreal: {e}")
            self.connected = False
            return False
    
    def disconnect(self):
        """Disconnect from the Unreal Engine instance."""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        self.socket = None
        self.connected = False

    def receive_full_response(self, sock, buffer_size=4096) -> bytes:
        """Receive a complete response from Unreal, handling chunked data."""
        chunks = []
        sock.settimeout(30)  # 30 second timeout
        try:
            while True:
                chunk = sock.recv(buffer_size)
                if not chunk:
                    if not chunks:
                        raise Exception("Connection closed before receiving data")
                    break
                chunks.append(chunk)
                
                # Process the data received so far
                data = b''.join(chunks)
                decoded_data = data.decode('utf-8')
                
                # Try to parse as JSON to check if complete
                try:
                    json.loads(decoded_data)
                    logger.info(f"Received complete response ({len(data)} bytes)")
                    return data
                except json.JSONDecodeError:
                    # Not complete JSON yet, continue reading
                    logger.debug(f"Received partial response, waiting for more data...")
                    continue
                except Exception as e:
                    logger.warning(f"Error processing response chunk: {str(e)}")
                    continue
        except socket.timeout:
            logger.warning("Socket timeout during receive")
            if chunks:
                # If we have some data already, try to use it
                data = b''.join(chunks)
                try:
                    json.loads(data.decode('utf-8'))
                    logger.info(f"Using partial response after timeout ({len(data)} bytes)")
                    return data
                except:
                    pass
            raise Exception("Timeout receiving Unreal response")
        except Exception as e:
            logger.error(f"Error during receive: {str(e)}")
            raise
    
    def send_command(self, command: str, params: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        """Send a command to Unreal Engine and get the response."""
        # Always reconnect for each command, since Unreal closes the connection after each command
        # This is different from Unity which keeps connections alive
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            self.connected = False
        
        if not self.connect():
            logger.error("Failed to connect to Unreal Engine for command")
            return None
        
        try:
            # Match Unity's command format exactly
            command_obj = {
                "type": command,  # Use "type" instead of "command"
                "params": params or {}  # Use Unity's params or {} pattern
            }
            
            # Send without newline, exactly like Unity
            command_json = json.dumps(command_obj)
            logger.info(f"Sending command: {command_json}")
            self.socket.sendall(command_json.encode('utf-8'))
            
            # Read response using improved handler
            response_data = self.receive_full_response(self.socket)
            response = json.loads(response_data.decode('utf-8'))
            
            # Log complete response for debugging
            logger.info(f"Complete response from Unreal: {response}")
            
            # Check for both error formats: {"status": "error", ...} and {"success": false, ...}
            if response.get("status") == "error":
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                logger.error(f"Unreal error (status=error): {error_message}")
                # We want to preserve the original error structure but ensure error is accessible
                if "error" not in response:
                    response["error"] = error_message
            elif response.get("success") is False:
                # This format uses {"success": false, "error": "message"} or {"success": false, "message": "message"}
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                logger.error(f"Unreal error (success=false): {error_message}")
                # Convert to the standard format expected by higher layers
                response = {
                    "status": "error",
                    "error": error_message
                }
            
            # Always close the connection after command is complete
            # since Unreal will close it on its side anyway
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            self.connected = False
            
            return response
            
        except Exception as e:
            logger.error(f"Error sending command: {e}")
            # Always reset connection state on any error
            self.connected = False
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            return {
                "status": "error",
                "error": str(e)
            }

# Global connection state
_unreal_connection: UnrealConnection = None

def get_unreal_connection() -> Optional[UnrealConnection]:
    """Get the connection to Unreal Engine."""
    global _unreal_connection
    try:
        if _unreal_connection is None:
            _unreal_connection = UnrealConnection()
            if not _unreal_connection.connect():
                logger.warning("Could not connect to Unreal Engine")
                _unreal_connection = None
        else:
            # Verify connection is still valid with a ping-like test
            try:
                # Simple test by sending an empty buffer to check if socket is still connected
                _unreal_connection.socket.sendall(b'\x00')
                logger.debug("Connection verified with ping test")
            except Exception as e:
                logger.warning(f"Existing connection failed: {e}")
                _unreal_connection.disconnect()
                _unreal_connection = None
                # Try to reconnect
                _unreal_connection = UnrealConnection()
                if not _unreal_connection.connect():
                    logger.warning("Could not reconnect to Unreal Engine")
                    _unreal_connection = None
                else:
                    logger.info("Successfully reconnected to Unreal Engine")
        
        return _unreal_connection
    except Exception as e:
        logger.error(f"Error getting Unreal connection: {e}")
        return None


def spill_if_oversized(
    response: Any,
    tool_name: str,
    identifier: str,
    preview_keys: tuple = ("name", "path", "parent_class"),
    count_keys: tuple = (),
    threshold: int = 24000,
    hint: str = "",
) -> Any:
    """If a tool response would exceed the MCP tool payload ceiling (~25KB),
    write the full JSON to a temp file and return a small pointer instead.

    Args:
        response: The raw response dict from send_command.
        tool_name: Short tool identifier used in the temp filename (e.g. "read_blueprint").
        identifier: The asset/thing being read; used to make the temp file name readable
            and stable across repeated calls (e.g. "/Game/UI/W_Healthbar").
        preview_keys: Top-level keys copied verbatim into `preview`.
        count_keys: Keys whose length is reported as `<key>_count` in `preview`
            (so callers know how much data is in the spill file).
        threshold: Char count above which we spill to disk.
        hint: Optional message appended to the returned `message` field, e.g.
            "Pass include_nodes=False for a smaller inline response."

    Returns either the original response (if under threshold), or a small dict
    with success/overflow/size_bytes/file_path/preview/message.
    """
    import json as _json
    import os as _os
    import tempfile as _tempfile
    import hashlib as _hashlib

    try:
        serialized = _json.dumps(response, ensure_ascii=False)
    except Exception:
        return response  # can't measure; return as-is

    if len(serialized) <= threshold:
        return response

    slug = _hashlib.md5(identifier.encode("utf-8")).hexdigest()[:12]
    safe_name = identifier.strip("/").replace("/", "_").replace(":", "_").replace("\\", "_")
    out_path = _os.path.join(
        _tempfile.gettempdir(),
        f"mcp_{tool_name}_{safe_name}_{slug}.json",
    )
    try:
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(serialized)
    except OSError as e:
        logger.warning(f"[{tool_name}] Failed to spill oversized response to disk: {e}")
        return response

    payload = response.get("result", response) if isinstance(response, dict) else {}
    preview = {}
    if isinstance(payload, dict):
        for k in preview_keys:
            if k in payload:
                preview[k] = payload.get(k)
        for k in count_keys:
            val = payload.get(k)
            if isinstance(val, list):
                preview[f"{k}_count"] = len(val)

    message = (
        f"Response was {len(serialized)} chars (over {threshold} threshold). "
        f"Full JSON written to '{out_path}' — read it with read_file to inspect."
    )
    if hint:
        message += " " + hint

    logger.info(f"[{tool_name}] spilled {len(serialized)} chars > {threshold} to {out_path}")
    return {
        "success": True,
        "overflow": True,
        "size_bytes": len(serialized),
        "file_path": out_path,
        "preview": preview,
        "message": message,
    }

@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    """Handle server startup and shutdown."""
    global _unreal_connection
    logger.info("UnrealMCP server starting up")
    try:
        _unreal_connection = get_unreal_connection()
        if _unreal_connection:
            logger.info("Connected to Unreal Engine on startup")
        else:
            logger.warning("Could not connect to Unreal Engine on startup")
    except Exception as e:
        logger.error(f"Error connecting to Unreal Engine on startup: {e}")
        _unreal_connection = None
    
    try:
        yield {}
    finally:
        if _unreal_connection:
            _unreal_connection.disconnect()
            _unreal_connection = None
        logger.info("Unreal MCP server shut down")

# Initialize server
mcp = FastMCP(
    "UnrealMCP",
    description="Unreal Engine integration via Model Context Protocol",
    lifespan=server_lifespan
)

# Import and register tools
import os

from tools.editor_tools import register_editor_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.node_tools import register_blueprint_node_tools
from tools.project_tools import register_project_tools
from tools.umg_tools import register_umg_tools
from tools.material_tools import register_material_tools
from tools.niagara_tools import register_niagara_tools

# Register all tools first
register_editor_tools(mcp)
register_blueprint_tools(mcp)
register_blueprint_node_tools(mcp)
register_project_tools(mcp)
register_umg_tools(mcp)
register_material_tools(mcp)
register_niagara_tools(mcp)

# Read-only mode: if UNREAL_MCP_READ_ONLY=1, remove all write/modify tools
# and keep only read/query tools. This is useful when you want the AI to
# understand your project without making any changes.
#
# Usage in mcp.json:
#   "env": { "UNREAL_MCP_READ_ONLY": "1" }
_read_only = os.environ.get("UNREAL_MCP_READ_ONLY", "").strip() in ("1", "true", "yes")

if _read_only:
    # Tools that are safe in read-only mode (query/inspect only, no side effects)
    _READ_ONLY_TOOLS = {
        # Editor: query actors and their properties
        "get_actors_in_level",
        "find_actors_by_name",
        "get_actor_properties",
        # Blueprint: read structure and list assets
        "read_blueprint",
        "list_blueprints",
        # Node: inspect existing graph nodes
        "find_blueprint_nodes",
        # Editor logs: read output log
        "get_editor_logs",
        # Editor state: check unsaved changes
        "get_unsaved_changes",
        # Material: read material assets and properties
        "list_materials",
        "read_material",
        "get_material_instance_parameters",
        # UMG: read widget layout
        "read_widget_layout",
        # Niagara: read-only inspection
        "list_niagara_systems",
        "read_niagara_system",
        "get_niagara_parameters",
        "list_module_inputs",
        "list_module_static_switches",
        "read_ns_curve",
    }

    all_tool_names = list(mcp._tool_manager._tools.keys())
    removed = []
    for tool_name in all_tool_names:
        if tool_name not in _READ_ONLY_TOOLS:
            del mcp._tool_manager._tools[tool_name]
            removed.append(tool_name)

    logger.info(f"READ-ONLY mode enabled. Kept {len(_READ_ONLY_TOOLS)} tools, removed {len(removed)}: {removed}")

# ─────────────────────────────────────────────────────────────────────────────
# Per-category tool gating
#
# Each tool category can be disabled via an environment variable, so you can trim
# the tool list when some tools aren't needed (fewer tools = better model tool
# selection). A category is DISABLED when its env var is set to one of
# "0" / "false" / "no" / "off". Any other value (or unset) keeps it ENABLED.
#
# Usage in mcp.json:
#   "env": { "MCP_UMG_ENABLED": "0", "MCP_NIAGARA_ENABLED": "0" }
#
# Categories and their tool name prefixes/sets are defined below.
# ─────────────────────────────────────────────────────────────────────────────
_CATEGORY_TOOLS = {
    "umg": {
        "create_umg_widget_blueprint", "add_text_block_to_widget", "add_button_to_widget",
        "bind_widget_event", "set_text_block_binding", "add_widget_to_viewport",
        "add_progress_bar_to_widget", "add_image_to_widget", "add_vertical_box_to_widget",
        "add_horizontal_box_to_widget", "add_overlay_to_widget", "add_size_box_to_widget",
        "add_border_to_widget", "add_spacer_to_widget", "add_combobox_to_widget",
        "add_slider_to_widget", "read_widget_layout", "set_widget_property",
        "set_widget_slot_property", "set_widget_anchor", "set_actor_property",
    },
    "material": {
        "list_materials", "read_material", "get_material_instance_parameters",
        "create_material", "add_material_expression", "set_material_expression_property",
        "connect_material_expressions", "connect_material_to_property", "create_material_instance",
        "add_material_comment", "reset_material_node_layout", "set_material_property",
        "add_custom_hlsl_expression", "set_material_expression_position",
    },
    "niagara": {
        "list_niagara_systems", "read_niagara_system", "set_niagara_parameter",
        "get_niagara_parameters", "create_niagara_system", "set_niagara_rapid_parameter",
        "modify_emitter_properties", "list_niagara_emitter_templates", "add_emitter_to_system",
        "remove_emitter_from_system", "add_module_to_emitter", "remove_module_from_emitter",
        "set_niagara_renderer_property", "list_module_inputs", "enable_module_input",
        "list_module_static_switches", "set_module_static_switch", "bind_module_input_datainterface",
        "read_ns_curve", "set_ns_curve_keys", "set_module_dynamic_input",
    },
    "blueprint": {
        "create_blueprint", "add_component_to_blueprint", "set_component_property",
        "set_physics_properties", "compile_blueprint", "set_blueprint_property",
        "set_static_mesh_properties", "set_pawn_properties", "read_blueprint", "list_blueprints",
    },
    "node": {
        "connect_blueprint_nodes", "add_blueprint_get_self_component_reference",
        "add_blueprint_self_reference", "find_blueprint_nodes", "add_blueprint_event_node",
        "add_blueprint_input_action_node", "add_blueprint_function_node",
        "add_blueprint_get_component_node", "add_blueprint_variable",
    },
    "project": {
        "create_input_mapping", "read_data_asset", "get_class_properties",
        "read_behavior_tree", "read_blackboard", "read_state_tree",
    },
    "editor": {
        "get_actors_in_level", "find_actors_by_name", "spawn_actor", "delete_actor",
        "set_actor_transform", "get_actor_properties", "spawn_blueprint_actor",
        "focus_viewport", "take_screenshot", "get_unsaved_changes", "save_asset",
        "close_editor", "open_asset", "open_level", "save_level", "create_level",
        "get_editor_logs",
    },
}

def _is_disabled(env_value: str) -> bool:
    return env_value.strip().lower() in ("0", "false", "no", "off")

_disabled_categories = []
for _category, _tool_set in _CATEGORY_TOOLS.items():
    _env_var = f"MCP_{_category.upper()}_ENABLED"
    _env_value = os.environ.get(_env_var, "")
    if _env_value and _is_disabled(_env_value):
        _removed_in_cat = []
        for _tool_name in _tool_set:
            if _tool_name in mcp._tool_manager._tools:
                del mcp._tool_manager._tools[_tool_name]
                _removed_in_cat.append(_tool_name)
        _disabled_categories.append(_category)
        logger.info(f"Category '{_category}' disabled via {_env_var}. Removed {len(_removed_in_cat)} tools: {_removed_in_cat}")

if _disabled_categories:
    logger.info(f"Disabled tool categories: {_disabled_categories}. Remaining tools: {len(mcp._tool_manager._tools)}")

@mcp.prompt()
def info():
    """Information about available Unreal MCP tools and best practices."""
    return """
    # Unreal MCP Server Tools and Best Practices
    
    ## UMG (Widget Blueprint) Tools
    - `create_umg_widget_blueprint(widget_name, parent_class="UserWidget", path="/Game/UI")` 
      Create a new UMG Widget Blueprint
    - `add_text_block_to_widget(widget_name, text_block_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1])`
      Add a Text Block widget with customizable properties
    - `add_button_to_widget(widget_name, button_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1], background_color=[0.1,0.1,0.1,1])`
      Add a Button widget with text and styling
    - `bind_widget_event(widget_name, widget_component_name, event_name, function_name="")`
      Bind events like OnClicked to functions
    - `add_widget_to_viewport(widget_name, z_order=0)`
      Add widget instance to game viewport
    - `set_text_block_binding(widget_name, text_block_name, binding_property, binding_type="Text")`
      Set up dynamic property binding for text blocks

    ## Editor Tools
    ### Viewport and Screenshots
    - `focus_viewport(target, location, distance, orientation)` - Focus viewport
    - `take_screenshot(filename, show_ui, resolution)` - Capture screenshots

    ### Actor Management
    - `get_actors_in_level()` - List all actors in current level
    - `find_actors_by_name(pattern)` - Find actors by name pattern
    - `spawn_actor(name, type, location=[0,0,0], rotation=[0,0,0], scale=[1,1,1])` - Create actors
    - `delete_actor(name)` - Remove actors
    - `set_actor_transform(name, location, rotation, scale)` - Modify actor transform
    - `get_actor_properties(name)` - Get actor properties
    
    ## Blueprint Management
    - `create_blueprint(name, parent_class)` - Create new Blueprint classes
    - `add_component_to_blueprint(blueprint_name, component_type, component_name)` - Add components
    - `set_static_mesh_properties(blueprint_name, component_name, static_mesh)` - Configure meshes
    - `set_physics_properties(blueprint_name, component_name)` - Configure physics
    - `compile_blueprint(blueprint_name)` - Compile Blueprint changes
    - `set_blueprint_property(blueprint_name, property_name, property_value)` - Set properties
    - `set_pawn_properties(blueprint_name)` - Configure Pawn settings
    - `spawn_blueprint_actor(blueprint_name, actor_name)` - Spawn Blueprint actors
    - `read_blueprint(blueprint_name)` - Read full Blueprint structure (components, variables, graphs, etc.)
    - `list_blueprints(path, recursive, name_filter)` - List all Blueprints in the project
    
    ## Blueprint Node Management
    - `add_blueprint_event_node(blueprint_name, event_type)` - Add event nodes
    - `add_blueprint_input_action_node(blueprint_name, action_name)` - Add input nodes
    - `add_blueprint_function_node(blueprint_name, target, function_name)` - Add function nodes
    - `connect_blueprint_nodes(blueprint_name, source_node_id, source_pin, target_node_id, target_pin)` - Connect nodes
    - `add_blueprint_variable(blueprint_name, variable_name, variable_type)` - Add variables
    - `add_blueprint_get_self_component_reference(blueprint_name, component_name)` - Add component refs
    - `add_blueprint_self_reference(blueprint_name)` - Add self references
    - `find_blueprint_nodes(blueprint_name, node_type, event_type)` - Find nodes
    
    ## Project Tools
    - `create_input_mapping(action_name, key, input_type)` - Create input mappings
    
    ## Best Practices
    
    ### UMG Widget Development
    - Create widgets with descriptive names that reflect their purpose
    - Use consistent naming conventions for widget components
    - Organize widget hierarchy logically
    - Set appropriate anchors and alignment for responsive layouts
    - Use property bindings for dynamic updates instead of direct setting
    - Handle widget events appropriately with meaningful function names
    - Clean up widgets when no longer needed
    - Test widget layouts at different resolutions
    
    ### Editor and Actor Management
    - Use unique names for actors to avoid conflicts
    - Clean up temporary actors
    - Validate transforms before applying
    - Check actor existence before modifications
    - Take regular viewport screenshots during development
    - Keep the viewport focused on relevant actors during operations
    
    ### Blueprint Development
    - Compile Blueprints after changes
    - Use meaningful names for variables and functions
    - Organize nodes logically
    - Test functionality in isolation
    - Consider performance implications
    - Document complex setups
    
    ### Error Handling
    - Check command responses for success
    - Handle errors gracefully
    - Log important operations
    - Validate parameters
    - Clean up resources on errors
    """

# Run the server
if __name__ == "__main__":
    logger.info("Starting MCP server with stdio transport")
    mcp.run(transport='stdio') 