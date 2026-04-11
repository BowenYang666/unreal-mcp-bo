"""
Material Tools for Unreal MCP.

This module provides read-only tools for inspecting Material assets,
their node graphs, and MaterialInstance parameter values.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_material_tools(mcp: FastMCP):
    """Register material tools with the MCP server."""

    @mcp.tool()
    def list_materials(
        ctx: Context,
        path: str = "/Game",
        recursive: bool = True,
        name_filter: str = "",
        type: str = "all"
    ) -> Dict[str, Any]:
        """List all Material and MaterialInstance assets in the project.

        Args:
            ctx: The MCP context
            path: Asset path to search in (default: "/Game")
            recursive: Search subdirectories recursively (default: True)
            name_filter: Only include assets whose name contains this string (case-insensitive)
            type: Filter by type - "material", "instance", or "all" (default: "all")

        Returns:
            Dict with "count" (int) and "materials" (list of dicts with name, path, package_path, type)

        Examples:
            list_materials()
            list_materials(path="/Game/Materials")
            list_materials(type="instance", name_filter="Wood")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "path": path,
                "recursive": recursive,
            }
            if name_filter:
                params["name_filter"] = name_filter
            if type != "all":
                params["type"] = type

            response = unreal.send_command("list_materials", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Found {result.get('count', 0)} materials")
            return result

        except Exception as e:
            logger.error(f"Error listing materials: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def read_material(
        ctx: Context,
        name: str = "",
        path: str = ""
    ) -> Dict[str, Any]:
        """Read a Material's node graph, properties, and connections.

        Returns the complete material structure including: material domain, blend mode,
        shading model, all expression nodes with their types/parameters/connections,
        and which nodes are connected to material input pins (BaseColor, Metallic, etc).

        Provide either 'name' or 'path' to identify the material.

        Args:
            ctx: The MCP context
            name: Material asset name (e.g. "M_Wood"). Searches in /Game.
            path: Full asset path (e.g. "/Game/Materials/M_Wood")

        Returns:
            Dict with material properties, nodes (expression graph), material_inputs (pin connections), comments

        Examples:
            read_material(name="M_Wood")
            read_material(path="/Game/Materials/M_Wood")
        """
        from unreal_mcp_server import get_unreal_connection

        if not name and not path:
            return {"success": False, "message": "Provide either 'name' or 'path'"}

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if path:
                params["path"] = path
            else:
                params["name"] = name

            response = unreal.send_command("read_material", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Read material: {result.get('name', 'unknown')} ({result.get('node_count', 0)} nodes)")
            return result

        except Exception as e:
            logger.error(f"Error reading material: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def get_material_instance_parameters(
        ctx: Context,
        name: str = "",
        path: str = ""
    ) -> Dict[str, Any]:
        """Get a MaterialInstance's parameter overrides (scalar, vector, texture).

        Returns the parent material reference and all overridden parameter values.

        Provide either 'name' or 'path' to identify the material instance.

        Args:
            ctx: The MCP context
            name: MaterialInstance asset name (e.g. "MI_Wood_Dark"). Searches in /Game.
            path: Full asset path (e.g. "/Game/Materials/MI_Wood_Dark")

        Returns:
            Dict with parent info and parameter arrays: scalar_parameters, vector_parameters, texture_parameters

        Examples:
            get_material_instance_parameters(name="MI_Wood_Dark")
            get_material_instance_parameters(path="/Game/Materials/MI_Wood_Dark")
        """
        from unreal_mcp_server import get_unreal_connection

        if not name and not path:
            return {"success": False, "message": "Provide either 'name' or 'path'"}

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if path:
                params["path"] = path
            else:
                params["name"] = name

            response = unreal.send_command("get_material_instance_parameters", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Read material instance: {result.get('name', 'unknown')}")
            return result

        except Exception as e:
            logger.error(f"Error getting material instance parameters: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def create_material(
        ctx: Context,
        asset_path: str,
        blend_mode: str = "Opaque",
        shading_model: str = "DefaultLit",
        two_sided: bool = False,
        material_domain: str = "Surface"
    ) -> Dict[str, Any]:
        """Create a new Material asset.

        Args:
            ctx: The MCP context
            asset_path: Full asset path for the new material (e.g. "/Game/Materials/M_MyMaterial")
            blend_mode: Blend mode - "Opaque", "Translucent", "Additive", "Modulate", "Masked" (default: "Opaque")
            shading_model: Shading model - "DefaultLit", "Unlit", "Subsurface", "ClearCoat" (default: "DefaultLit")
            two_sided: Whether the material is two-sided (default: False)
            material_domain: Material domain - "Surface", "PostProcess", "DeferredDecal", "LightFunction", "UI" (default: "Surface")

        Returns:
            Dict with name, path, blend_mode, shading_model, two_sided

        Examples:
            create_material(asset_path="/Game/Materials/M_Ice")
            create_material(asset_path="/Game/Materials/M_Glass", blend_mode="Translucent", two_sided=True)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {"asset_path": asset_path}
            if blend_mode != "Opaque":
                params["blend_mode"] = blend_mode
            if shading_model != "DefaultLit":
                params["shading_model"] = shading_model
            if two_sided:
                params["two_sided"] = two_sided
            if material_domain != "Surface":
                params["material_domain"] = material_domain

            response = unreal.send_command("create_material", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Created material: {result.get('name', 'unknown')}")
            return result

        except Exception as e:
            logger.error(f"Error creating material: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_material_expression(
        ctx: Context,
        asset_path: str,
        expression_type: str,
        pos_x: int = 0,
        pos_y: int = 0
    ) -> Dict[str, Any]:
        """Add a material expression (node) to a Material.

        Common expression types:
        - Constants: "Constant", "Constant2Vector", "Constant3Vector", "Constant4Vector"
        - Parameters: "ScalarParameter", "VectorParameter", "StaticSwitchParameter"
        - Math: "Add", "Subtract", "Multiply", "Divide", "Power", "Abs", "Lerp", "Clamp"
        - Texture: "TextureCoordinate", "TextureSample", "TextureSampleParameter2D"
        - Utility: "Noise", "Time", "Panner", "WorldPosition", "VertexNormalWS", "PixelNormalWS"
        - Custom: "Custom" (HLSL code node)

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material (e.g. "/Game/Materials/M_MyMaterial")
            expression_type: Expression class name, with or without "MaterialExpression" prefix
            pos_x: X position in the material editor graph (default: 0)
            pos_y: Y position in the material editor graph (default: 0)

        Returns:
            Dict with node_index, node_type, description, pos_x, pos_y

        Examples:
            add_material_expression(asset_path="/Game/Materials/M_Ice", expression_type="Constant3Vector")
            add_material_expression(asset_path="/Game/Materials/M_Ice", expression_type="Multiply", pos_x=200, pos_y=0)
            add_material_expression(asset_path="/Game/Materials/M_Ice", expression_type="Noise", pos_x=-400, pos_y=200)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "expression_type": expression_type,
                "pos_x": pos_x,
                "pos_y": pos_y,
            }

            response = unreal.send_command("add_material_expression", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Added expression: {result.get('node_type', 'unknown')} at index {result.get('node_index', -1)}")
            return result

        except Exception as e:
            logger.error(f"Error adding material expression: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_material_expression_property(
        ctx: Context,
        asset_path: str,
        node_index: int,
        property_name: str,
        value: Any = None
    ) -> Dict[str, Any]:
        """Set a property on a material expression node.

        Supports numeric types (float, int, bool), strings, colors (LinearColor as {r,g,b,a}),
        vectors ({x,y,z}), and enum values (as string name).

        Note: Texture properties are not yet supported (TODO).

        Common property examples by expression type:
        - Constant: property_name="R", value=0.5
        - Constant3Vector: property_name="Constant", value={"r":0.5,"g":0.8,"b":1.0,"a":1.0}
        - ScalarParameter: property_name="DefaultValue", value=0.5 (also "ParameterName" for name)
        - VectorParameter: property_name="DefaultValue", value={"r":1,"g":0,"b":0,"a":1}
        - Noise: property_name="NoiseFunction", value="VoronoiALU" (enum)

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material
            node_index: Index of the expression node (from add_material_expression or read_material)
            property_name: Name of the property to set
            value: Value to set. Type depends on the property: number, bool, string, or dict for colors/vectors.

        Returns:
            Dict with success, node_type, node_index, property_name

        Examples:
            set_material_expression_property(asset_path="/Game/Materials/M_Ice", node_index=0, property_name="R", value=0.5)
            set_material_expression_property(asset_path="/Game/Materials/M_Ice", node_index=1, property_name="Constant", value={"r":0.5,"g":0.8,"b":1.0,"a":1.0})
            set_material_expression_property(asset_path="/Game/Materials/M_Ice", node_index=2, property_name="ParameterName", value="IceColor")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "node_index": node_index,
                "property_name": property_name,
                "value": value,
            }

            response = unreal.send_command("set_material_expression_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Set property {property_name} on node {node_index}")
            return result

        except Exception as e:
            logger.error(f"Error setting material expression property: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def connect_material_expressions(
        ctx: Context,
        asset_path: str,
        from_node_index: int,
        to_node_index: int,
        to_input_name: str,
        from_output_name: str = ""
    ) -> Dict[str, Any]:
        """Connect two material expression nodes (node output -> node input).

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material
            from_node_index: Index of the source expression node
            to_node_index: Index of the destination expression node
            to_input_name: Name of the input pin on the destination node (e.g. "A", "B", "Coordinates")
            from_output_name: Name of the output pin on the source node (default: "" for first/default output)

        Returns:
            Dict with success, from_node, to_node, from_output, to_input

        Examples:
            connect_material_expressions(asset_path="/Game/Materials/M_Ice", from_node_index=0, to_node_index=2, to_input_name="A")
            connect_material_expressions(asset_path="/Game/Materials/M_Ice", from_node_index=1, to_node_index=2, to_input_name="B")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "from_node_index": from_node_index,
                "to_node_index": to_node_index,
                "to_input_name": to_input_name,
            }
            if from_output_name:
                params["from_output_name"] = from_output_name

            response = unreal.send_command("connect_material_expressions", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Connected nodes: {from_node_index} -> {to_node_index}")
            return result

        except Exception as e:
            logger.error(f"Error connecting material expressions: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def connect_material_to_property(
        ctx: Context,
        asset_path: str,
        node_index: int,
        material_property: str,
        output_name: str = ""
    ) -> Dict[str, Any]:
        """Connect a material expression node to a material input property (e.g. BaseColor, Normal).

        Valid material properties: BaseColor, Metallic, Specular, Roughness, Anisotropy,
        Normal, Tangent, EmissiveColor, Opacity, OpacityMask, WorldPositionOffset,
        SubsurfaceColor, AmbientOcclusion, Refraction, PixelDepthOffset

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material
            node_index: Index of the expression node to connect
            material_property: Material input property name (e.g. "BaseColor", "Normal", "Roughness")
            output_name: Name of the output pin on the node (default: "" for first/default output)

        Returns:
            Dict with success, node, output, material_property

        Examples:
            connect_material_to_property(asset_path="/Game/Materials/M_Ice", node_index=2, material_property="BaseColor")
            connect_material_to_property(asset_path="/Game/Materials/M_Ice", node_index=3, material_property="Normal")
            connect_material_to_property(asset_path="/Game/Materials/M_Ice", node_index=0, material_property="Roughness")
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "node_index": node_index,
                "material_property": material_property,
            }
            if output_name:
                params["output_name"] = output_name

            response = unreal.send_command("connect_material_to_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Connected node {node_index} to {material_property}")
            return result

        except Exception as e:
            logger.error(f"Error connecting material to property: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_custom_hlsl_expression(
        ctx: Context,
        asset_path: str,
        code: str,
        output_type: str = "CMOT_Float3",
        description: str = "Custom",
        inputs: List[str] = None,
        pos_x: int = 0,
        pos_y: int = 0
    ) -> Dict[str, Any]:
        """Add a Custom HLSL expression node to a Material.

        This creates a UMaterialExpressionCustom node with custom HLSL code,
        configurable output type, description, and named inputs.

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material
            code: HLSL code string (e.g. "return saturate(pow(1-dot(Normal,CameraDir),3));")
            output_type: Output type enum - "CMOT_Float1", "CMOT_Float2", "CMOT_Float3", "CMOT_Float4", "CMOT_MaterialAttributes" (default: "CMOT_Float3")
            description: Display name for the node (default: "Custom")
            inputs: List of input pin names (e.g. ["Normal", "CameraDir"]). Default: no extra inputs.
            pos_x: X position in the material editor graph (default: 0)
            pos_y: Y position in the material editor graph (default: 0)

        Returns:
            Dict with node_index, node_type, description, code, output_type, inputs

        Examples:
            add_custom_hlsl_expression(asset_path="/Game/Materials/M_Ice", code="return pow(1-dot(Normal,CameraDir),3);", output_type="CMOT_Float1", description="Fresnel", inputs=["Normal","CameraDir"])
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "code": code,
                "output_type": output_type,
                "description": description,
                "pos_x": pos_x,
                "pos_y": pos_y,
            }
            if inputs:
                params["inputs"] = inputs

            response = unreal.send_command("add_custom_hlsl_expression", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Added custom HLSL expression at index {result.get('node_index', -1)}")
            return result

        except Exception as e:
            logger.error(f"Error adding custom HLSL expression: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def create_material_instance(
        ctx: Context,
        asset_path: str,
        parent_material_path: str,
        scalar_params: Dict[str, float] = None,
        vector_params: Dict[str, Dict[str, float]] = None
    ) -> Dict[str, Any]:
        """Create a MaterialInstanceConstant from a parent Material and set parameter overrides.

        Args:
            ctx: The MCP context
            asset_path: Full asset path for the new material instance (e.g. "/Game/Materials/MI_FrostLight")
            parent_material_path: Full asset path of the parent material (e.g. "/Game/Materials/M_Frost")
            scalar_params: Dict of scalar parameter overrides {"ParamName": value}. Default: no overrides.
            vector_params: Dict of vector parameter overrides {"ParamName": {"r":1,"g":0,"b":0,"a":1}}. Default: no overrides.

        Returns:
            Dict with name, path, parent, scalar_parameters, vector_parameters

        Examples:
            create_material_instance(asset_path="/Game/Materials/MI_FrostLight", parent_material_path="/Game/Materials/M_Frost", scalar_params={"FrostAmount": 0.3})
            create_material_instance(asset_path="/Game/Materials/MI_FrostHeavy", parent_material_path="/Game/Materials/M_Frost", scalar_params={"FrostAmount": 1.0}, vector_params={"IceColor": {"r":0.4,"g":0.7,"b":1.0,"a":1.0}})
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "parent_material_path": parent_material_path,
            }
            if scalar_params:
                params["scalar_params"] = scalar_params
            if vector_params:
                params["vector_params"] = vector_params

            response = unreal.send_command("create_material_instance", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Created material instance: {result.get('name', 'unknown')}")
            return result

        except Exception as e:
            logger.error(f"Error creating material instance: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_material_comment(
        ctx: Context,
        asset_path: str,
        text: str,
        pos_x: int = 0,
        pos_y: int = 0,
        size_x: int = 400,
        size_y: int = 300
    ) -> Dict[str, Any]:
        """Add a comment box (group) to the material editor graph.

        This is equivalent to pressing 'C' in the material editor and typing a name.
        Useful for organizing and documenting material node groups.

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material
            text: Comment text displayed on the box
            pos_x: X position in the material editor graph (default: 0)
            pos_y: Y position in the material editor graph (default: 0)
            size_x: Width of the comment box (default: 400)
            size_y: Height of the comment box (default: 300)

        Returns:
            Dict with success, text, pos_x, pos_y, size_x, size_y

        Examples:
            add_material_comment(asset_path="/Game/Materials/M_Ice", text="Ice Color", pos_x=-850, pos_y=-50, size_x=300, size_y=150)
            add_material_comment(asset_path="/Game/Materials/M_Ice", text="Frost Mask", pos_x=-850, pos_y=150, size_x=700, size_y=400)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "text": text,
                "pos_x": pos_x,
                "pos_y": pos_y,
                "size_x": size_x,
                "size_y": size_y,
            }

            response = unreal.send_command("add_material_comment", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Added material comment: {text}")
            return result

        except Exception as e:
            logger.error(f"Error adding material comment: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def reset_material_node_layout(
        ctx: Context,
        asset_path: str,
        spacing_x: int = 400,
        spacing_y: int = 250
    ) -> Dict[str, Any]:
        """Auto-layout material nodes using topological sort.

        Analyzes the material's node graph connections, performs a topological sort
        from material property inputs back to source nodes, and arranges all nodes
        in clean columns (layers). Layer 0 (rightmost) contains nodes directly
        connected to material properties; deeper layers are placed further left.

        Nodes within each layer are sorted to minimize edge crossings.

        Args:
            ctx: The MCP context
            asset_path: Full asset path of the material (e.g. "/Game/Materials/M_MyMaterial")
            spacing_x: Horizontal spacing between layers in pixels (default: 250)
            spacing_y: Vertical spacing between nodes in the same layer (default: 150)

        Returns:
            Dict with success, nodes_laid_out, layers count, and layout details per layer

        Examples:
            reset_material_node_layout(asset_path="/Game/Materials/M_FrostTest")
            reset_material_node_layout(asset_path="/Game/Materials/M_FrostTest", spacing_x=300, spacing_y=200)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "spacing_x": spacing_x,
                "spacing_y": spacing_y,
            }

            response = unreal.send_command("reset_material_node_layout", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Layout reset: {result.get('nodes_laid_out', 0)} nodes in {result.get('layers', 0)} layers")
            return result

        except Exception as e:
            logger.error(f"Error resetting material node layout: {e}")
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_expression_position(
        asset_path: str,
        node_index: int,
        pos_x: int,
        pos_y: int,
    ) -> dict:
        """
        Set the editor position of a material expression node.
        Use this to manually arrange nodes before adding comment groups.

        Args:
            asset_path: The asset path of the material (e.g. "/Game/Materials/M_Ice")
            node_index: The index of the expression node to move
            pos_x: Target X coordinate (negative = left, 0 = near material output)
            pos_y: Target Y coordinate (negative = up, positive = down)

        Examples:
            set_expression_position(asset_path="/Game/Materials/M_Ice", node_index=0, pos_x=-400, pos_y=0)
            set_expression_position(asset_path="/Game/Materials/M_Ice", node_index=1, pos_x=-800, pos_y=0)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "node_index": node_index,
                "pos_x": pos_x,
                "pos_y": pos_y,
            }

            response = unreal.send_command("set_expression_position", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            result = response.get("result", response)
            logger.info(f"Moved node {node_index} to ({pos_x}, {pos_y})")
            return result

        except Exception as e:
            logger.error(f"Error setting expression position: {e}")
            return {"success": False, "message": str(e)}

    logger.info("Material tools registered successfully")
