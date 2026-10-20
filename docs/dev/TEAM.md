# Team mode (lead + parallel agents)

Owner decision 2026-10-20: work as a dev team. One lead (Opus, the main Claude Code session) plans, hands out work,
verifies, reviews and merges; up to **3 worker agents** run at the same time. Pattern from coop-anything
`playbook/multi-agent-workflows.md` and `playbook/methods.md` (sections 2 and 8).

## Roles
| Role | Model | Does | May |
|---|---|---|---|
| Lead | Opus | plan, assign, verify claims, review, merge into `main`, must-pass runs, docs/dev, talk to the owner | the only one who writes `docs/dev/*.md` and `main`; push/release only after the owner says yes |
| re (reverse engineering) | Sonnet | Ghidra, UnrealScript, playbook patterns; recipes for native calls | read only; own Ghidra project copy; never starts the game |
| Implementers (infra, title, cine, tp, input, ...) | Sonnet | code + test + twin in their own worktree | commit only on their branch `lane/<name>`; game only while holding the slot lease |

**Owner rule 2026-10-20:** worker agents run with effort `medium` at most and preferably not on Opus (Sonnet, also
for RE). Only the lead session runs on Opus (effort high).

## Rules for every agent
1. Read first: PLAN.md, BUGS.md, NOTES.md, TESTS.md, this file, and the ground rules in
   `C:\GitRepo\coop-anything\AGENTS.md`.
2. Work only in your worktree `C:\GitRepo\foc-wt\<lane>` (branch `lane/<lane>`) or your folder
   `C:\GitRepo\coop-anything\local\lanes\<lane>\`. Never edit `C:\GitRepo\foc-coop` (main), never push, never release.
3. Do not edit `docs/dev/*.md`. End your report with a **Docs delta** block: ready-to-paste lines for
   PLAN/BUGS/DONE/NOTES/TESTS. Write findings into your own REPORT.md as you go, not at the end.
4. New DLL code goes into your own file `src/proxy_dinput8/<lane>.inc`. Shared files get one-line registrations only
   (include, Tick call, test command line, ini key, sync key). Hot spots: test command dispatcher (`overlay.inc`),
   ini keys (`dinput8.cpp`), `TickSync` (`sync.inc`), Tick calls (`gametick.inc`).
5. File ownership: infra owns `tools/dev.ps1` and `tools/matrix.ps1`; story lanes add new test scripts.
6. Game runs only while holding the slot lease (see below). Before starting games make sure the desktop is displayed;
   a SKIP / "no display" means stop and report "needs the RDP window shown", no retry loops. Only copies under
   `E:\FoC-coop`, never the Steam install in Program Files, never the owner's saves.
7. Every fix: an ini flag, a test script, a twin (fix off must FAIL), in-game `shot` screenshots of host and guest.
   "Works" only with a RESULT line from a run; assert the mode (map, role, players) before the result.
8. Pitfalls: write C/PS1 with the Write/Edit tools (backslashes), then check for `[\x00-\x08\x0b\x0c]`; no Cyrillic in
   .ps1 (PowerShell 5.1 reads them as ANSI); no desktop screenshots; PowerShell 5.1 syntax.
9. Report format: what was done, commits, commands and RESULT lines, twin result, screenshots, open questions,
   confidence, Docs delta.

## Shared resources
| Resource | Rule |
|---|---|
| Test slot (copies A/B, UDP 7777/7778, foc_cmd.txt, foc_state.json, focus, RDP display) | lease file `E:\FoC-coop\slot.lock` (owner, pid, since, ttl). Wait while it is held; an expired lease may be taken over. Until infra's lease code is merged only the lane the lead names may run games. |
| Ghidra | re uses its copy `coop-anything\local\ghidra_proj_re` with `-readOnly`; new scripts in `local\lanes\re\ghidra_scripts`; the main `ghidra_proj` is the lead's (address checks) |
| Sources | one worktree per lane + its own .inc |
| docs/dev | lead only |
| Steam mock `%TEMP%\foc_mock_steam` | Steam-mode tests only by the slot holder |

## Lead workflow
1. Fan out read-only work first (re), then implement in lanes, tests through the lease one at a time.
2. Verify every claim before acting on it (Ghidra addresses with an own DecompAt run, tests by rerunning).
3. Review each lane diff (`/code-review` on the branch for DLL changes), merge one or two lanes at a time, rebuild,
   run the must-pass set in TESTS.md plus the lane tests and twins, update docs/dev, commit locally.
4. Push and GitHub releases only after the owner's yes.

## How agents are started
- Used: background subagents from the lead session (Agent tool, `model: sonnet`, `effort: medium`), started in one message so
  they run at the same time. Visible in the agent panel below the prompt and in `/tasks`; continued with SendMessage.
- Alternative: agent teams (`"env": {"CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS": "1"}` in settings.json, restart; arrows +
  Enter to talk to a teammate, Ctrl+T task list). Experimental; teammates cannot run background commands; no resume.
- Alternative: one terminal per lane: `git -C C:\GitRepo\foc-coop worktree add C:\GitRepo\foc-wt\<lane> -b lane/<lane>
  main`, then `cd C:\GitRepo\foc-wt\<lane>; claude --model sonnet`. (`claude --worktree` starts from `origin/main`
  unless `"worktree": {"baseRef": "head"}` is set.)

## Current lanes
| Lane | Agent | Worktree / folder | Goal |
|---|---|---|---|
| re | Sonnet | `coop-anything\local\lanes\re\` | B15, B14, B16, B17 recipes; missing script classes; playbook patterns |
| infra | Sonnet | `C:\GitRepo\foc-wt\infra` | slot lease, `-Repo`, copy parameters, `foc_build.txt`, B9, `slottest.ps1` |
| title | Sonnet | `C:\GitRepo\foc-wt\title` | B16 chapter title on both screens (`chaptersync`), B17 observation |
Next wave: cine (B14/B17 fix), tp (B15, B11c revive tests), input (Stage 6, second slot); then sweep (R1).
