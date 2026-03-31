"""
UMG Tools for Unreal MCP.

This module provides tools for creating and manipulating UMG Widget Blueprints in Unreal Engine.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_umg_tools(mcp: FastMCP):
    """Register UMG tools with the MCP server."""

    @mcp.tool()
    def create_umg_widget_blueprint(
        ctx: Context,
        widget_name: str,
        parent_class: str = "",
        path: str = "/Game/Widgets"
    ) -> Dict[str, Any]:
        """
        Create a new UMG Widget Blueprint.
        
        Args:
            widget_name: Name of the widget blueprint to create (e.g. "WBP_HealthBar")
            parent_class: Parent class for the widget. Supports:
                - Native class name: "UserWidget" (default if empty)
                - Blueprint asset path: "/Game/UI/WBP_BaseHUD"
                - Full class path: "/Script/UMG.UserWidget"
            path: Content browser folder path (default: "/Game/Widgets")
            
        Returns:
            Dict containing name, path, and parent_class of the created widget
            
        Examples:
            create_umg_widget_blueprint("WBP_MainMenu")
            create_umg_widget_blueprint("WBP_HealthBar", parent_class="/Game/UI/WBP_BaseWidget")
            create_umg_widget_blueprint("WBP_HUD", path="/Game/UI/HUD")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "name": widget_name,
                "path": path
            }
            if parent_class:
                params["parent_class"] = parent_class
            
            logger.info(f"Creating UMG Widget Blueprint with params: {params}")
            response = unreal.send_command("create_umg_widget_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Create UMG Widget Blueprint response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error creating UMG Widget Blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_text_block_to_widget(
        ctx: Context,
        path: str,
        text_block_name: str,
        text: str = "",
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 50.0],
        font_size: int = 12,
        color: List[float] = [1.0, 1.0, 1.0, 1.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a Text Block widget to a UMG Widget Blueprint.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            text_block_name: Name to give the new Text Block
            text: Initial text content
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the text block (only used when adding to root CanvasPanel)
            font_size: Font size in points
            color: [R, G, B, A] color values (0.0 to 1.0)
            parent_name: Name of an existing container widget (e.g. VBox, HBox, Overlay, Border) to nest this widget inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing success status and text block properties
            
        Examples:
            add_text_block_to_widget("/Game/UI/WBP_HUD", "Title", text="Hello World")
            add_text_block_to_widget("/Game/UI/WBP_HUD", "Label", text="HP:", parent_name="StatsVBox")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": text_block_name,
                "text": text,
                "position": position,
                "size": size,
                "font_size": font_size,
                "color": color
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            logger.info(f"Adding Text Block to widget with params: {params}")
            response = unreal.send_command("add_text_block_to_widget", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add Text Block response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding Text Block to widget: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_button_to_widget(
        ctx: Context,
        path: str,
        button_name: str,
        text: str = "",
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 50.0],
        font_size: int = 12,
        color: List[float] = [1.0, 1.0, 1.0, 1.0],
        background_color: List[float] = [0.1, 0.1, 0.1, 1.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a Button widget to a UMG Widget Blueprint.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            button_name: Name to give the new Button
            text: Text to display on the button
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the button (only used when adding to root CanvasPanel)
            font_size: Font size for button text
            color: [R, G, B, A] text color values (0.0 to 1.0)
            background_color: [R, G, B, A] button background color values (0.0 to 1.0)
            parent_name: Name of an existing container widget to nest this button inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing success status and button properties
            
        Examples:
            add_button_to_widget("/Game/UI/WBP_Menu", "StartBtn", text="Start Game")
            add_button_to_widget("/Game/UI/WBP_Menu", "StartBtn", text="Start", parent_name="ButtonsVBox")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": button_name,
                "text": text,
                "position": position,
                "size": size,
                "font_size": font_size,
                "color": color,
                "background_color": background_color
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            logger.info(f"Adding Button to widget with params: {params}")
            response = unreal.send_command("add_button_to_widget", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add Button response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding Button to widget: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def bind_widget_event(
        ctx: Context,
        path: str,
        widget_component_name: str,
        event_name: str,
        function_name: str = ""
    ) -> Dict[str, Any]:
        """
        Bind an event on a widget component to a function.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_Menu")
            widget_component_name: Name of the widget component (button, etc.)
            event_name: Name of the event to bind (OnClicked, etc.)
            function_name: Name of the function to create/bind to (defaults to f"{widget_component_name}_{event_name}")
            
        Returns:
            Dict containing success status and binding information
            
        Examples:
            bind_widget_event("/Game/UI/WBP_Menu", "StartBtn", "OnClicked")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # If no function name provided, create one from component and event names
            if not function_name:
                function_name = f"{widget_component_name}_{event_name}"
            
            params = {
                "blueprint_name": path,
                "widget_name": widget_component_name,
                "event_name": event_name,
                "function_name": function_name
            }
            
            logger.info(f"Binding widget event with params: {params}")
            response = unreal.send_command("bind_widget_event", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Bind widget event response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error binding widget event: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_widget_to_viewport(
        ctx: Context,
        path: str,
        z_order: int = 0
    ) -> Dict[str, Any]:
        """
        Add a Widget Blueprint instance to the viewport.
        
        Args:
            path: Full asset path of the Widget Blueprint to add (e.g. "/Game/UI/WBP_HUD")
            z_order: Z-order for the widget (higher numbers appear on top)
            
        Returns:
            Dict containing success status and widget instance information
            
        Examples:
            add_widget_to_viewport("/Game/UI/WBP_HUD")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "z_order": z_order
            }
            
            logger.info(f"Adding widget to viewport with params: {params}")
            response = unreal.send_command("add_widget_to_viewport", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add widget to viewport response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding widget to viewport: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_text_block_binding(
        ctx: Context,
        path: str,
        text_block_name: str,
        binding_property: str,
        binding_type: str = "Text"
    ) -> Dict[str, Any]:
        """
        Set up a property binding for a Text Block widget.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            text_block_name: Name of the Text Block to bind
            binding_property: Name of the property to bind to
            binding_type: Type of binding (Text, Visibility, etc.)
            
        Returns:
            Dict containing success status and binding information
            
        Examples:
            set_text_block_binding("/Game/UI/WBP_HUD", "HealthText", "CurrentHealth")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": text_block_name,
                "binding_name": binding_property,
                "binding_type": binding_type
            }
            
            logger.info(f"Setting text block binding with params: {params}")
            response = unreal.send_command("set_text_block_binding", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set text block binding response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting text block binding: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_progress_bar_to_widget(
        ctx: Context,
        path: str,
        progress_bar_name: str,
        percent: float = 1.0,
        fill_color: List[float] = [1.0, 0.0, 0.0, 1.0],
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 20.0],
        fill_type: str = "LeftToRight",
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a ProgressBar widget to a UMG Widget Blueprint.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HealthBar")
            progress_bar_name: Name to give the new ProgressBar
            percent: Initial fill percentage from 0.0 (empty) to 1.0 (full). Default: 1.0
            fill_color: [R, G, B, A] fill color values (0.0 to 1.0). Default: red [1,0,0,1]
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the progress bar (only used when adding to root CanvasPanel). Default: [200, 20]
            fill_type: Fill direction. One of: "LeftToRight", "RightToLeft", "TopToBottom",
                       "BottomToTop", "FillFromCenter", "FillFromCenterHorizontal",
                       "FillFromCenterVertical". Default: "LeftToRight"
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing path, percent, fill_color, and size
            
        Examples:
            add_progress_bar_to_widget("/Game/UI/WBP_HealthBar", "HealthBar", percent=0.75, fill_color=[1,0,0,1])
            add_progress_bar_to_widget("/Game/UI/WBP_HUD", "HPBar", parent_name="HealthOverlay")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": progress_bar_name,
                "percent": percent,
                "fill_color": fill_color,
                "position": position,
                "size": size,
                "fill_type": fill_type
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            logger.info(f"Adding ProgressBar to widget with params: {params}")
            response = unreal.send_command("add_progress_bar_to_widget", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add ProgressBar response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding ProgressBar to widget: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_image_to_widget(
        ctx: Context,
        path: str,
        image_name: str,
        texture_path: str = "",
        tint_color: List[float] = [1.0, 1.0, 1.0, 1.0],
        position: List[float] = [0.0, 0.0],
        size: List[float] = [100.0, 100.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add an Image widget to a UMG Widget Blueprint.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            image_name: Name to give the new Image widget
            texture_path: Full asset path of a Texture2D to display. Optional.
            tint_color: [R, G, B, A] tint color (0.0 to 1.0). Default: white
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the image (only used when adding to root CanvasPanel). Default: [100, 100]
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name and texture_path
            
        Examples:
            add_image_to_widget("/Game/UI/WBP_HUD", "BgImage", size=[1920, 1080])
            add_image_to_widget("/Game/UI/WBP_HUD", "Icon", parent_name="IconOverlay")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": image_name,
                "tint_color": tint_color,
                "position": position,
                "size": size
            }
            if texture_path:
                params["texture_path"] = texture_path
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_image_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding Image to widget: {e}"}

    @mcp.tool()
    def add_vertical_box_to_widget(
        ctx: Context,
        path: str,
        box_name: str,
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 400.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a VerticalBox layout container to a UMG Widget Blueprint.
        Children added to this box will stack top-to-bottom.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            box_name: Name to give the new VerticalBox
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the box (only used when adding to root CanvasPanel). Default: [200, 400]
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name
            
        Examples:
            add_vertical_box_to_widget("/Game/UI/WBP_Menu", "MenuItems")
            add_vertical_box_to_widget("/Game/UI/WBP_Menu", "InnerVBox", parent_name="OuterBorder")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": box_name,
                "position": position,
                "size": size
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_vertical_box_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding VerticalBox to widget: {e}"}

    @mcp.tool()
    def add_horizontal_box_to_widget(
        ctx: Context,
        path: str,
        box_name: str,
        position: List[float] = [0.0, 0.0],
        size: List[float] = [400.0, 200.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a HorizontalBox layout container to a UMG Widget Blueprint.
        Children added to this box will stack left-to-right.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            box_name: Name to give the new HorizontalBox
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the box (only used when adding to root CanvasPanel). Default: [400, 200]
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name
            
        Examples:
            add_horizontal_box_to_widget("/Game/UI/WBP_HUD", "ActionBar")
            add_horizontal_box_to_widget("/Game/UI/WBP_HUD", "ButtonRow", parent_name="MainVBox")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": box_name,
                "position": position,
                "size": size
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_horizontal_box_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding HorizontalBox to widget: {e}"}

    @mcp.tool()
    def add_overlay_to_widget(
        ctx: Context,
        path: str,
        overlay_name: str,
        position: List[float] = [0.0, 0.0],
        size: List[float] = [300.0, 300.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add an Overlay container to a UMG Widget Blueprint.
        Children stack on top of each other (z-order).
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            overlay_name: Name to give the new Overlay
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the overlay (only used when adding to root CanvasPanel). Default: [300, 300]
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name
            
        Examples:
            add_overlay_to_widget("/Game/UI/WBP_HealthBar", "BarOverlay")
            add_overlay_to_widget("/Game/UI/WBP_HUD", "Overlay1", parent_name="MainVBox")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": overlay_name,
                "position": position,
                "size": size
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_overlay_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding Overlay to widget: {e}"}

    @mcp.tool()
    def add_size_box_to_widget(
        ctx: Context,
        path: str,
        size_box_name: str,
        width_override: float = 0.0,
        height_override: float = 0.0,
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 200.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a SizeBox to a UMG Widget Blueprint to constrain child dimensions.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            size_box_name: Name to give the new SizeBox
            width_override: Fixed width constraint. 0 = no override.
            height_override: Fixed height constraint. 0 = no override.
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the size box slot (only used when adding to root CanvasPanel). Default: [200, 200]
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name and override values
            
        Examples:
            add_size_box_to_widget("/Game/UI/WBP_HUD", "HealthBarWrapper", width_override=300, height_override=30)
            add_size_box_to_widget("/Game/UI/WBP_HUD", "Wrapper", parent_name="MainVBox", width_override=200)
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": size_box_name,
                "position": position,
                "size": size
            }
            if width_override > 0:
                params["width_override"] = width_override
            if height_override > 0:
                params["height_override"] = height_override
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_size_box_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding SizeBox to widget: {e}"}

    @mcp.tool()
    def add_border_to_widget(
        ctx: Context,
        path: str,
        border_name: str,
        background_color: List[float] = [0.1, 0.1, 0.1, 1.0],
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 200.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a Border widget to a UMG Widget Blueprint.
        A Border has a background color/brush and one child slot.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            border_name: Name to give the new Border
            background_color: [R, G, B, A] background color (0.0 to 1.0). Default: dark gray
            position: [X, Y] position (only used when adding to root CanvasPanel)
            size: [Width, Height] of the border (only used when adding to root CanvasPanel). Default: [200, 200]
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name
            
        Examples:
            add_border_to_widget("/Game/UI/WBP_HUD", "PanelBg", background_color=[0, 0, 0, 0.8])
            add_border_to_widget("/Game/UI/WBP_HUD", "Inner", parent_name="MainVBox")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": border_name,
                "background_color": background_color,
                "position": position,
                "size": size
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_border_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding Border to widget: {e}"}

    @mcp.tool()
    def add_spacer_to_widget(
        ctx: Context,
        path: str,
        spacer_name: str,
        size: List[float] = [100.0, 20.0],
        position: List[float] = [0.0, 0.0],
        parent_name: str = ""
    ) -> Dict[str, Any]:
        """
        Add a Spacer widget for padding/spacing between elements.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            spacer_name: Name to give the new Spacer
            size: [Width, Height] of the spacer. Default: [100, 20]
            position: [X, Y] position (only used when adding to root CanvasPanel)
            parent_name: Name of an existing container widget to nest this inside. If empty, adds to root CanvasPanel.
            
        Returns:
            Dict containing widget_name
            
        Examples:
            add_spacer_to_widget("/Game/UI/WBP_Menu", "MenuSpacer", size=[200, 40])
            add_spacer_to_widget("/Game/UI/WBP_Menu", "Gap", parent_name="MenuVBox", size=[0, 20])
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": spacer_name,
                "size": size,
                "position": position
            }
            if parent_name:
                params["parent_name"] = parent_name
            
            response = unreal.send_command("add_spacer_to_widget", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error adding Spacer to widget: {e}"}

    @mcp.tool()
    def set_widget_anchor(
        ctx: Context,
        path: str,
        widget_name: str,
        anchor: List[float] = [0.0, 0.0, 0.0, 0.0],
        alignment: List[float] = [0.0, 0.0],
        offset: List[float] = None,
        position: List[float] = None,
        size: List[float] = None
    ) -> Dict[str, Any]:
        """
        Set anchor, alignment, offset, and size on any existing widget in a CanvasPanel.
        
        Args:
            path: Full asset path of the target Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            widget_name: Name of the widget to modify (must already exist in the blueprint)
            anchor: [MinX, MinY, MaxX, MaxY] anchor values (0.0 to 1.0).
                Common presets:
                  Top-Left: [0,0,0,0], Top-Center: [0.5,0,0.5,0], Top-Right: [1,0,1,0]
                  Center: [0.5,0.5,0.5,0.5], Stretch-All: [0,0,1,1]
                  Bottom-Center: [0.5,1,0.5,1]
            alignment: [X, Y] pivot alignment (0.0 to 1.0). [0.5, 0.5] = centered on position.
            offset: [Left, Top, Right, Bottom] margin offsets for stretched anchors. Optional.
            position: [X, Y] position override. Optional.
            size: [Width, Height] size override. Optional.
            
        Returns:
            Dict containing widget_name and success status
            
        Examples:
            set_widget_anchor("/Game/UI/WBP_HUD", "HealthBar", anchor=[0.5, 1, 0.5, 1], alignment=[0.5, 1])
            set_widget_anchor("/Game/UI/WBP_HUD", "FullScreenBg", anchor=[0, 0, 1, 1], offset=[0, 0, 0, 0])
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path,
                "widget_name": widget_name,
                "anchor": anchor,
                "alignment": alignment
            }
            if offset is not None:
                params["offset"] = offset
            if position is not None:
                params["position"] = position
            if size is not None:
                params["size"] = size
            
            response = unreal.send_command("set_widget_anchor", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error setting widget anchor: {e}"}

    @mcp.tool()
    def read_widget_layout(
        ctx: Context,
        path: str
    ) -> Dict[str, Any]:
        """
        Read the full widget tree layout of a UMG Widget Blueprint.
        Returns a recursive tree of all widgets with their type, slot info, and properties.
        This is a read-only tool — it does not modify anything.
        
        Args:
            path: Full asset path of the Widget Blueprint (e.g. "/Game/UI/WBP_HUD")
            
        Returns:
            Dict with "path", "parent_class", and "root" containing the recursive widget tree.
            Each node has: name, type, slot (position/size/anchor/alignment), properties, children.
            
        Examples:
            read_widget_layout("/Game/UI/WBP_TowerInteraction")
            read_widget_layout("/Game/UI/WBP_HealthBar")
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": path
            }
            
            response = unreal.send_command("read_widget_layout", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
            
        except Exception as e:
            return {"success": False, "message": f"Error reading widget layout: {e}"}

    logger.info("UMG tools registered successfully") 