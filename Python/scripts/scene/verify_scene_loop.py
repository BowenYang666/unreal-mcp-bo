"""Explicit, resumable scene acceptance through real MCP stdio; never retries edits."""

import argparse
import asyncio
import json
import hashlib
import os
from pathlib import Path
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def run(arguments):
    receipt_path = Path(arguments.receipt)
    state = json.loads(receipt_path.read_text(encoding="utf-8")) if receipt_path.exists() else {}
    environment = {**os.environ, "UNREAL_MCP_PORT": str(arguments.port), "UNREAL_MCP_READ_ONLY": "0"}
    server = Path(__file__).resolve().parents[2] / "unreal_mcp_server.py"
    parameters = StdioServerParameters(command=sys.executable, args=[str(server)], env=environment, cwd=str(server.parent))
    async with stdio_client(parameters) as streams, ClientSession(*streams) as session:
        await session.initialize()
        public_tools = {tool.name for tool in (await session.list_tools()).tools}
        contracts = {}

        async def invoke(name, **payload):
            response = await session.call_tool(name, payload)
            result = json.loads(response.content[0].text)
            print(json.dumps({"tool": name, "result": result}, ensure_ascii=False), flush=True)
            if response.isError or result.get("success") is False:
                raise RuntimeError(f"{name} failed; inspect receipt/state before retrying")
            return result

        async def call(name, **payload):
            if name in public_tools:
                return await invoke(name, **payload)
            if "scene_search" not in public_tools:
                raise RuntimeError(f"Tool {name} is unavailable; no alternate route attempted")
            if name not in contracts:
                contracts[name] = await invoke("scene_search", tool=name)
            return await invoke(contracts[name]["call_tool"], tool=name, arguments=payload)

        async def material(name, **payload):
            if name in public_tools:
                return await invoke(name, **payload)
            if "material_search" not in public_tools:
                raise RuntimeError(f"Material operation {name} is unavailable")
            if name not in contracts:
                contracts[name] = await invoke("material_search", tool=name)
            return await invoke(contracts[name]["call_tool"], tool=name, arguments=payload)

        context = await call("get_editor_context")
        if os.path.normcase(os.path.normpath(context["project_path"])) != os.path.normcase(os.path.normpath(arguments.project)):
            raise RuntimeError("Wrong editor project; no mutation performed")
        if arguments.action == "context":
            print("Public tool count:", len((await session.list_tools()).tools))
            return
        if not arguments.level.startswith("/Game/__Dev/SceneTools/"):
            raise RuntimeError("Acceptance is restricted to /Game/__Dev/SceneTools/")
        identity = {"project_path": arguments.project, "level_path": arguments.level}

        def record():
            receipt_path.parent.mkdir(parents=True, exist_ok=True)
            receipt_path.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")

        if arguments.action == "setup":
            if state or context["dirty_packages"]:
                raise RuntimeError("Existing receipt or dirty packages; refusing setup/map switch")
            await call("create_level", level_path=arguments.level, partitioned=False)
            state.update(identity)
            state["actors"] = {}
            record()

            async def create(kind, identifier, **transform):
                result = await call("manage_scene_actor", **identity, operation="create", actor_type=kind, managed_id=identifier, **transform)
                state["actors"][identifier] = result["actor_path"]
                record()
                return result["actor_path"]

            async def component(actor_path, class_suffix):
                result = await call("inspect_scene_target", **identity, actor_path=actor_path)
                matches = [item["name"] for item in result["components"] if item["class_path"].endswith(class_suffix)]
                if len(matches) != 1:
                    raise RuntimeError("Ambiguous component")
                return matches[0]

            for identifier, location, scale in (
                ("Floor", [0, 0, -20], [12, 12, 0.2]),
                ("Wall", [400, 0, 150], [0.2, 12, 3]),
                ("Block", [0, 0, 70], [1.4, 1.4, 1.4]),
            ):
                actor = await create("StaticMeshActor", identifier, location=location, scale=scale)
                name = await component(actor, "StaticMeshComponent")
                await call("set_scene_mesh", **identity, actor_path=actor, component_name=name,
                           static_mesh="/Engine/BasicShapes/Cube.Cube")
            light = await create("RectLight", "KeyLight", location=[-250, -200, 300], rotation=[-35, 35, 0])
            state["light_component"] = await component(light, "RectLightComponent")
            await call("patch_scene_target", **identity, actor_path=light, component_name=state["light_component"], changes=[
                {"path": "Mobility", "value": "Movable"}, {"path": "Intensity", "value": 2000},
                {"path": "SourceWidth", "value": 150}, {"path": "SourceHeight", "value": 150},
                {"path": "AttenuationRadius", "value": 2500}])
            volume = await create("PostProcessVolume", "Exposure")
            await call("patch_scene_target", **identity, actor_path=volume, changes=[
                {"path": "Settings.AutoExposureMinBrightness", "value": 0},
                {"path": "Settings.AutoExposureMaxBrightness", "value": 0},
                {"path": "Settings.AutoExposureBias", "value": 0},
                {"path": "Settings.BloomIntensity", "value": 0}])
            camera = await create("CameraActor", "Camera", location=[-650, -750, 340], rotation=[-15, 48, 0])
            current = await call("get_editor_context")
            views = [view for view in current["viewports"] if view["perspective"]]
            if len(views) != 1:
                raise RuntimeError("Expected one perspective Level Viewport")
            state["viewport_id"] = views[0]["viewport_id"]
            await call("set_scene_viewport", **identity, viewport_id=state["viewport_id"], camera_path=camera, game_view=True)
            state["setup_completed"] = True
            record()
            return
        if state.get("level_path") != arguments.level or state.get("project_path") != arguments.project:
            raise RuntimeError("Receipt identity mismatch")
        if context["level_path"] != arguments.level:
            raise RuntimeError("Current map differs; no mutation performed")
        if arguments.action == "sky-frame":
            if context["mode"] != "editor" or context["scene_task_active"]:
                raise RuntimeError("Editor is busy or in Play")
            state["sky_view_before"] = await call("get_scene_viewport", **identity, viewport_id=state["viewport_id"])
            await call("set_scene_viewport", **identity, viewport_id=state["viewport_id"], camera_path=state["actors"]["Camera"], game_view=True)
            record()
            return
        if arguments.action == "sky-apply":
            if state.get("daylight"):
                raise RuntimeError("Daylight edit already started; inspect its receipt, never replay blindly")
            if context["mode"] != "editor" or context["scene_task_active"] or context["dirty_packages"]:
                raise RuntimeError("Daylight setup requires idle editor and no pre-existing dirty packages")
            inventory = await call("list_scene_actors", **identity, limit=200)
            if inventory["has_more"]:
                raise RuntimeError("Incomplete actor inventory")
            def unique_actor(class_name, required=False):
                matches = [actor for actor in inventory["actors"] if actor["class_path"] == "/Script/Engine." + class_name]
                if len(matches) > 1 or (required and not matches):
                    raise RuntimeError(f"Ambiguous/missing {class_name}; no edits started")
                return matches[0]["actor_path"] if matches else None
            skylight = unique_actor("SkyLight", True)
            volume = unique_actor("PostProcessVolume", True)
            sun = unique_actor("DirectionalLight")
            atmosphere = unique_actor("SkyAtmosphere")
            fog = unique_actor("ExponentialHeightFog")
            if sun or atmosphere or fog:
                raise RuntimeError("An environment was added since inspection; inspect it before replacing settings")
            map_file = Path(arguments.project).parent / "Content" / (arguments.level.removeprefix("/Game/") + ".umap")
            state["daylight"] = {"before_actors": inventory["actors"], "before_context": context,
                "map_sha256_before": hashlib.sha256(map_file.read_bytes()).hexdigest(), "operations": [], "phase": "started"}
            record()
            async def daylight_call(name, **payload):
                result = await call(name, **identity, **payload)
                state["daylight"]["operations"].append({"tool": name, "arguments": payload, "result": result})
                record()
                return result
            async def component_path(actor, suffix):
                inspection = await call("inspect_scene_target", **identity, actor_path=actor)
                matches = [item["name"] for item in inspection["components"] if item["class_path"].endswith(suffix)]
                if len(matches) != 1:
                    raise RuntimeError(f"Ambiguous {suffix}")
                return matches[0]
            async def patch_fields(actor, values, component=""):
                inspection = await call("inspect_scene_target", **identity, actor_path=actor, component_name=component, property_paths=list(values))
                before = {item["path"]: item["value"] for item in inspection["properties"]}
                return await daylight_call("patch_scene_target", actor_path=actor, component_name=component,
                    changes=[{"path": name, "value": value, "expected_value": before[name]} for name, value in values.items()])
            sky_component = await component_path(skylight, "SkyLightComponent")
            await patch_fields(volume, {"Settings.AutoExposureMinBrightness": 12, "Settings.AutoExposureMaxBrightness": 12,
                "Settings.AutoExposureBias": 0, "Settings.WhiteTemp": 6500})
            atmosphere = (await daylight_call("manage_scene_actor", operation="create", actor_type="SkyAtmosphere",
                managed_id="DaylightAtmosphere_20260929", location=[0, 0, 0]))["actor_path"]
            await patch_fields(atmosphere, {"RayleighScatteringScale": 0.0331, "MieScatteringScale": 0.0032,
                "MieAnisotropy": 0.8, "AerialPespectiveViewDistanceScale": 1}, await component_path(atmosphere, "SkyAtmosphereComponent"))
            sun = (await daylight_call("manage_scene_actor", operation="create", actor_type="DirectionalLight",
                managed_id="DaylightSun_20260929", location=[-400, -200, 600], rotation=[-38, -25, 0]))["actor_path"]
            await patch_fields(sun, {"Mobility": "Movable", "Intensity": 50000, "Temperature": 5700,
                "bUseTemperature": True, "bAtmosphereSunLight": True, "AtmosphereSunLightIndex": 0, "LightSourceAngle": 0.535},
                await component_path(sun, "DirectionalLightComponent"))
            fog = (await daylight_call("manage_scene_actor", operation="create", actor_type="HeightFog",
                managed_id="DaylightFog_20260929", location=[0, 0, 0]))["actor_path"]
            await patch_fields(fog, {"FogDensity": 0.003, "FogHeightFalloff": 0.2, "StartDistance": 500,
                "FogInscatteringLuminance": {"R": 0.52, "G": 0.64, "B": 0.8, "A": 1}, "bEnableVolumetricFog": False},
                await component_path(fog, "ExponentialHeightFogComponent"))
            await patch_fields(skylight, {"Mobility": "Movable", "Intensity": 1.2, "SourceType": "SLS_CapturedScene", "bRealTimeCapture": True}, sky_component)
            await daylight_call("recapture_scene_skylight", actor_path=skylight, component_name=sky_component)
            after = await call("list_scene_actors", **identity, limit=200)
            by_path = {actor["actor_path"]: actor for actor in after["actors"]}
            for original in inventory["actors"]:
                actual = by_path.get(original["actor_path"])
                if actual is None or any(actual[key] != original[key] for key in ("location", "rotation", "scale", "class_path")):
                    raise RuntimeError("An existing actor transform/identity changed")
            state["daylight"]["map_sha256_after"] = hashlib.sha256(map_file.read_bytes()).hexdigest()
            if state["daylight"]["map_sha256_after"] != state["daylight"]["map_sha256_before"]:
                raise RuntimeError("Preview unexpectedly changed saved map")
            state["daylight"]["after_context"] = await call("get_editor_context")
            state["daylight"]["phase"] = "preview_ready"
            record()
            return
        if arguments.action == "sky-refine":
            if context["mode"] != "editor" or context["scene_task_active"] or not state.get("daylight"):
                raise RuntimeError("Daylight preview unavailable or busy")
            created = [operation for operation in state["daylight"]["operations"]
                if operation["tool"] == "manage_scene_actor" and operation["arguments"].get("actor_type") == "SkyAtmosphere"]
            if len(created) != 1:
                raise RuntimeError("Ambiguous owned atmosphere")
            actor = created[0]["result"]["actor_path"]
            inspection = await call("inspect_scene_target", **identity, actor_path=actor)
            components = [item["name"] for item in inspection["components"] if item["class_path"].endswith("SkyAtmosphereComponent")]
            if len(components) != 1:
                raise RuntimeError("Ambiguous atmosphere component")
            values = {"RayleighScatteringScale": 0.0331, "MieScatteringScale": 0.0032}
            actual = await call("inspect_scene_target", **identity, actor_path=actor, component_name=components[0], property_paths=list(values))
            changes = [{"path": item["path"], "expected_value": item["value"], "value": values[item["path"]]} for item in actual["properties"]]
            result = await call("patch_scene_target", **identity, actor_path=actor, component_name=components[0], changes=changes)
            state["daylight"]["operations"].append({"tool": "patch_scene_target", "arguments": {"actor_path": actor, "component_name": components[0], "changes": changes}, "result": result})
            record()
            return
        if arguments.action == "sky-inspect":
            actors = await call("list_scene_actors", **identity, limit=200)
            if actors["has_more"]:
                raise RuntimeError("Scene exceeds inspection page; refusing incomplete inventory")
            environment = []
            classes = {"DirectionalLight", "SkyAtmosphere", "SkyLight", "ExponentialHeightFog", "PostProcessVolume", "RectLight"}
            for actor in actors["actors"]:
                if actor["class_path"].split(".")[-1] not in classes:
                    continue
                entry = {"actor": actor, "inspection": await call("inspect_scene_target", **identity, actor_path=actor["actor_path"])}
                entry["components"] = []
                for component in entry["inspection"].get("components", []):
                    if component["class_path"].endswith(("LightComponent", "SkyAtmosphereComponent", "ExponentialHeightFogComponent")):
                        entry["components"].append(await call("inspect_scene_target", **identity, actor_path=actor["actor_path"], component_name=component["name"]))
                environment.append(entry)
            state["sky_inspection"] = {"context": context, "actors": actors["actors"], "environment": environment}
            record()
            return
        if arguments.action == "features":
            if state.get("features_started"):
                raise RuntimeError("Features already started; inspect prior receipt, do not recreate blindly")
            state["features_started"] = True
            record()
            changed = await call("patch_scene_target", **identity, actor_path=state["actors"]["KeyLight"], component_name=state["light_component"],
                changes=[{"path": "Intensity", "value": 8100, "expected_value": 8000}])
            await call("undo_scene_edit", **identity, transaction_id=changed["transaction_id"])
            state["undo_readback"] = await call("inspect_scene_target", **identity, actor_path=state["actors"]["KeyLight"], component_name=state["light_component"], property_paths=["Intensity"])
            if state["undo_readback"]["properties"][0]["value"] != 8000:
                raise RuntimeError("Undo did not restore actual light")
            manifest = {"version": 1, "namespace": "AcceptanceV2", "source_hash": "fixture001", "units": "cm", "up_axis": "Z", "handedness": "left", "objects": [
                {"id": "Probe", "mesh": "/Engine/BasicShapes/Cube.Cube", "location": [-250, 250, 30], "rotation": [0, 15, 0], "scale": [0.6, 0.6, 0.6]}]}
            await call("apply_scene_manifest", **identity, manifest=manifest)
            state["manifest"] = await call("apply_scene_manifest", **identity, manifest=manifest, dry_run=False)
            repeated = await call("apply_scene_manifest", **identity, manifest=manifest, dry_run=False)
            if repeated["modified"]:
                raise RuntimeError("Manifest repeat was not idempotent")
            await call("undo_scene_edit", **identity, transaction_id=state["manifest"]["transaction_id"])
            for kind, identifier, location in (("CineCameraActor", "CineProbeV2", [-650, -750, 340]), ("PlayerStart", "PlayerStartV2", [0, -300, 100]), ("SkyLight", "SkyProbeV2", [0, 0, 350])):
                created = await call("manage_scene_actor", **identity, operation="create", actor_type=kind, managed_id=identifier, location=location)
                state["actors"][identifier] = created["actor_path"]
                record()
                inspected = await call("inspect_scene_target", **identity, actor_path=created["actor_path"])
                if kind == "CineCameraActor":
                    names = [item["name"] for item in inspected["components"] if item["class_path"].endswith("CineCameraComponent")]
                    if len(names) != 1:
                        raise RuntimeError("CineCamera component ambiguous")
                    state["cine"] = await call("patch_scene_target", **identity, actor_path=created["actor_path"], component_name=names[0], changes=[
                        {"path": "CurrentFocalLength", "value": 35}, {"path": "CurrentAperture", "value": 4},
                        {"path": "FocusSettings.FocusMethod", "value": "Manual"}, {"path": "FocusSettings.ManualFocusDistance", "value": 1000}])
                elif kind == "SkyLight":
                    names = [item["name"] for item in inspected["components"] if item["class_path"].endswith("SkyLightComponent")]
                    if len(names) != 1:
                        raise RuntimeError("SkyLight component ambiguous")
                    await call("recapture_scene_skylight", **identity, actor_path=created["actor_path"], component_name=names[0])
            state["features_completed"] = True
            record()
            return
        if arguments.action == "material-setup":
            if state.get("material"):
                raise RuntimeError("Material setup already started; inspect before retrying")
            parent = "/Game/__Dev/SceneTools/Materials/M_SceneIteration"
            instance = "/Game/__Dev/SceneTools/Materials/MI_SceneIteration"
            state["material"] = {"parent": parent, "instance": instance, "phase": "creating"}
            record()
            await material("create_material", asset_path=parent)
            node = await material("add_material_expression", asset_path=parent, expression_type="VectorParameter", pos_x=-400, pos_y=0)
            index = node["node_index"]
            await material("set_material_expression_property", asset_path=parent, node_index=index, property_name="ParameterName", value="Tint")
            await material("set_material_expression_property", asset_path=parent, node_index=index, property_name="DefaultValue", value={"r": 0.03, "g": 0.5, "b": 0.15, "a": 1})
            await material("connect_material_to_property", asset_path=parent, node_index=index, material_property="EmissiveColor")
            await material("read_material", path=parent)
            await material("set_expression_position", asset_path=parent, node_index=index, pos_x=-400, pos_y=0)
            await material("add_material_comment", asset_path=parent, text="Scene color iteration", pos_x=-480, pos_y=-100, size_x=500, size_y=300)
            state["material"]["graph"] = await material("read_material", path=parent)
            await call("save_asset", asset_path=parent)
            await material("create_material_instance", asset_path=instance, parent_material_path=parent)
            state["material"]["initial"] = await material("get_material_instance_parameters", path=instance)
            if state["material"]["initial"].get("parameter_contract") != 2:
                raise RuntimeError("Deployed native material contract is not v2")
            await call("set_scene_mesh", **identity, actor_path=state["actors"]["Block"], component_name="StaticMeshComponent0", materials=[{"slot_index": 0, "material_path": instance}])
            await call("set_scene_viewport", **identity, viewport_id=state["viewport_id"], camera_path=state["actors"]["Camera"], game_view=True)
            state["material"]["phase"] = "ready"
            record()
            return
        if arguments.action in ("material-update", "material-clear", "material-check", "material-save"):
            instance = state["material"]["instance"]
            if arguments.action == "material-update":
                if state["material"].get("updated"):
                    raise RuntimeError("Material update already recorded")
                asset_file = Path(arguments.project).parent / "Content" / (instance.removeprefix("/Game/") + ".uasset")
                before_hash = hashlib.sha256(asset_file.read_bytes()).hexdigest()
                state["material"]["updated"] = await material("set_material_instance_parameters", asset_path=instance, vector_params={"Tint": {"r": 0.8, "g": 0.02, "b": 0.08, "a": 1}}, save=False)
                if state["material"]["updated"]["saved"]:
                    raise RuntimeError("Preview unexpectedly saved")
                after_hash = hashlib.sha256(asset_file.read_bytes()).hexdigest()
                state["material"]["preview_file_hashes"] = {"before": before_hash, "after": after_hash}
                if before_hash != after_hash:
                    raise RuntimeError("Preview changed the on-disk asset")
            elif arguments.action == "material-clear":
                state["material"]["cleared"] = await material("set_material_instance_parameters", asset_path=instance, clear_vector_params=["Tint"], save=False)
            elif arguments.action == "material-save":
                await call("save_asset", asset_path=instance)
            state["material"]["readback"] = await material("get_material_instance_parameters", path=instance)
            record()
            return
        if arguments.action == "import":
            if not arguments.request_id or not arguments.source_file:
                raise RuntimeError("--request-id and --source-file required")
            result = await call("import_scene_asset", project_path=arguments.project, request_id=arguments.request_id,
                source_file=arguments.source_file, destination_path="/Game/__Dev/SceneTools/Import_" + arguments.request_id, save=False)
            state.setdefault("imports", {})[arguments.request_id] = result
            record()
            return
        if arguments.action == "import-status":
            result = await call("get_scene_import_status", project_path=arguments.project, request_id=arguments.request_id)
            state.setdefault("imports", {})[arguments.request_id] = result
            if result["state"] == "completed":
                for asset in result["assets"]:
                    if asset["class_path"].endswith(("StaticMesh", "Texture2D")):
                        await call("get_class_properties", asset_path=asset["asset_path"], category="Collision")
                        await call("inspect_scene_asset", project_path=arguments.project, asset_path=asset["asset_path"])
            record()
            return
        if arguments.action == "import-place":
            imported = state["imports"][arguments.request_id]
            if imported["state"] != "completed":
                raise RuntimeError("Import not completed")
            meshes = [asset["asset_path"] for asset in imported["assets"] if asset["class_path"] == "/Script/Engine.StaticMesh"]
            if len(meshes) != 1:
                raise RuntimeError("Expected one imported mesh")
            manifest = {"version": 1, "namespace": "ImportedAcceptance", "source_hash": imported["source_md5"], "units": "cm", "up_axis": "Z", "handedness": "left", "objects": [
                {"id": "ImportedCube", "mesh": meshes[0], "location": [250, -250, 40], "rotation": [0, 0, 0], "scale": [0.8, 0.8, 0.8]}]}
            await call("apply_scene_manifest", **identity, manifest=manifest)
            state["import_placement"] = await call("apply_scene_manifest", **identity, manifest=manifest, dry_run=False)
            for complex_collision in (False, True):
                hit = await call("trace_physical_material", level_path=arguments.level, start=[250, -250, 250], end=[250, -250, -200], trace_complex=complex_collision)
                if not hit.get("blocking_hit") or "ImportedCube" not in str(hit.get("actor", "")):
                    raise RuntimeError("Imported cube collision did not block")
                state.setdefault("collision", {})[str(complex_collision)] = hit
            hit = await call("trace_physical_material", level_path=arguments.level, start=[-250, -250, 250], end=[-250, -250, -200])
            if not hit.get("blocking_hit") or "Floor" not in str(hit.get("actor", "")):
                raise RuntimeError("Floor collision did not block")
            state["collision"]["floor"] = hit
            record()
            return
        if arguments.action == "save-scope":
            assets = [state["material"]["instance"]]
            for imported in state.get("imports", {}).values():
                if imported["state"] != "completed":
                    raise RuntimeError("Unfinished import; refusing save")
                assets.extend(asset["asset_path"] for asset in imported["assets"])
            packages = {asset.split(".")[0] for asset in assets} | {arguments.level}
            if not all(package.startswith("/Game/__Dev/SceneTools/") for package in packages) or set(context["dirty_packages"]) - packages:
                raise RuntimeError("Unexpected dirty packages or out-of-scope asset; refusing save")
            for asset in assets:
                await call("save_asset", asset_path=asset)
            state["final_save"] = await call("save_scene_level", **identity, confirm_all_changes_in_map=True)
            state["final_context"] = await call("get_editor_context")
            if state["final_context"]["dirty_packages"]:
                raise RuntimeError("Save left dirty packages; inspect before retry")
            record()
            return
        if arguments.action in ("capture-a", "capture-b"):
            identifier = arguments.request_id
            if not identifier:
                raise RuntimeError("--request-id required")
            result = await call("capture_scene_viewport", **identity, viewport_id=state["viewport_id"],
                                request_id=identifier, width=1280, height=720, warmup_frames=48)
            state[arguments.action] = result
        elif arguments.action == "status":
            if not arguments.request_id:
                raise RuntimeError("--request-id required")
            result = await call("get_scene_task_status", project_path=arguments.project, request_id=arguments.request_id)
            state.setdefault("captures", {})[arguments.request_id] = result
        elif arguments.action == "adjust":
            if "adjust" in state:
                raise RuntimeError("Adjustment already recorded; refusing duplicate edit")
            state["adjust"] = await call("patch_scene_target", **identity, actor_path=state["actors"]["KeyLight"],
                component_name=state["light_component"], changes=[{"path": "Intensity", "value": 8000, "expected_value": 2000}])
        elif arguments.action == "save":
            state["save"] = await call("save_scene_level", **identity, confirm_all_changes_in_map=True)
        elif arguments.action == "verify":
            if context["dirty_packages"]:
                raise RuntimeError("Dirty packages; refusing map reload")
            await call("open_level", level_path=arguments.level, save_dirty=False)
            state["reloaded_light"] = await call("inspect_scene_target", **identity, actor_path=state["actors"]["KeyLight"],
                component_name=state["light_component"], property_paths=["Intensity", "SourceWidth", "SourceHeight", "Mobility"])
            intensity = next(item["value"] for item in state["reloaded_light"]["properties"] if item["path"] == "Intensity")
            if intensity != 8000:
                raise RuntimeError("Saved intensity did not survive reopening")
            if state.get("material"):
                current = await material("get_material_instance_parameters", path=state["material"]["instance"])
                tint = next(item for item in current["available_parameters"] if item["name"] == "Tint")
                if tint["local_override"] or not tint["inherited"] or abs(tint["effective_value"]["g"] - 0.5) > 0.00001:
                    raise RuntimeError("Cleared Tint override did not survive reopening")
                state["material"]["reloaded"] = current
                mesh = await call("get_scene_mesh", **identity, actor_path=state["actors"]["Block"], component_name="StaticMeshComponent0")
                if not mesh["slots"][0]["material"].startswith(state["material"]["instance"] + "."):
                    raise RuntimeError("Material assignment did not survive reopening")
                state["reloaded_block"] = mesh
            if state.get("import_placement"):
                actor = state["import_placement"]["items"][0]["after"]["actor_path"]
                state["reloaded_import"] = await call("get_scene_mesh", **identity, actor_path=actor, component_name="StaticMeshComponent0")
                if state["reloaded_import"]["static_mesh"] != state["import_placement"]["items"][0]["requested"]["mesh"]:
                    raise RuntimeError("Imported mesh assignment did not survive reopening")
        elif arguments.action == "close":
            if context["dirty_packages"]:
                raise RuntimeError("Dirty packages; refusing editor close")
            await call("close_editor", save_all=False)
        record()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["context", "setup", "capture-a", "status", "adjust", "capture-b", "save", "verify", "close",
        "features", "material-setup", "material-update", "material-clear", "material-check", "material-save", "import", "import-status", "import-place", "save-scope", "sky-inspect", "sky-frame", "sky-apply", "sky-refine"])
    parser.add_argument("--project", required=True)
    parser.add_argument("--level", required=True)
    parser.add_argument("--receipt", required=True)
    parser.add_argument("--port", type=int, default=13091)
    parser.add_argument("--request-id", default="")
    parser.add_argument("--source-file", default="")
    asyncio.run(run(parser.parse_args()))