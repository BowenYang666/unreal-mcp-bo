"""Verify public Niagara authoring on new SkillTest __Dev copies; never retry writes."""

import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
from uuid import uuid4

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


async def verify(root, source, port):
    if root.name != "SkillTest" or not source.startswith("/Game/__Dev/"):
        raise ValueError("Only SkillTest and existing isolated __Dev source fixtures are allowed")
    config_path = root / ".vscode/mcp.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))["servers"]["unrealMCP"]
    if config.get("env", {}).get("UNREAL_MCP_READ_ONLY") != "0":
        raise ValueError("Existing authoring permission required; never broaden a read-only profile")
    protected = [config_path, root / "Content" / (source.removeprefix("/Game/") + ".uasset"),
                 root / "Content" / (source.removeprefix("/Game/").rsplit("/", 1)[0] + "/M_Test.uasset")]
    hashes = {str(path): digest(path) for path in protected}
    identifier = uuid4().hex
    destination = "/Game/__Dev/NiagaraAuthoring_MCP_" + identifier
    output = root / "Saved/NiagaraAuthoringAcceptance" / identifier
    output.mkdir(parents=True, exist_ok=False)
    records = {}
    contracts = {}
    params = StdioServerParameters(command=config["command"], args=config["args"],
        env={**os.environ, **config.get("env", {}), "UNREAL_MCP_PORT": str(port)})
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()

            async def call(name, arguments, label, failure=False):
                response = await session.call_tool(name, arguments)
                if response.isError:
                    (output / (label + "_mcp_error.json")).write_text(response.model_dump_json(indent=2), encoding="utf-8")
                    raise AssertionError(response)
                value = json.loads(response.content[0].text)
                value = value.get("result", value)
                if value.get("overflow"):
                    value = json.loads(Path(value["file_path"]).read_text(encoding="utf-8"))
                records[label] = value
                (output / (label + ".json")).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
                assert (value.get("success") is False) if failure else (value.get("success", True) and value.get("status") != "error"), value
                return value

            async def operation(name, arguments, label, failure=False, category="niagara"):
                if name not in contracts:
                    contracts[name] = await call(category + "_search", {"tool": name}, name + "_contract")
                return await call(contracts[name]["call_tool"], {"tool": name, "arguments": arguments}, label, failure)

            scene = await call("scene_search", {"tool": "get_editor_context"}, "scene_contract")
            context = await call(scene["call_tool"], {"tool": "get_editor_context", "arguments": {}}, "context_before")
            assert context["project_name"] == "SkillTest" and not context["dirty_packages"], context
            await call("get_class_properties", {"asset_path": source}, "source_properties")
            before_dirty = await call("get_unsaved_changes", {}, "dirty_before")
            assert before_dirty["total_unsaved"] == 0
            system = destination + "/NS_Test"
            effect = destination + "/NET_Test"
            copied = await operation("duplicate_asset", {"source_asset_path": source, "destination_asset_path": system}, "isolated_copy", category="asset")
            assert copied["saved"]
            preview = await operation("create_niagara_effect_type", {"asset_path": effect}, "create_preview")
            assert not preview["created"] and not preview["saved"]
            created = await operation("create_niagara_effect_type", {"asset_path": effect, "dry_run": False, "save": True}, "create_apply")
            assert created["created"] and created["saved"]
            await operation("create_niagara_effect_type", {"asset_path": effect, "dry_run": False}, "create_collision", True)
            effect_read = await operation("read_niagara_authoring", {"asset_path": effect, "target": "effect_type"}, "effect_before")
            edit = {"asset_path": effect, "target": "effect_type", "expected_baseline": effect_read["baseline"],
                    "changes": [{"field": "UpdateFrequency", "value": "SpawnOnly"}, {"field": "CullReaction", "value": "DeactivateImmediate"}],
                    "significance_handler": "Age"}
            preview = await operation("set_niagara_scalability", edit, "effect_preview")
            assert not preview["modified"] and not preview["saved"]
            applied = await operation("set_niagara_scalability", {**edit, "dry_run": False, "save": True}, "effect_apply")
            assert applied["saved"] and applied["after"]["significance_handler"] == "Age"
            system_read = await operation("read_niagara_authoring", {"asset_path": system}, "system_before")
            linked = await operation("set_niagara_scalability", {"asset_path": system, "expected_baseline": system_read["baseline"],
                "changes": [{"field": "EffectType", "value": effect}], "dry_run": False, "save": True}, "effect_system_binding")
            assert linked["saved"] and linked["after"]["fields"]["EffectType"].split(".")[0] == effect
            system_read = await operation("read_niagara_authoring", {"asset_path": system}, "system_user_before")
            declarations = [{"name": "User.MCP_PublicFloat", "type": "float", "value": 3.0}]
            arguments = {"asset_path": system, "expected_baseline": system_read["baseline"], "parameters": declarations}
            preview = await operation("set_niagara_user_parameters", arguments, "user_preview")
            assert preview["would_modify"] and not preview["modified"]
            draft = await operation("set_niagara_user_parameters", {**arguments, "dry_run": False}, "user_draft")
            assert draft["modified"] and not draft["saved"] and draft["package_dirty"]
            current = await operation("read_niagara_authoring", {"asset_path": system}, "draft_review")
            refused = await operation("set_niagara_user_parameters", {**arguments, "expected_baseline": current["baseline"], "dry_run": False}, "dirty_refusal", True)
            assert not refused["modified"] and not refused["saved"]
            saved = await operation("save_niagara_authoring", {"asset_path": system, "expected_baseline": current["baseline"]}, "draft_save")
            assert saved["compiled"] and saved["saved"] and not saved["package_dirty"]
            legacy = await operation("read_niagara_system", {"asset_full_path": system}, "legacy_read")
            assert legacy["reader_contract"] == 2 and legacy["scalability_complete"]
            emitter = legacy["emitters"][0]
            nodes = emitter["emitter_update_script"]["modules"]
            node = next(item for item in nodes if item.get("script_name") == "SpawnRate")
            identity = {"asset_path": system, "target": "module", "emitter_handle_id": emitter["handle_id"],
                        "script_type": "emitter_update", "module_node_guid": node["node_guid"]}
            module = await operation("read_niagara_authoring", identity, "module_before")
            binding = next(item for item in module["module"]["inputs"] if item.get("linked_parameter") == "User.MCP_Rate")
            arguments = {**{key: value for key, value in identity.items() if key != "target"},
                         "expected_baseline": module["baseline"], "input_name": binding["name"], "user_parameter": "User.MCP_Rate"}
            same = await operation("bind_niagara_module_input", {**arguments, "dry_run": False}, "linked_noop")
            assert not same["modified"] and not same["saved"]
            refused = await operation("bind_niagara_module_input", {**arguments, "user_parameter": "User.MCP_PublicFloat", "dry_run": False}, "old_link_refusal", True)
            assert not refused["modified"]
            renderer = {"asset_path": system, "target": "renderer", "emitter_handle_id": emitter["handle_id"], "renderer_index": 0}
            current = await operation("read_niagara_authoring", renderer, "renderer_before")
            assert current["material_parameters"]["complete"]
            mapped = await operation("set_niagara_material_bindings", {**{key: value for key, value in renderer.items() if key != "target"}, "expected_baseline": current["baseline"],
                "bindings": [{"material_parameter": "Brightness", "user_parameter": "User.MCP_PublicFloat"}],
                "dry_run": False, "save": True}, "material_binding_apply")
            assert mapped["compiled"] and mapped["saved"]
            final = await operation("read_niagara_system", {"asset_full_path": system}, "final_read")
            assert final["scalability_complete"] and final["emitters"][0]["renderers"][0]["material_parameters"]["complete"]
            after_dirty = await call("get_unsaved_changes", {}, "dirty_after")
            assert before_dirty == after_dirty
            after_context = await call(scene["call_tool"], {"tool": "get_editor_context", "arguments": {}}, "context_after")
            assert not after_context["dirty_packages"] and after_context["project_name"] == "SkillTest"
    assert all(digest(Path(path)) == value for path, value in hashes.items()), "Protected source/config changed"
    receipt = {"passed": True, "source": source, "created_root": destination, "protected_sha256": hashes,
               "contracts": contracts, "scope": "public authoring on new __Dev copies, not NF or visual/performance acceptance"}
    (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS: public grouped Niagara authoring; receipt=" + str(output / "receipt.json"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--port", type=int, default=13090)
    arguments = parser.parse_args()
    asyncio.run(verify(arguments.project_root, arguments.source, arguments.port))