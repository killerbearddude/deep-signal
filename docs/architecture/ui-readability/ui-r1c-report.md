# UI-R1C implementation report: deterministic workspace docking

## Scope and layout invariant

The SDL desktop requests 1920 x 1080 and remains resizable. The existing shell
partition reserves up to 420 px on the right at that width. The left dockspace
retains its previous `DockSpace` identity; a new right dockspace owns Overview,
temporary Preview, and pinned information tabs. Operational panels, View utility
windows, and Configure Processing default to the left dockspace. ImGui settings,
not `GameState` or SQLite, own later docking and floating geometry.

The dock builder populates a region only when that region has no usable node or
when View > Reset Workspace Layout is selected. Ordinary workspace selection
changes visibility and focuses its preferred tab. A new or retargeted temporary
preview focuses its existing information tab. Selection, preview retargeting,
time advancement, New Game, and Load do not rebuild the dock tree. Reset redocks
the current processing editor window by its stable ID while retaining its draft.
Information preview labels display the target name, while their hidden IDs remain
world and Preview ID based. User supplied `#` characters are neutralized in the
visible label so they cannot change ImGui identity.

The initial dock builder registers all operational panel names, including panels
hidden by the current workspace. This lets a later workspace expose its tabs in
the same region without recreating the layout. First-use dock hints for newly
created preview and editor windows yield to existing ImGui settings. The reset
command is presentation-only and does not invoke simulation or save operations.

## Verification performed

- The UI executable and all UI-enabled targets built in `build-ui`.
- `ctest --test-dir build-ui --output-on-failure`: 69/69 passed. New focused
  submission checks cover preview and pin docking, stable retargeting identity,
  processing editor docking, and preservation of a dirty editor record through
  a dock-node rebuild.
- `ctest --test-dir build-r1b-headless --output-on-failure`: 57/57 passed.
- An isolated `SDL_VIDEODRIVER=dummy` run at 1920 x 1080 wrote ImGui settings
  with Strategic Map and Bodies / System in the 1500 px operational dock and
  Overview in the 420 px information dock. The selected operational tab ID
  resolves to Strategic Map. A second run preserved a deliberately
  undocked Bodies window at its saved 75,80 position and 640 x 500 size.

## Review limit

A visual native review was not possible in the execution environment. Xvfb
launched without a usable SDL video device. Dummy SDL startup and ImGui settings
verify submission and saved geometry, not pixels, tab readability, pointer
interaction, or Shipyard usability. The requested native 1920 x 1080 review of
workspace tabs, multiple pins, editor, manual undocking, and Reset remains open.

UI-R1A, UI-R1A.1, and UI-R1B reports remain historical records of their own
baselines.
