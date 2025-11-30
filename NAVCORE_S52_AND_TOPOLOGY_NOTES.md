# NavCore S-52 and Topology Notes

**Target**: Porting OpenCPN's S-57/S-52 chart rendering to Rust + WGPU
**Source**: OpenCPN codebase analysis
**Focus**: Vector edge topology, conditional symbology, symbol atlas, styling, and rendering

---

## 1. Repository Overview

### Key S-57/SENC Modules
- **`gui/src/Osenc.cpp`** [520–2500] – SENC binary parser, reads records 64–100
  - Ingests features, attributes, geometry, edge/node tables
  - Version 201 format with packed structs (`#pragma pack(1)`)
- **`gui/src/s57chart.cpp`** [982–1300, 4094–4500] – Chart loading and area assembly
  - `AssembleLineGeometry()` [982] – builds topology from edge tables
  - `BuildRAZFromSENCFile()` [4094] – creates renderable object arrays (ObjRazRules)
- **`libs/s52plib/src/s52plib.cpp`** [766–950, 1363–1414] – S-52 presentation library
  - `S52_LUPLookup()` [1363] – lookup table matching by feature + attributes
  - `FindBestLUP()` [766] – attribute scoring algorithm with wildcards
  - `StringToRules()` [958] – parses INST strings (SY/LS/LC/AC/AP/TX/TE/CS)
- **`libs/s52plib/src/s52cnsy.cpp`** [80–6100] – Conditional symbology procedures
  - 40+ CS functions (DEPARE02, DEPCNT02, LIGHTS05, RESARE02, etc.)
- **`libs/s52plib/src/chartsymbols.cpp`** [200–650] – Symbol/pattern loader
  - Parses `data/s57data/chartsymbols.xml` via pugixml
  - Builds texture atlas from `rastersymbols-*.png` sheets
- **`libs/s52plib/src/mygeom.cpp`** [200–700] – Polygon tessellation
  - `PolyTessGeo` class using GLU tessellator
  - Outputs triangle strips/fans for GPU

### Entry Points
- **Chart open**: `ChartDB::OpenChartFromDB()` [gui/src/chartdb.cpp:974] → `s57chart::Init()` [gui/src/s57chart.cpp:2506]
- **Render**: `s57chart::RenderRegionViewOnGL()` [1583] → `DoRenderOnGL()` [1705] → `s52plib::RenderObjectToGL()` [libs/s52plib/src/s52plib.cpp:6400]

---

## 2. Vector Edge Topology (Area Assembly)

### Overview
S-57 stores polygons as **edge tables** (SENC records 96/97) to avoid duplicate coastline data. OpenCPN assembles closed rings from these fragments.

### Key Structures
- **`VE_Element`** [libs/s52plib/src/s52s57.h:508–516] – Vector Edge: index + point array + bbox
- **`VC_Element`** [518–522] – Vector Connected node: index + single point
- **`line_segment_element`** [543–553] – Linked list node: edge or connector segment
- **`connector_segment`** [535–541] – 2-point join between node and edge

### Algorithm: `AssembleLineGeometry()` [gui/src/s57chart.cpp:982–1300]

**Inputs**:
- `m_ve_hash`: map<edge_id → VE_Element*> (edge geometries)
- `m_vc_hash`: map<node_id → VC_Element*> (node positions)
- `m_lsindex_array`: per-feature triplets [start_node, edge_id, end_node]

**Outputs**:
- `m_ls_list`: linked list of line_segment_element per feature
- `m_line_vertex_buffer`: single VBO for all chart line geometry

**Pseudocode**:
```
for each feature with line geometry:
    for each [start_node, edge_id, end_node] triplet:
        if edge_id < 0: edge_dir = reverse

        # Create connector from start_node to edge start
        if start_node exists:
            key = (start_node << 32) | edge_id
            if key not in ce_connector_hash:
                pcs = new connector_segment
                pcs.points = [node.pPoint, edge.pPoints[0 or end]]
                ce_connector_hash[key] = pcs
            append TYPE_CE segment to feature list

        # Add edge geometry
        append TYPE_EE (or TYPE_EE_REV) segment to feature list

        # Create connector from edge end to end_node
        (similar for ec_connector_hash, TYPE_EC)

        # Create connector between adjacent edges at same node
        (similar for cc_connector_hash, TYPE_CC)

# Flatten all segments into single VBO
for priority in 0..9:
    for each feature at priority:
        walk feature's segment list
        copy geometry to VBO at vbo_offset
        store offset in connector_segment or VE_Element
```

