---
name: ue-state-tree
description: "Use this skill when working with StateTree assets, reading/analyzing state tree structures, or debugging StateTree AI behavior. Covers task execution semantics, state transitions, completion modes, and key UE5 source references."
metadata:
  version: 1.0.3
---

# UE5 StateTree Reference (for AI Agents)

## Critical Behavior Notes (Common Misconceptions)

### Tasks Have Concurrent Lifetimes, Not Sequential Step Semantics
Active, enabled, running tasks that request ticking are processed **in editor order during the same frame**.
They are NOT sequential steps where one must finish before the next starts.
- `Pick Next Target` + `Move To Location` in the same state can both tick during the same frame
- This is fundamentally different from Behavior Trees where tasks are sequential leaves
- Disabled tasks, completed tasks, tasks that did not enter, and tasks configured not to tick are skipped
- If an earlier task that participates in completion **fails**, subsequent tasks stop ticking (failure propagates forward)
- Task tick order matches the order they appear in the editor (top to bottom)
- Source: `StateTreeExecutionContext.cpp` `TickTasks()` — iterates all tasks in a for-loop, calling `Task.Tick()` on each

### TasksCompletion Mode (the "Any/All" dropdown)
Each state has a `TasksCompletion` field (default varies, check `tasks_completion` in `read_state_tree` output):
- **Any**: State completes when any task considered for completion reports success/failure. If any succeeds, state succeeds. If any fails, state fails.
- **All**: State completes only when all tasks considered for completion have completed. A mix of Succeeded+Stopped counts as Succeeded.
- **Failure always wins**: Regardless of Any/All, if any task considered for completion fails, the completion status is Failed.
- Source: `StateTreeTasksStatus.h` `GetCompletionStatus()` — bitwise check on completion masks

### State Selection Behavior
- **TrySelectChildrenInOrder**: Tries child states top-to-bottom, enters first one whose conditions pass
- **TryFollowTransitions**: Only follows explicit transitions
- **TryEnterState**: Enters this state directly without checking children

### Transitions
- Completion triggers: OnStateCompleted (both), OnStateSucceeded, OnStateFailed
- Tick/event triggers: OnTick, OnEvent, OnDelegate
- Completion transitions are checked from the completed active state up through its parents (child to root)
- Within each state, completion transitions are checked in declaration order; the first accepted transition wins
- Transition priorities are Low, Normal, Medium, High, and Critical (`None` is hidden). Completion transitions are order-driven and requested at Normal priority; priority arbitration matters for other transition requests.
- `link_type` values: GotoState, NextState, NextSelectableState, Succeeded, Failed, None

### Utility AI (Weight & Considerations) — EXPERIMENTAL
> ⚠️ This feature is **experimental** in UE 5.5+. The API is expected to change.

Each state has a `weight` (float, default 1.0) and an optional `considerations` array.
These fields are only meaningful when the **parent** state's `selection_behavior` is:
- **TrySelectChildrenWithHighestUtility** — selects the child with the highest final score
- **TrySelectChildrenAtRandomWeightedByUtility** — random selection weighted by scores (only Score > 0 enters the pool)

**Scoring**: NOT simple multiplication. Considerations form an **expression tree**:
- Each Consideration has an `Operand` (`And`=min, `Or`=max, `Multiply`=product, `Copy`=overwrite) and `DeltaIndent` (nesting level)
- `GetScore()` returns a raw float; `GetNormalizedScore()` normalizes it for the expression
- Final utility = `Weight × ExpressionResult`
- Source: `StateTreeExecutionContext.cpp` `EvaluateUtilityWithValidation()` ~line 4932

**Fields in `read_state_tree` output**:
- `weight` (float) — always present on every state
- `considerations` (array) — present when the state has Consideration nodes; each entry uses the same format as tasks/conditions (`class`, `instance_properties`, `node_properties`), with `Operand` and `DeltaIndent` exposed in `node_properties`

## Source Code Paths (UE 5.5+)

Read these files for authoritative behavior when the summary above isn't enough:

| Concept | Source Path | What It Contains |
|---------|-------------|------------------|
| StateTree asset | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTree.h` | Root structure, BlackboardAsset ref |
| State definition | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeState.h` | UStateTreeState: Tasks[], Transitions[], Children[], EnterConditions[], TasksCompletion, Weight, Considerations |
| Editor data | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorData.h` | UStateTreeEditorData: SubTrees[], Evaluators[], GlobalTasks[], Parameters |
| Editor node | `Engine/Plugins/Runtime/StateTree/Source/StateTreeEditorModule/Public/StateTreeEditorNode.h` | FStateTreeEditorNode: Node (FInstancedStruct), Instance, InstanceObject |
| Consideration base | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeConsiderationBase.h` | FStateTreeConsiderationCommonBase — GetScore() (raw float), GetNormalizedScore(), Operand, DeltaIndent |
| Task execution | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Private/StateTreeExecutionContext.cpp` | `TickTasks()` at ~line 4672 — proves concurrent tick, failure propagation |
| Task completion | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeTasksStatus.h` | `EStateTreeTaskCompletionType` (Any/All), `GetCompletionStatus()` bitwise logic |
| Schema (AI) | `Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Public/Components/StateTreeAIComponentSchema.h` | StateTreeAIComponentSchema: ContextActorClass, AIController |
| Transition logic | `Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Private/StateTreeExecutionContext.cpp` | `TriggerTransitions()` — transition priority & evaluation order |

## read_state_tree Return Format

`weight` is emitted for every state. Optional fields such as `tasks`, `tasks_completion`,
`considerations`, `transitions`, and `enter_conditions` are emitted only when applicable;
do not assume an absent field is present as an empty array.
States of type `LinkedAsset` include `linked_asset`, the `/Game/...` package path of the
referenced StateTree. Read that asset separately when its internal hierarchy is needed.

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
    "weight": 1.0,
    "children": [{
      "name": "Patrol",
      "type": "EStateTreeStateType::LinkedAsset",
      "linked_asset": "/Game/AI/ST_Common_Patrol",
      "weight": 1.0,
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
    },
    {
      "name": "Chase",
      "weight": 2.0,
      "selection_behavior": "EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility",
      "considerations": [
        {"class": "MyConsideration_HPLow", "instance_properties": {"Curve": "...", "Multiplier": "1.5"}, "node_properties": {"Name": "HP Low Bonus", "Operand": "And", "DeltaIndent": "0"}},
        {"class": "StateTreeFloatConsideration", "instance_properties": {"Input": "0.0"}, "node_properties": {"Operand": "Multiply", "DeltaIndent": "0"}}
      ],
      "tasks": [...],
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
