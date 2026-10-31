"""Read-only Lyra saved-scalability acceptance through public grouped MCP tools.

Run with uv run python scripts/niagara/verify_scalability_read.py --project-root
E:/04_UnrealProjects/GameRefProjects/LyraStarterGame. The correct editor must
already be running. Never changes assets, CVars, profiles or preview quality.
"""

import argparse
import asyncio
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
from uuid import uuid4

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


SYSTEMS = {
    "portal": "/Game/Effects/Particles/Environmental/NS_WallPortal",
    "death": "/Game/Effects/Particles/Impacts/NS_DeathCubes",
    "grenade": "/Game/Effects/Particles/Explosion/NS_Grenade_Explosion",
    "impact": "/Game/Effects/Particles/Impacts/NS_ImpactConcrete",
    "pad": "/Game/Effects/Particles/Item/NS_GunPad",
    "loading": "/Game/Effects/Particles/Item/NS_GunPad_Loading",
}
EFFECTS = {name: f"/Game/Effects/EffectTypes/EffectType_{name}" for name in (
    "World_Portal", "Weapon_Impacts", "World_Placeable", "World_Burst")}


def asset_file(root, path):
    return root / "Content" / (path.removeprefix("/Game/") + ".uasset")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


async def verify(root):
    config_path = root / ".vscode/mcp.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))["servers"]["unrealMCP"]
    if config.get("env", {}).get("UNREAL_MCP_READ_ONLY") != "1":
        raise ValueError("Acceptance requires the existing Lyra read-only profile; do not broaden permissions")
    protected = {str(asset_file(root, path)): digest(asset_file(root, path)) for path in [*SYSTEMS.values(), *EFFECTS.values()]}
    protected[str(config_path)] = digest(config_path)
    output = root / "Saved/NiagaraScalabilityAcceptance" / (datetime.now().strftime("%Y%m%d_%H%M%S_") + uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    params = StdioServerParameters(command=config["command"], args=config["args"], env={**os.environ, **config.get("env", {})})
    contracts = {}
    records = {}
    context = None
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()

            async def call(name, arguments):
                response = await session.call_tool(name, arguments)
                if response.isError:
                    raise AssertionError(response)
                result = json.loads(response.content[0].text)
                if result.get("status") == "error" or result.get("success") is False:
                    raise AssertionError(result)
                result = result.get("result", result)
                if result.get("overflow"):
                    pointer = result
                    result = json.loads(Path(pointer["file_path"]).read_text(encoding="utf-8"))
                    assert "reader_contract" in result or "contract_version" in result, pointer
                return result

            async def read(operation, arguments, label):
                if operation not in contracts:
                    contracts[operation] = await call("niagara_search", {"tool": operation})
                    assert contracts[operation]["effect"] == "read", contracts[operation]
                result = await call(contracts[operation]["call_tool"], {"tool": operation, "arguments": arguments})
                records[label] = result
                (output / f"{label}.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
                return result

            dirty_before = await call("get_unsaved_changes", {})
            for label, path in SYSTEMS.items():
                result = await read("read_niagara_system", {"asset_full_path": path}, label)
                assert result.get("reader_contract") == 2 and result.get("scalability_complete") is True, result.get("scalability")
                assert result["scalability"]["asset_path"].split(".")[0] == path, path
                if context is None:
                    context = result["read_context"]
                assert result["read_context"] == context, (label, context, result["read_context"])
                for emitter in result["emitters"]:
                    assert emitter["handle_id"] and emitter["scalability"]["handle_id"] == emitter["handle_id"], emitter
                    assert emitter["name"] == emitter["scalability"]["display_name"], emitter
                    for renderer in emitter.get("renderers", []):
                        assert "platforms" in renderer and renderer["renderer_index"] >= 0
                        assert renderer["scalability"]["complete"] is True
                print(f"Read system {label}: contract 2, complete", flush=True)
            for name, path in EFFECTS.items():
                result = await read("read_niagara_effect_type", {"asset_path": path}, name)
                assert result["contract_version"] == 1 and result["complete"] is True, result

            def emitter(system, name):
                matches = [item for item in records[system]["emitters"] if item["name"] == name]
                assert len(matches) == 1, (system, name, [item["name"] for item in records[system]["emitters"]])
                return matches[0]

            for system, name, mask in (
                ("portal", "Tunnel", 28), ("portal", "TunnelLow", 3), ("portal", "Sparkle", 31),
                ("death", "NE_SkeletalSurfaceBasedEmission", 30),
                ("death", "NE_SkeletalSurfaceBasedEmission_LOWONLY", 1), ("grenade", "Decal", 28)):
                item = emitter(system, name)
                platforms = item["scalability"]["Platforms"]
                assert platforms["QualityLevelMask"] == mask, (name, platforms)
                assert [quality["mask_enabled"] for quality in platforms["quality_levels"][:5]] == [bool(mask & (1 << index)) for index in range(5)]
            for name in ("Tunnel", "TunnelLow"):
                item = emitter("portal", name)
                assert item["enabled"] is True
                assert all(all(quality["mask_enabled"] for quality in renderer["scalability"]["Platforms"]["quality_levels"]) for renderer in item["renderers"])
            rows = emitter("portal", "Sparkle")["scalability"]["ScalabilityOverrides"]["Overrides"]
            assert len(rows) == 2 and [row["Platforms"]["QualityLevelMask"] for row in rows] == [-29, -4]
            assert rows[0]["bOverrideSpawnCountScale"] is True and rows[0]["bScaleSpawnCount"] is True
            assert abs(rows[0]["SpawnCountScale"] - 0.1) <= 0.000001
            assert rows[1]["bOverrideSpawnCountScale"] is False and rows[1]["bScaleSpawnCount"] is False and rows[1]["SpawnCountScale"] == 1
            system = records["portal"]["scalability"]
            assert system["EffectType"]["path"].split(".")[0] == EFFECTS["World_Portal"]
            assert system["bOverrideScalabilitySettings"] is False
            assert all(quality["mask_enabled"] for quality in system["Platforms"]["quality_levels"])
            portal = records["World_Portal"]
            assert portal["UpdateFrequency"] == "Continuous" and portal["CullReaction"] == "DeactivateImmediateResume"
            assert "Distance" in portal["SignificanceHandler"]["class_path"]
            assert any(row["bCullPerSystemMaxInstanceCount"] is True and row["MaxSystemInstances"] == 6 for row in portal["SystemScalabilitySettings"]["Settings"])
            for label, effect in (("impact", "Weapon_Impacts"), ("pad", "World_Placeable"), ("loading", "World_Burst")):
                assert records[label]["scalability"]["EffectType"]["path"].split(".")[0] == EFFECTS[effect]
            for name in ("Weapon_Impacts", "World_Placeable"):
                assert records[name]["UpdateFrequency"] == "Continuous"
                assert any(row["VisibilityCulling"]["bCullWhenNotRendered"] is True for row in records[name]["SystemScalabilitySettings"]["Settings"])
            assert any(row["VisibilityCulling"]["bCullWhenNotRendered"] is True and row["VisibilityCulling"]["MaxTimeWithoutRender"] == 0
                for row in records["Weapon_Impacts"]["SystemScalabilitySettings"]["Settings"])
            assert records["World_Placeable"]["CullReaction"] == "DeactivateImmediateResume"
            assert "Distance" in records["World_Placeable"]["SignificanceHandler"]["class_path"]
            assert records["World_Burst"]["UpdateFrequency"] == "SpawnOnly" and records["World_Burst"]["CullReaction"] == "Deactivate"
            modules = []
            for section in ("emitter_spawn_script", "emitter_update_script", "spawn_script", "update_script"):
                modules.extend(emitter("portal", "Sparkle").get(section, {}).get("modules", []))
            spawn_rates = [module for module in modules if module.get("script_name") == "SpawnRate"]
            assert spawn_rates, "Missing actual SpawnRate script identity"
            for module in spawn_rates:
                assert module["node_guid"] and module["script_asset_path"].startswith("/Niagara/")
                assert module["version_status"] in ("resolved", "unversioned") and module["resolved_version_guid"]
                assert "uses_exposed_default" in module and "input_pins" in module
            second = await read("read_niagara_system", {"asset_full_path": SYSTEMS["portal"]}, "portal_after")
            assert second["read_context"] == context
            assert second["scalability"]["Platforms"] == records["portal"]["scalability"]["Platforms"]
            dirty_after = await call("get_unsaved_changes", {})
            assert dirty_before == dirty_after, (dirty_before, dirty_after)
    assert all(digest(Path(path)) == before for path, before in protected.items()), "Sample package/config changed"
    receipt = {"passed": True, "scope": "saved configuration only; no eligibility/runtime diagnosis", "dirty_before": dirty_before,
        "dirty_after": dirty_after, "context": context, "protected_sha256": protected, "contracts": contracts,
        "systems": SYSTEMS, "effect_types": EFFECTS, "output": str(output)}
    (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS: Lyra saved scalability samples; context/dirty/hash unchanged; receipt={output / 'receipt.json'}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, required=True)
    asyncio.run(verify(parser.parse_args().project_root.resolve(strict=True)))