**Key Takeaways for NavCore**:
- Edge direction encoded by sign: positive = forward, negative = reverse
- 5 segment types: CE (node→edge), EE (edge), EC (edge→node), CC (node connector), EE_REV (reversed edge)
- No explicit winding order; holes not handled here (area features use PolyTessGeo instead)
- Connector segments are 2 points, edges are N points
- All geometry flattened into single VBO per chart, indexed by offset
- Must preserve topology to enable priority-based rendering (deep contours first, then shallow)

---

## 3. S-52 Conditional Symbology (CS Procedures)

### Implementation
CS procedures are C function pointers in `condTable[]` [libs/s52plib/src/s52cnsy.cpp:6050–6095], called via `RenderCS()` [libs/s52plib/src/s52plib.cpp:6365].

### Major CS Procedures

| CS Name | Features | Inputs | Effect | Location |
|---------|----------|--------|--------|----------|
| **DEPARE02** | DEPARE | DRVAL1, DRVAL2, safety/shallow/deep contour | Sets area fill (DEPIT/DEPVS/DEPMS/DEPMD/DEPDW) based on depth range vs safety contour | [s52cnsy.cpp:607–670] |
| **DEPCNT02** | DEPCNT | VALDCO, safety contour, chart's VALDCO array | Highlights safety contour; emphasizes contours crossing safety threshold | [s52cnsy.cpp:704–810] |
| **LIGHTS05** | LIGHTS | VALNMR, LITCHR, COLOUR, HEIGHT, mariner settings | Generates light sector arcs, flare symbols; filters by range | [s52cnsy.cpp:1200–1500] |
| **RESARE02** | RESARE | CATREA, RESTRN | Adds pattern + restriction symbols based on category | [s52cnsy.cpp:1950–2050] |
| **OBSTRN04** | OBSTRN | VALSOU, WATLEV, safety contour | Danger symbol if depth < safety; varies by water level | [s52cnsy.cpp:520–600] |
| **WRECKS02** | WRECKS | VALSOU, CATWRK, safety contour | Danger if depth < safety; different symbols for wreck category | [s52cnsy.cpp:3238–3410] |

### DEPARE02 Pseudocode [607–670]
```
safety = mariner_param(MAR_SAFETY_CONTOUR)
shallow = mariner_param(MAR_SHALLOW_CONTOUR)
deep = mariner_param(MAR_DEEP_CONTOUR)
drval1 = feature.DRVAL1  # min depth
drval2 = feature.DRVAL2  # max depth

if drval1 >= safety && drval2 > safety:
    return ""  # deeper than safe: no fill
if drval1 >= shallow && drval2 > shallow:
    return "AC(DEPMS);AP(DIAMOND1);LS(SOLD,1,DEPSC)"
if drval1 >= safety && drval2 > safety:
    return "AC(DEPMD);LS(SOLD,1,DEPSC)"
if drval1 >= deep && drval2 > deep:
    return "AC(DEPDW);LS(SOLD,1,DEPCN)"
else:
    return "AC(DEPIT);LS(SOLD,1,DEPCN)"  # shallow/danger
```

### DEPCNT02 Pseudocode [704–810]
```
valdco = feature.VALDCO  # contour depth value
safety = mariner_param(MAR_SAFETY_CONTOUR)
safe = false

# Check if this contour matches safety contour
drval1 = feature.DRVAL1
drval2 = feature.DRVAL2
if drval1 <= safety && drval2 >= safety:
    safe = true  # contour crosses safety depth

# Find nearest contour from chart's VALDCO array
if valdco == safety:
    safe = true
else if valdco > safety:
    # Search for next deeper contour
    for each chart.valdco:
        if chart.valdco >= safety && chart.valdco < next_safe_contour:
            next_safe_contour = chart.valdco
    if valdco == next_safe_contour:
        safe = true

if safe:
    return "LS(SOLD,2,DEPSC)"  # safety contour: thick line
else:
    return "LS(SOLD,1,DEPCN)"  # normal contour
```

