---
name: ue-state-tree
description: "Use this skill when working with StateTree assets, reading/analyzing state tree structures, or debugging StateTree AI behavior. Covers task execution semantics, state transitions, completion modes, and key UE5 source references."
metadata:
  version: 1.0.0
---

# UE5 StateTree Reference (for AI Agents)

## Critical Behavior Notes (Common Misconceptions)

### Tasks Execute Concurrently (All Tick Each Frame), Not Sequentially
All tasks within a state are ticked **every frame** in order. They are NOT sequential steps
where one finishes before the next starts.
- `Pick Next Target` + `Move To Location` in the same state = both tick every frame simultaneously
- This is fundamentally different from Behavior Trees where tasks are sequential leaves
- However, if an earlier task **fails**, subsequent tasks stop ticking (failure propagates forward)
- Task tick order matches the order they appear in the editor (top to bottom)
- Source: `StateTreeExecutionContext.cpp` `TickTasks()` — iterates all tasks in a for-loop, calling `Task.Tick()` on each

### TasksCompletion Mode (the "Any/All" dropdown)
Each state has a `TasksCompletion` field (default varies, check `tasks_completion` in `read_state_tree` output):
- **Any**: State completes when ANY single task reports success/failure. If any task succeeds, state succeeds. If any fails, state fails.
- **All**: State completes only when ALL tasks have completed. A mix of Succeeded+Stopped counts as Succeeded.
- **Failure always wins**: Regardless of Any/All, if any task fails, the completion status is Failed.
- Source: `StateTreeTasksStatus.h` `GetCompletionStatus()` — bitwise check on completion masks

### State Selection Behavior
- **TrySelectChildrenInOrder**: Tries child states top-to-bottom, enters first one whose conditions pass
- **TryFollowTransitions**: Only follows explicit transitions
- **TryEnterState**: Enters this state directly without checking children

### Transitions
- Completion triggers: OnStateCompleted (both), OnStateSucceeded, OnStateFailed
- Tick/event triggers: OnTick, OnEvent, OnDelegate
- Multiple transitions can exist; first matching one wins (checked bottom-to-top for completion)
- Transitions have priority: Normal, High, Critical
- `link_type` values: GotoState, NextState, NextSelectableState, Succeeded, Failed, None

## Source Code Paths (UE 5.5+)

Read these files for authoritative behavior when the summary above isn't enough:

| Concept | Source Path | What It Contains |
|---------|-------------|------------------|
| StateTree asset | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTree.h` | Root structure, BlackboardAsset ref |
| State definition | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeState.h` | UStateTreeState: Tasks[], Transitions[], Children[], EnterConditions[], TasksCompletion |
| Editor data | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h` | UStateTreeEditorData: SubTrees[], Evaluators[], GlobalTasks[], Parameters |
| Editor node | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorNode.h` | FStateTreeEditorNode: Node (FInstancedStruct), Instance, InstanceObject |
| Task execution | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Private/StateTreeExecutionContext.cpp` | `TickTasks()` at ~line 4672 — proves concurrent tick, failure propagation |
| Task completion | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeTasksStatus.h` | `EStateTreeTaskCompletionType` (Any/All), `GetCompletionStatus()` bitwise logic |
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
- State, Group, Linked, LinkedAsset, Subtree

### EStateTreeTransitionTrigger
- OnStateCompleted (= OnStateSucceeded | OnStateFailed), OnStateSucceeded, OnStateFailed, OnTick, OnEvent, OnDelegate

### EStateTreeTransitionType
- None, Succeeded, Failed, GotoState, NextState, NextSelectableState

### EStateTreeTaskCompletionType
- All, Any

### EStateTreeStateSelectionBehavior
- None, TryEnterState, TrySelectChildrenInOrder, TrySelectChildrenAtRandom, TrySelectChildrenWithHighestUtility, TrySelectChildrenAtRandomWeightedByUtility, TryFollowTransitions

### No-Transition Fallback
When a state completes but no completion transition matches, the tree **automatically jumps back to Root** and re-selects children. If Root selection also fails, the tree stops with Failed.
Source: `StateTreeExecutionContext.cpp` ~line 5933: `"Could not trigger completion transition, jump back to root state."`
