# Parts Editor → real vector editor (port of svgedit's model into the Qt PE)

> Design reference. Newest decisions at top. This is the spec for turning the live parts
> editor from a **metadata editor with a static SVG preview + per-element terminal-point
> handle** into a **real in-app vector editor** (freehand + path-node editing + primitives +
> text + layers + undo), eliminating the dependency on external Inkscape / the parametric
> `fritzing-schematic` plugin for authoring part graphics.

---

## 0. What exists today (ground truth)

- The **live** parts editor is the `pe*` family: `src/partseditor/pemainwindow.cpp` (+
  `pemetadataview`, `peconnectorsview`, `pesvgview`, `petoolview`, `pegraphicsitem`,
  `pecommands`). The *old* editor (`partseditormainwindow`, `partseditorview`, old `svgview`)
  is parked in `src/partseditor/obsolete/` and **commented out** in `pri/partseditor.pri`.
- **It has never been a vector editor.** `initSvgTree()` (`pemainwindow.cpp:1393`) parses the
  part's SVG into a `QDomDocument`, normalizes it via `FSvgRenderer::loadSvg(...)`
  (`FSvgRenderer : public QSvgRenderer`, Qt *static* SVG; returns cleaned SVG bytes), then does a
  depth-first walk (`pemainwindow.cpp:1444`) and creates **one `PEGraphicsItem` per element**.
- `PEGraphicsItem` (`pegraphicsitem.h`) is a **`QGraphicsRectItem` overlay** — a hit-target rect
  over an already-baked SVG node. Its only affordances are hover/press/move and a single
  **terminal-point** handle. There is **no primitive creation and no node/path editing**: grep of
  the live `pemainwindow.cpp` for `addRect`/`addPath`/`addText`/`insertElement`/`createNew` → none.
- Rendering into the canvas is `QGraphicsSvgItem` + `QSvgRenderer` (`sketchwidget.cpp:3230,9882`).
- Round-trip: on save the DOM is serialized back with `removeGorn(...)`/`writeXml(...)`
  (`pemainwindow.cpp:907,1716,2177`); the `QDomDocument` is the source of truth.

Consequence: today, to author *any* new part graphic you must leave the app (real Inkscape) or
bake a symbol with the parametric `fritzing-schematic` plugin. That is the gap this work closes.

## 1. The bridge (invariants we keep)

1. **`QDomDocument` stays the source of truth.** The new model is a *typed view* over the existing
   DOM (`m_breadboardDocument`/`m_schematicDocument`/`m_pcbDocument`/`m_iconDocument`), not a
   parallel DOM. Edits mutate the same DOM nodes the rest of the app already consumes.
2. **Connector semantics are a layer on top of generic primitives.** A "connector" remains an SVG
   node whose `id` is `connectorXpin` (+ optional `connectorXterminal`). The current
   terminal-point handle (`pegraphicsitem.cpp`, wired at `pemainwindow.cpp:1499`) becomes *one
   special case* of node editing rather than the whole editing model.
3. **Every edit is a command.** Extend `pecommands.*` (`QUndoCommand`) — mirrors svgedit
   `history.js` `ChangeElementCommand`. No silent DOM mutation outside a command.
4. **Logic is golden-tested**, not smoke-tested (AGENTS.md §4): `svg → model → svg` byte-stable,
   plus geometry goldens. Verified by a fresh clean aggregate `-j` build (Session 12 lesson).


## 2. Source of the model we are porting (svgedit `core/`)

svgedit is the reference; its core is ~19k LOC JS. The faithful mapping (not ported line-by-line,
re-implemented in Qt with the same *behavior*):

| svgedit (`packages/svgcanvas/core`) | Qt PE target | note |
|---|---|---|
| `path-method.js` `TYPE_TO_CMD` = M,L,C,Q,A,S,T,Z (+rel) | `PEPathItem` segment list (QPainterPath / segment structs) | node = anchor + bezier handles |
| `path.js` grips, `math.js` `transformPoint`/`getMatrix` | `PENodeItem` grips + scene<->item transform | replaces rect-overlay hit-test |
| `selected-elem.js` (in-place attr edit) | `PEItem::setAttribute` + command | the live edit path |
| `draw.js` (modes: rect/ellipse/line/path/freehand/text) | `PEToolView` mode router → create element | new capability |
| `layer.js` | `PEDocument` layer groups | `<g>` per layer |
| `history.js` (command-based undo) | `pecommands` | already command-shaped |
| `units.js` | `PEUtils::convertUnitsStr` (already used in `pesvgview.cpp`) | unit-aware inputs |
| `sanitize.js` | port | keeps hand-edited SVG Fritzing-valid |
| `ext-connector` (side/marker, `#connector_rules`) | connector semantics layer | id-convention preserved |

## 3. Phased slices (each: feature branch → PR into develop → green)

- **P0 — model + contract (inert).** Introduce `PEDocument`/`PENode`/`PEElement` as a typed view
  over the DOM; golden `svg→model→svg` round-trip on the 4 view SVGs. No UI change; app behavior
  identical. This is the foundation; it must land first and green.
- **P1 — real item model.** `PEPathItem`/`PERectItem`/`PEEllipseItem`/`PELineItem`/`PETextItem`
  as true `QGraphics*Item`s replacing the `QGraphicsRectItem` overlay; reuse existing marquee/hover.
- **P2 — node editing.** Path `d` ⇄ segment list; select a path → show anchor/handle grips;
  drag edits `d` via a command. (This is the slice that, alone, removes the Inkscape dependency.)
- **P3 — primitive creation.** Tool-driven rect/line/ellipse/path/freehand/text creation.
- **P4 — connector semantics.** Port `ext-connector`; `connectorXpin`/`terminal` authored in-app;
  existing terminal-point handle becomes a node-edit special case.
- **P5 — units + history + sanitize hardening.** Unit-aware numeric inputs; command coverage;
  sanitizer guarantees the output stays valid.

## 4. Open design decisions to resolve per slice
- Freehand capture: polyline-of-segments vs. auto-smooth — match svgedit (it stores freehand as
  `L`-segment path until edited).
- Whether `FSvgRenderer`/`QSvgRenderer` static rendering stays the *preview*, with the live-edit
  canvas switching to real `QGraphicsPathItem`s (it must, for node editing).
- Gorn (`generic:origin`/namespace) handling: current save does `removeGorn(...)`; the model must
  round-trip it faithfully.

## 5. Status
- **Now:** P0 spec'd, code not started. Branch `feature/vector-editor` off `develop`.
- Next landable PR: **P0 model + round-trip golden test** (inert), then P1, etc.