### Porting Notes for NavCore
- **CPU-side**: All CS logic must run in Rust before GPU submission
  - Inputs: feature attributes (from SENC) + mariner settings (user prefs)
  - Outputs: color name, symbol ID, line style, z-order/priority
- **GPU-side**: CS results become per-feature uniforms or vertex attributes
  - Example: DEPARE02 outputs "DEPIT" color → lookup RGB(131,178,149) → send to fragment shader
- **Caching**: CS results depend only on feature + mariner params → can cache per feature ID
- **Not implemented**: Some CS procedures are stubs (return error or simple fallback)
- **Edge case**: Safety contour selection is "nearest deeper if exact not found" – must replicate logic exactly

---

## 4. Symbol Atlas Layout (Point Symbols & Patterns)

### Source Files
- **`data/s57data/chartsymbols.xml`** – Symbol definitions (name, size, pivot, HPGL or bitmap)
- **`data/s57data/rastersymbols-day.png`** (+ dusk.png, night.png) – Pre-rendered sprite sheets
- **`libs/s52plib/src/chartsymbols.cpp`** [200–650] – Loader and atlas builder

### Atlas Structure
- Single texture per color scheme (Day/Dusk/Night): typically 2048×2048 or 1024×1024
- Symbols packed at fixed positions defined in XML
- UV coords stored in `m_symbolGraphicLocations` hash map [chartsymbols.h:181]

### Symbol Metadata (from XML)
```xml
<symbol name="ACHARE51">
  <bitmap width="20" height="20">
    <graphics-location x="123" y="456"/>  <!-- atlas position -->
    <origin x="10" y="10"/>               <!-- unused in OpenCPN -->
    <pivot x="10" y="10"/>                <!-- hotspot/anchor -->
  </bitmap>
</symbol>
```

Parsed into `SymbolSizeInfo_t` [chartsymbols.h:87–94]:
```c++
struct SymbolSizeInfo_t {
  wxSize size;        // width, height in pixels
  wxPoint origin;     // (unused)
  wxPoint pivot;      // anchor offset from top-left
  wxPoint graphics;   // (x, y) in atlas texture
  int minDistance;    // min spacing for repeated symbols
  int maxDistance;    // max spacing
};
```

### Example Symbols

| Symbol ID | Atlas Rect | Pivot | Code Ref |
|-----------|------------|-------|----------|
| ACHARE51 | (123, 456, 20, 20) | (10, 10) | [chartsymbols.cpp:434–436] |
| BOYLAT12 | (340, 100, 16, 32) | (8, 28) | parsed from XML |
| LIGHTS11 | (200, 50, 24, 24) | (12, 12) | parsed from XML |

**Pivot semantics**: Offset from symbol's top-left corner to placement point. When rendering at screen position (x, y), draw symbol at (x - pivot.x, y - pivot.y).

### Retrieval API
- `ChartSymbols::GetImage(symbolName)` [chartsymbols.cpp:870] – extracts sub-rect from atlas
- `ChartSymbols::GetGLTextureRect(symbolName)` [chartsymbols.cpp:880] – returns UV rect for OpenGL
- `HashKey(symbolName)` [chartsymbols.cpp:865] – lookup by name string

### Porting Notes for NavCore
- **Atlas format**: PNG sprite sheet, not packed by algorithm – positions are fixed from XML
- **Separate atlases per color scheme**: must load 3 textures (Day/Dusk/Night) or use color modulation
- **Pivot is critical**: symbol placement breaks if pivot ignored (e.g., lights appear offset)
- **HPGL fallback**: Some symbols have vector definitions (HPGL strings) if raster missing – NavCore may skip or pre-render
- **Rotation**: No per-instance rotation in atlas; symbols are pre-rotated or rendered via HPGL
- **Rust struct**:
  ```rust
  struct SymbolDef {
      name: String,
      atlas_rect: Rect<u16>,  // (x, y, w, h) in texture
      pivot: (i16, i16),
      min_distance: u16,
  }
  ```

