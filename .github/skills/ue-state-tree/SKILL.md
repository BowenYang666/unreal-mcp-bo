---
name: ue-state-tree
description: "Use this skill when working with StateTree assets, reading/analyzing state tree structures, or debugging StateTree AI behavior. Covers task execution semantics, state transitions, completion modes, and key UE5 source references."
metadata:
  version: 1.0.0
---

# UE5 StateTree Reference (for AI Agents)

## Critical Behavior Notes (Common Misconceptions)

### Tasks Execute in PARALLEL, Not Sequence
All tasks within a state run **concurrently** every tick. They are NOT sequential steps.
- `Pick Next Target` + `Move To Location` in the same state = both tick every frame
- This is fundamentally different from Behavior Trees where tasks are sequential leaves

### TasksCompletion Mode (the "Any/All" dropdown)
Each state has a `TasksCompletion` field:
- **Any** (default): State completes when ANY single task reports success/failure
- **All**: State completes only when ALL tasks have completed

This is returned by `read_state_tree` as `tasks_completion` per state.

### State Selection Behavior
- **TrySelectChildrenInOrder**: Tries child states top-to-bottom, enters first one whose conditions pass
- **TryFollowTransitions**: Only follows explicit transitions
- **TryEnterState**: Enters this state directly without checking children

### Transitions
- Transitions are checked every tick (OnTick) or on state events (OnStateCompleted, OnEvent)
- Multiple transitions can exist; first matching one wins
- Transitions have priority: Normal, High, Critical
- `link_type` values: GotoState, NextState, TreeSucceeded, TreeFailed, None

## Source Code Paths (UE 5.5+)

Read these files for authoritative behavior when the summary above isn't enough:

| Concept | Source Path | What It Contains |
|---------|-------------|------------------|
| StateTree asset | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTree.h` | Root structure, BlackboardAsset ref |
| State definition | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeState.h` | UStateTreeState: Tasks[], Transitions[], Children[], EnterConditions[], TasksCompletion |
| Editor data | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h` | UStateTreeEditorData: SubTrees[], Evaluators[], GlobalTasks[], Parameters |
| Editor node | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorNode.h` | FStateTreeEditorNode: Node (FInstancedStruct), Instance, InstanceObject |
| Task execution | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Private/StateTreeExecutionContext.cpp` | `TickTasks()` — proves parallel execution |
| Task completion | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeTypes.h` | `EStateTreeTaskCompletionType` enum (Any/All) |
| Schema (AI) | `Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Public/StateTreeAIComponentSchema.h` | StateTreeAIComponentSchema: ContextActorClass, AIController |
| Transition logic | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Private/StateTreeExecutionContext.cpp` | `TriggerTransitions()` — transition priority & evaluation order |

## read_state_tree Return Format

```json
{
  "name": "ST_Enemy_Dog",
  "schema": "StateTreeAIComponentSchema",
  "global_parameters": [{"name": "HasTarget", "type": "bool"}],
  "evaluators": [],
  "global_tasks": [],
  "states": [{
    "name": "Root",
    "type": "EStateTreeStateType::State",
    "selection_behavior": "...",
    "children": [{
      "name": "Patrol",
      "tasks_completion": "EStateTreeTaskCompletionType::Any",
      "tasks": [
        {"class": "STTask_MoveToActor", "instance_properties": {...}},
        {"class": "STTask_NextWaypoint", ...}
      ],
      "transitions": [{
        "trigger": "EStateTreeTransitionTrigger::OnTick",
        "link_type": "EStateTreeTransitionType::GotoState",
        "target_state": "Chase",
        "conditions": [{"class": "StateTreeCompareBoolCondition", ...}]
      }],
      "enter_conditions": [...],
      "children": [...]
    }]
  }]
}
```

## Key Enums

### EStateTreeStateType
- State, Group, Subtree, LinkedSubtree, LinkedAsset

### EStateTreeTransitionTrigger  
- OnStateCompleted, OnTick, OnEvent

### EStateTreeTaskCompletionType
- Any, All
