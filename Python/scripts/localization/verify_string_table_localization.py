"""Verify a saved __Dev String Table fixture with the plugin enabled.

Run with: uv run --with polib python scripts/localization/verify_string_table_localization.py
  --project <absolute .uproject> --engine <UE root>
  --root /Game/__Dev/StringTables_<unique> [--verify-language]
The fixture must already exist. Only Saved outputs and their PO are written.
Use --asset-text with /Game/__Dev/AssetText_<unique> for the TextBind DataAsset fixture.
"""

import argparse
import json
from pathlib import Path
import subprocess

import polib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--verify-language", action="store_true")
    parser.add_argument("--asset-text", action="store_true")
    args = parser.parse_args()
    project = args.project.resolve(strict=True)
    prefix = "/Game/__Dev/AssetText_" if args.asset_text else "/Game/__Dev/StringTables_"
    if not args.root.startswith(prefix) or any(part in ("", ".", "..") for part in args.root[6:].split("/")):
        raise ValueError(f"Only an existing {prefix}<unique> fixture is allowed")
    assets = project.parent / "Content" / args.root[6:]
    for name in (("ST_Test.uasset", "DA_Test.uasset") if args.asset_text else ("ST_Status.uasset", "WBP_Reference.uasset")):
        if not (assets / name).is_file():
            raise FileNotFoundError(assets / name)
    output = project.parent / "Saved" / "StringTableLocalization" / args.root.rsplit("/", 1)[-1]
    output.mkdir(parents=True, exist_ok=False)
    executable = args.engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    common = (
        "[CommonSettings]\n"
        f"SourcePath={output.as_posix()}\nDestinationPath={output.as_posix()}\n"
        "ManifestName=MCPStringTable.manifest\nArchiveName=MCPStringTable.archive\n"
        "PortableObjectName=MCPStringTable.po\nResourceName=MCPStringTable.locres\n"
        "NativeCulture=en\nCulturesToGenerate=en\nCulturesToGenerate=zh-Hans\n\n"
    )
    configurations = {
        "Gather": (
            "[GatherTextStep0]\nCommandletClass=GatherTextFromAssets\n"
            f"IncludePathFilters={assets.as_posix()}/*\nPackageFileNameFilters=*.uasset\n"
            "ShouldGatherFromEditorOnlyData=true\nSkipGatherCache=true\n\n"
            "[GatherTextStep1]\nCommandletClass=GenerateGatherManifest\n\n"
            "[GatherTextStep2]\nCommandletClass=GenerateGatherArchive\n\n"
            "[GatherTextStep3]\nCommandletClass=InternationalizationExport\nbExportLoc=true\n"
        ),
        "Import": "[GatherTextStep0]\nCommandletClass=InternationalizationExport\nbImportLoc=true\n",
        "Compile": "[GatherTextStep0]\nCommandletClass=GenerateTextLocalizationResource\n",
    }
    results = []

    def run(phase, commands, automation=False):
        log = output / f"{phase}.log"
        arguments = [str(executable), str(project), "-UnrealMCPNoServer", "-unattended", "-nop4", "-nosplash", "-NoLiveCoding", f"-abslog={log}"]
        arguments.extend(commands)
        with (output / f"{phase}.stdout.log").open("w", encoding="utf-8") as stream:
            completed = subprocess.run(arguments, stdout=stream, stderr=subprocess.STDOUT, check=False)
        text = log.read_text(encoding="utf-8", errors="replace")
        if completed.returncode != 0 or any(marker in text for marker in ("Ensure condition failed", "Fatal error", "EXCEPTION_ACCESS_VIOLATION")):
            raise RuntimeError(f"{phase} failed, exit {completed.returncode}: {log}")
        if "Unreal MCP Module has started" not in text:
            raise RuntimeError(f"{phase} did not prove the plugin was enabled: {log}")
        if automation:
            for name in (("TextReload",) if args.asset_text else ("Reload", "Localization")):
                if f"Result={{Success}} Name={{{name}}}" not in text:
                    raise RuntimeError(f"Missing {name} success: {log}")
        elif "GatherText completed with exit code 0" not in text:
            raise RuntimeError(f"Missing successful GatherText completion: {log}")
        results.append({"phase": phase, "exit_code": completed.returncode, "log": str(log), "plugin_enabled": True})
        print(f"{phase}: PASS, exit=0, plugin enabled", flush=True)

    for phase in ("Gather", "Import", "Compile"):
        if phase == "Import":
            po = polib.pofile(str(output / "zh-Hans/MCPStringTable.po"))
            expected = {"Slowed": "\u51cf\u901f", "Stunned": "\u7729\u6655", "{Status} x{Count}": "{Status} x{Count}"}
            if args.asset_text:
                del expected["Stunned"]
            seen = set()
            for entry in po:
                if entry.msgid in expected:
                    entry.msgstr = expected[entry.msgid]
                    entry.flags = [flag for flag in entry.flags if flag != "fuzzy"]
                    seen.add(entry.msgid)
            if seen != set(expected):
                raise AssertionError(f"Gather did not return every real String Table source: {seen}")
            po.save()
        config = output / f"{phase}.ini"
        config.write_text(common + configurations[phase], encoding="utf-8")
        run(phase, ["-run=GatherText", f"-config={config}"])
    if args.verify_language:
        if args.asset_text:
            run("Languages", ["/Engine/Maps/Entry", "-RenderOffscreen", "-stdout", "-FullStdOutLogOutput", f"-MCPAssetTextRoot={args.root}",
                f"-MCPAssetTextLocalization={output}", "-ExecCmds=Automation RunTests UnrealMCP.AssetProperties.TextReload",
                "-TestExit=Automation Test Queue Empty"], automation=True)
        else:
            run("Languages", ["/Engine/Maps/Entry", "-RenderOffscreen", f"-MCPStringTableRoot={args.root}",
                f"-MCPStringTableLocalization={output}", "-ExecCmds=Automation RunTests UnrealMCP.StringTables.Reload+UnrealMCP.StringTables.Localization",
                "-TestExit=Automation Test Queue Empty"], automation=True)
    (output / "receipt.json").write_text(json.dumps({"fixture": args.root, "results": results}, indent=2), encoding="utf-8")
    print(f"Receipt: {output / 'receipt.json'}", flush=True)


if __name__ == "__main__":
    main()