---

## 5. Line Pattern Definitions (Dash Styles)

### Source
Line styles defined in `chartsymbols.xml` [346–660] as part of `<instruction>` strings:
```xml
<instruction>LS(SOLD,1,CHBLK)</instruction>    <!-- solid, width 1, black -->
<instruction>LS(DASH,2,CHMGF)</instruction>    <!-- dashed, width 2, magenta fill -->
<instruction>LS(DOTT,1,DEPSC)</instruction>    <!-- dotted, width 1, safety contour color -->
```

### Line Style Enum
Defined in `s52plib.cpp` [4640–4890] via `RenderLC()`:
- **SOLD** – Solid (continuous)
- **DASH** – Dashed (pattern defined by pen style)
- **DOTT** – Dotted (small gaps)
- **DASHDOT** – Dash-dot alternating
- **DASHDOTDOT** – Dash-dot-dot

### Implementation
OpenCPN uses **Qt/wxWidgets pen styles** [gui/src/s57chart.cpp:DCRenderLPB]:
- Converted to `wxPenStyle` enum: `wxPENSTYLE_SOLID`, `wxPENSTYLE_DOT`, `wxPENSTYLE_SHORT_DASH`
- On **OpenGL**: No native dash support
  - **CPU tessellation** [s52plib.cpp:4640]: Lines split into on/off segments
  - **Shader approach** (newer): Fragment shader discards based on `mod(distance, pattern_length)`

### Dash Pattern Encoding
Hard-coded in `RenderGLLC()` [s52plib.cpp:4750]:
```c++
if (style == "DASH") {
    float dash[] = {6.0, 2.0};  // 6 px on, 2 px off
    // tessellate or send to shader
}
```

### Porting Notes for NavCore
- **WGPU has no built-in stipple**: Must implement in fragment shader or geometry preprocessing
- **Shader approach** (recommended):
  ```wgsl
  fn fragment(in: VertexOutput) -> @location(0) vec4<f32> {
      let dist = in.line_distance;
      let pattern = 8.0;  // pixels per cycle
      if (fract(dist / pattern) > 0.75) { discard; }  // 75% on, 25% off
      return in.color;
  }
  ```
- **Geometry approach**: Pre-generate dashed segments on CPU, upload as separate line strips
- **Pattern variations**: DASH (75% on), DOTT (50% on), DASHDOT (custom sequence)
- **Width**: Independent of pattern; handled by line primitive width or geometry shader extrusion

---

## 6. Color Palette Tables (Day/Dusk/Night)

### Source
- **`data/s57data/chartsymbols.xml`** [1–100] – Color definitions per scheme
- **Raster sync**: Must match `rastersymbols-{day,dusk,night}.png` palettes

### Color Table Structure (from XML)
```xml
<color-table name="DAY_BRIGHT">
  <color name="NODTA" r="163" g="180" b="183"/>
  <color name="DEPSC" r="82" g="90" b="92"/>    <!-- safety contour -->
  <color name="DEPIT" r="131" g="178" b="149"/> <!-- shallow water -->
  <color name="DEPVS" r="115" g="182" b="239"/> <!-- very shallow -->
  <color name="CHBLK" r="7" g="7" b="7"/>       <!-- black -->
  ...
</color-table>
```

Loaded into `m_colorTables` [chartsymbols.cpp:728–812], indexed by scheme (0=Day, 1=Dusk, 2=Night).

### Example Color Mappings

| Token | Day RGB | Dusk RGB | Night RGB | Usage |
|-------|---------|----------|-----------|-------|
| DEPSC | (82, 90, 92) | (51, 58, 61) | (41, 48, 51) | Safety contour line |
| DEPIT | (131, 178, 149) | (90, 130, 110) | (20, 20, 20) | Shallow water fill |
| DEPVS | (115, 182, 239) | (80, 140, 180) | (15, 15, 80) | Very shallow fill |
| CHBLK | (7, 7, 7) | (0, 0, 0) | (0, 0, 0) | Chart black |
| LANDA | (201, 185, 122) | (150, 130, 80) | (60, 50, 30) | Land area |

### Palette Selection
- **`s52plib::SetPLIBColorScheme()`** [s52plib.cpp:1378–1414] – switches `m_colortable_index`
- **`s52plib::getColor(token)`** [1420] – returns `S52color*` (RGB struct)
- **`ChartSymbols::SetColorTableIndex()`** [chartsymbols.cpp:900] – reloads raster atlas

### Color Storage
```c++
typedef struct {
    unsigned char R, G, B;
} S52color;
```

### Porting Notes for NavCore
- **Data structure**:
  ```rust
  struct S52ColorTable {
      colors: HashMap<&'static str, [Rgb8; 3]>,  // token → [Day, Dusk, Night]
  }
  ```
- **Lookup**: By token string (e.g., "DEPSC") + scheme index (0/1/2)
- **Gamma**: No adjustments in OpenCPN; RGB values as-is
- **Alpha**: Most colors opaque; some (NODTA) have implicit transparency in patterns
- **Synchronization**: Raster atlas uses same RGB values; reloading atlas on scheme change ensures consistency
- **Fallback**: Unknown tokens default to `CHBLK` (black)

---

## 7. Text Placement Rules (Label Decluttering)

### Implementation
- **`s52plib::CheckTextRectList()`** [s52plib.cpp:2310–2320] – collision detection
- **`m_textObjList`** [s52plib.h:563] – global list of placed text bounding boxes
- **`m_bDeClutterText`** [s52plib.h:349] – user toggle for decluttering

### Algorithm (Pseudocode)
```
# Per frame:
m_textObjList.clear()

for priority in [PRIO_HAZARDS, PRIO_MARINERS, ...]:  # high to low
    for each feature at priority:
        if feature has text rule (TX or TE):
            rect = compute_text_bbox(feature.position, text_string, font_size)

            if m_bDeClutterText:
                if CheckTextRectList(rect, text_obj):
                    skip  # collision detected

            # No collision or declutter disabled
            render_text(text_string, rect)
            m_textObjList.append(text_obj)

def CheckTextRectList(test_rect, ptext):
    for existing in m_textObjList:
        if test_rect.Intersects(existing.rText):
            # Additional check: priority wins
            if ptext.rul_seq_creator < existing.rul_seq_creator:
                return false  # higher priority, allow overlap
            return true  # collision
    return false  # no collision
```

### Text Anchor Rules [s52plib.cpp:2370–2514]
- **Point features**: Anchor at feature position + offset (hjust, vjust from TE rule)
- **Line features**: Place at midpoint or repeated every N pixels
- **Area features**: Place at centroid (computed via polygon bbox)

### Collision Detection
- **Simple rect intersection**: No spatial index (acceptable for <1000 labels/frame)
- **Priority-based**: Higher priority (lower number) wins overlaps
- **Padding**: None explicit; rects are tight bounding boxes

### Porting Notes for NavCore
- **Spatial index**: For >10k labels, use quadtree or grid; OpenCPN's linear search is O(N²)
- **Priority ordering**: Must render high-priority features first (PRIO_HAZARDS = 8, PRIO_MARINERS = 9)
- **Font metrics**: Rust needs platform-independent text measurement (e.g., `rusttype` or GPU text atlas)
- **Multi-line text**: Some TE rules have `\n`; must split and stack
- **Curved text**: Not implemented in OpenCPN; NavCore could add for line features
- **Zoom-dependent**: No automatic hiding at low zoom; rely on SCAMIN attribute per feature
- **Data structure**:
  ```rust
  struct PlacedText {
      rect: Rect<f32>,
      priority: u8,
      text: String,
  }
  ```

---

## 8. Depth Contour & Safety Contour Generation

### Mariner Parameters [s52plib.cpp:S52_getMarinerParam()]
- **MAR_SAFETY_CONTOUR** – Primary threshold (default 10m)
- **MAR_SHALLOW_CONTOUR** – Secondary (default 5m)
- **MAR_DEEP_CONTOUR** – Tertiary (default 30m)
- **MAR_TWO_SHADES** / **MAR_FOUR_SHADES** – Color mode toggle

### Safety Contour Selection [s52cnsy.cpp:DEPCNT02, 704–810]
```
safety = mariner_param(MAR_SAFETY_CONTOUR)  # e.g., 10.0m
chart.valdco_array = [2.0, 5.0, 10.0, 20.0, 50.0]  # available contours

# Find exact match or next deeper
next_safe = INFINITY
for valdco in chart.valdco_array:
    if valdco == safety:
        next_safe = valdco; break
    else if valdco > safety && valdco < next_safe:
        next_safe = valdco

# Apply to contour feature
if feature.VALDCO == next_safe:
    style = "LS(SOLD,2,DEPSC)"  # thick safety contour line
else:
    style = "LS(SOLD,1,DEPCN)"  # normal contour
```

### Area Depth Styling [s52cnsy.cpp:DEPARE02, 607–670]
```
drval1 = feature.DRVAL1  # min depth (e.g., 0m)
drval2 = feature.DRVAL2  # max depth (e.g., 5m)

if drval2 <= safety:
    return "AC(DEPIT)"  # shallow/danger: green
else if drval1 < safety && drval2 > safety:
    return "AC(DEPMS)"  # medium shallow: light blue
else if drval1 >= safety && drval2 <= deep:
    return "AC(DEPMD)"  # medium deep: blue
else:
    return "AC(DEPDW)"  # deep water: dark blue
```

### Sounding Filtering [s52cnsy.cpp:SOUNDG02, 5589–5660]
- **Sparse mode**: Show soundings only if depth < safety + margin
- **Dense mode**: Show all soundings with color by depth category
- **Logic**:
  ```
  sounding_value = feature.VALSOU
  if sounding_value < safety:
      symbol = "DANGER01"; color = "DNGHL"  # red/magenta
  else:
      symbol = "SOUNDG02"; color = "SNDG2"  # black
  ```

### Porting Notes for NavCore
- **Inputs needed**:
  - Vessel draft (user setting, default 2m)
  - Safety margin (user setting, default 0m → safety = draft + margin)
  - Two/four-shades mode (affects color choices)
- **Outputs per feature**:
  - DEPARE: area_color (DEPIT/DEPVS/DEPMS/DEPMD/DEPDW)
  - DEPCNT: line_style (SOLD,1 or SOLD,2), color (DEPCN or DEPSC)
  - SOUNDG: show/hide, symbol_id, color
- **Caching**: Safety contour rarely changes; cache styled features until mariner param updates
- **Edge case**: If no contour matches safety, use next deeper (never shallower)
- **Per-chart state**: Each chart has unique VALDCO array; must store with chart instance

---

## 9. Porting Notes for NavCore (Rust + WGPU)

### Section 2: Vector Edge Topology
**Must replicate**:
- Signed edge IDs for direction (positive=forward, negative=reverse)
- 5 segment types (CE, EE, EC, CC, EE_REV)
- Single VBO per chart with offset indexing

**Can simplify**:
- Skip connector segments if rendering non-topological (accept minor gaps at junctions)

**CPU vs GPU**:
- Edge assembly: CPU (Rust)
- VBO upload: one-time per chart
- Rendering: GPU indexed draw calls

**Rust structure**:
```rust
enum SegmentType { CE, EE, EC, CC, EEReverse }
struct LineSegment {
    seg_type: SegmentType,
    vbo_offset: u32,
    point_count: u16,
}
```

### Section 3: S-52 Conditional Symbology
**Must replicate exactly**:
- DEPARE02, DEPCNT02 logic (visual parity for safety contours)
- Safety contour "next deeper" fallback rule
- Attribute wildcards in LUP matching (" " = any, "?" = absent)

**Can simplify**:
- Stub unimplemented CS procedures (e.g., DATCVR01, SEABED01)

**CPU vs GPU**:
- All CS evaluation: CPU (Rust, before geometry submission)
- CS outputs: per-feature color/symbol IDs sent to GPU as uniforms or vertex attributes

**Dangers**:
- Off-by-one errors in safety contour comparison (< vs <=)
- Missing VALDCO array for chart → fallback to manual contour

### Section 4: Symbol Atlas
**Must replicate exactly**:
- Pivot offsets (symbols appear wrong otherwise)
- Separate atlases per color scheme (Day/Dusk/Night)

**Can simplify**:
- Pre-render all HPGL symbols to raster (avoid runtime vector parsing)
- Use texture array instead of 3 separate textures

**CPU vs GPU**:
- Atlas loading: CPU (Rust image decode)
- Rendering: GPU instanced quads with UV lookup

**Rust structure**:
```rust
struct SymbolAtlas {
    texture: wgpu::Texture,
    symbols: HashMap<String, SymbolUV>,
}
struct SymbolUV {
    rect: Rect<f32>,  // normalized UV [0..1]
    pivot: (f32, f32),
}
```

### Section 5: Line Patterns
**Must replicate**:
- SOLD, DASH, DOTT visual appearance (critical for distinguishing restricted areas)

**Can simplify**:
- Use shader-based dashing (no CPU tessellation)

**CPU vs GPU**:
- Pattern definition: CPU lookup table
- Rendering: GPU fragment shader with `discard` based on `line_distance`

**WGPU shader snippet**:
```wgsl
if (style == DASH && fract(v_distance / 8.0) > 0.75) {
    discard;
}
```

### Section 6: Color Palettes
**Must replicate exactly**:
- All 50+ color tokens with Day/Dusk/Night variants (users expect familiar colors)

**Can simplify**:
- Ignore unused colors (e.g., RADAR-specific)

**CPU vs GPU**:
- Palette lookup: CPU (token → RGB)
- Color values: sent to GPU as uniforms or vertex colors

### Section 7: Text Decluttering
**Must replicate**:
- Priority-based collision (higher priority wins)

**Can simplify**:
- Use spatial index (quadtree) for >1000 labels
- Omit curved text (not in OpenCPN anyway)

**CPU vs GPU**:
- Collision detection: CPU (Rust)
- Text rendering: GPU (via texture atlas or SDF font)

**TODO**: Unclear if text priority ties are resolved by rule sequence or feature ID

### Section 8: Depth/Safety Contours
**Must replicate exactly**:
- "Next deeper" safety contour selection
- DEPARE02 color ranges (critical for navigation safety)

**Can simplify**:
- None; this is safety-critical

**CPU vs GPU**:
- Contour selection: CPU (once per mariner param change)
- Styling: GPU (per-feature uniforms)

**Inputs needed**:
```rust
struct MarinerParams {
    vessel_draft: f32,
    safety_margin: f32,
    shallow_contour: f32,
    deep_contour: f32,
    two_shades: bool,
}
```

---

## Summary Checklist for NavCore

- [ ] Implement SENC v201 parser (records 64–100)
- [ ] Build edge topology assembler (CE/EE/EC/CC segments)
- [ ] Port DEPARE02, DEPCNT02, OBSTRN04, WRECKS02 CS procedures
- [ ] Load chartsymbols.xml into symbol atlas (preserve pivot offsets)
- [ ] Implement Day/Dusk/Night color palettes (50+ tokens)
- [ ] Build LUP lookup with wildcard attribute matching
- [ ] Render SOLD/DASH/DOTT line styles (shader-based)
- [ ] Implement text decluttering (priority + rect collision)
- [ ] Port safety contour selection ("next deeper" rule)
- [ ] Validate with real S-57 charts (US5WA11M.000, GB5X01NE.000, etc.)

**Critical for visual parity**: Depth colors, safety contour emphasis, symbol pivots, CS procedures.
**Optional for MVP**: Advanced CS (LIGHTS05 sectors), curved text, HPGL vector symbols.
