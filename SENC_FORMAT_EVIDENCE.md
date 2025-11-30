# OpenCPN SENC Format Implementation Evidence
## Hard Evidence for NavCore Rust/WGPU Implementation

---

## 1) EDGE DIRECTION ENCODING

### Evidence Files
- **Main struct**: `S57Obj::m_lsindex_array` [`libs/s52plib/src/s52s57.h:426`]
- **Parsing**: `Osenc::ingest200()` [`gui/src/Osenc.cpp:909-915`]
- **Usage**: `s57chart::AssembleLineGeometry()` [`gui/src/s57chart.cpp:1027-1125`]

### How Direction is Encoded

**SIGNED INTEGER ID** - negative values indicate reverse direction:

```cpp
// gui/src/s57chart.cpp:1030-1042
int seg_index = iseg * 3;
int *index_run = &obj->m_lsindex_array[seg_index];

// Get first connected node
unsigned int inode = *index_run++;

// Get the edge - DIRECTION ENCODED IN SIGN
bool edge_dir = true;
int venode = *index_run++;
if (venode < 0) {
    venode = -venode;      // Make absolute
    edge_dir = false;      // Mark as reversed
}

// Get end connected node
unsigned int enode = *index_run++;
```

### Storage Format

Edge references are stored as **triplets of signed integers** [`gui/src/Osenc.cpp:913`]:
```
[start_node_id, edge_id_signed, end_node_id]
```

- **Positive edge_id**: traverse edge points[0] → points[n-1]
- **Negative edge_id**: traverse edge points[n-1] → points[0]

### Edge Reversal Logic

When `edge_dir == false` (negative ID), code accesses edge backwards:

```cpp
// gui/src/s57chart.cpp:1081-1088
if (edge_dir) {
    pair.e1 = pedge->pPoints[0];        // First point
    pair.n1 = pedge->pPoints[1];
} else {
    int last_point_index = (pedge->nCount - 1) * 2;
    pair.e1 = pedge->pPoints[last_point_index];      // Last point
    pair.n1 = pedge->pPoints[last_point_index + 1];
}
```

And sets special segment type [`gui/src/s57chart.cpp:1124-1125`]:
```cpp
pls->ls_type = TYPE_EE;
if (!edge_dir) pls->ls_type = TYPE_EE_REV;
```

### NavCore Rust Implementation

```rust
// Minimal structures for NavCore

struct EdgeReference {
    start_node: u32,
    edge_id: i32,        // SIGNED: negative = reverse
    end_node: u32,
}

impl EdgeReference {
    fn is_reversed(&self) -> bool {
        self.edge_id < 0
    }

    fn edge_index(&self) -> u32 {
        self.edge_id.abs() as u32
    }
}

struct Edge {
    id: u32,
    points: Vec<Point2D>,  // x,y pairs as floats
}

impl Edge {
    fn get_points(&self, reversed: bool) -> impl Iterator<Item = &Point2D> {
        if reversed {
            self.points.iter().rev()
        } else {
            self.points.iter()
        }
    }
}
```

---

## 2) SENC VERSION FIELD

### Evidence Files
- **Constant**: [`libs/s52plib/src/s52s57.h:38`]
- **Record type**: [`gui/include/gui/Osenc.h:63`]
- **Reading**: [`gui/src/Osenc.cpp:297-308`]

### Version Value

```cpp
// libs/s52plib/src/s52s57.h:38
#define CURRENT_SENC_FORMAT_VERSION 201
```

### File Structure

```cpp
// gui/include/gui/Osenc.h:63
#define HEADER_SENC_VERSION 1    // First record type in file
```

### Reading Logic

```cpp
// gui/src/Osenc.cpp:290-308
OSENC_Record_Base record;
fpx.Read(&record, sizeof(OSENC_Record_Base));

// Check first record is version record
if (HEADER_SENC_VERSION != record.record_type) {
    return ERROR_SENCFILE_NOT_FOUND;
}

// Read version value as uint16_t
unsigned char *buf = getBuffer(record.record_length - sizeof(OSENC_Record_Base));
fpx.Read(buf, record.record_length - sizeof(OSENC_Record_Base));
uint16_t *pint = (uint16_t *)buf;
m_senc_file_read_version = *pint;    // Should be 201
```

### Record Structure

```cpp
// gui/include/gui/Osenc.h:92-95
#pragma pack(push, 1)
typedef struct _OSENC_Record_Base {
    uint16_t record_type;      // = 1 for HEADER_SENC_VERSION
    uint32_t record_length;    // Total size including header
} OSENC_Record_Base;
#pragma pack(pop)
```

**File begins with**:
1. `OSENC_Record_Base` (6 bytes)
2. `uint16_t version` (2 bytes) = 201

### NavCore Rust Implementation

```rust
use std::io::Read;

#[repr(C, packed)]
struct SencRecordHeader {
    record_type: u16,
    record_length: u32,
}

const HEADER_SENC_VERSION: u16 = 1;
const CURRENT_SENC_FORMAT_VERSION: u16 = 201;

fn read_senc_header<R: Read>(reader: &mut R) -> Result<u16, Error> {
    // Read first record header (6 bytes)
    let mut header = SencRecordHeader { record_type: 0, record_length: 0 };
    reader.read_exact(unsafe {
        std::slice::from_raw_parts_mut(
            &mut header as *mut _ as *mut u8,
            std::mem::size_of::<SencRecordHeader>()
        )
    })?;

    // Must be version record
    if header.record_type != HEADER_SENC_VERSION {
        return Err(Error::NotSencFile);
    }

    // Read version value (uint16_t)
    let mut version_bytes = [0u8; 2];
    reader.read_exact(&mut version_bytes)?;
    let version = u16::from_le_bytes(version_bytes);

    if version != CURRENT_SENC_FORMAT_VERSION {
        return Err(Error::UnsupportedVersion(version));
    }

    Ok(version)
}
```

---

## 3) PRE-TRIANGULATED AREAS

### Evidence Files
- **Structure**: [`gui/include/gui/Osenc.h:193-203`]
- **Reading**: [`gui/src/Osenc.cpp:846-886`]
- **Building**: [`gui/src/Osenc.cpp:3236-3335`]
- **Rendering**: [`libs/s52plib/src/s52plib.cpp:8015-8063`]

### Key Discovery

**Areas are ALWAYS pre-triangulated in SENC files**. No runtime triangulation during load.

### Area Geometry Record

```cpp
// gui/include/gui/Osenc.h:193-203
typedef struct _OSENC_AreaGeometry_Record_Base {
    uint16_t record_type;        // = 82 (FEATURE_GEOMETRY_RECORD_AREA)
    uint32_t record_length;
    double extent_s_lat;
    double extent_n_lat;
    double extent_w_lon;
    double extent_e_lon;
    uint32_t contour_count;      // Number of contours (outer + holes)
    uint32_t triprim_count;      // Number of triangle primitives
    uint32_t edgeVector_count;   // Edge references for outline
} OSENC_AreaGeometry_Record_Base;
```

### Triangle Primitive Format

Each TriPrim [`gui/src/Osenc.cpp:3285-3332`]:

```cpp
// For each of triprim_count primitives:
uint8_t tri_type;              // 1 byte: PTG_TRIANGLES/STRIP/FAN
uint32_t nvert;                // 4 bytes: vertex count
double bbox[4];                // 32 bytes: minx, maxx, miny, maxy
float vertices[nvert * 2];     // nvert * 8 bytes: x,y pairs
```

### Reading Pre-Triangulated Data

```cpp
// gui/src/Osenc.cpp:3243-3332
unsigned int n_TriPrim = record->triprim_count;

// Loop through all triangle primitives
for (unsigned int i = 0; i < n_TriPrim; i++) {
    tri_type = *pPayloadRun++;
    nvert = *(uint32_t *)pPayloadRun;
    pPayloadRun += sizeof(uint32_t);

    TriPrim *tp = new TriPrim;
    tp->type = tri_type;              // GL_TRIANGLES/STRIP/FAN equivalent
    tp->nVert = nvert;

    // Read bounding box (4 doubles)
    double *pbb = (double *)pPayloadRun;
    tp->tri_box.Set(pbb[2], pbb[0], pbb[3], pbb[1]);
    pPayloadRun += 4 * sizeof(double);

    // Vertex data pointer (NOT copied, points into buffer)
    tp->p_vertex = (double *)pPayloadRun;
    pPayloadRun += nvert * 2 * sizeof(float);
}
```

### Rendering Uses Pre-Triangulated Data

```cpp
// libs/s52plib/src/s52plib.cpp:8015-8026
if (rzRules->obj->pPolyTessGeo) {
    // Get the vertex data from pre-triangulated structure
    PolyTriGroup *ppg_vbo = rzRules->obj->pPolyTessGeo->Get_PolyTriGroup_head();

    // Convert to VBO and render triangle primitives
    TriPrim *p_tp = ppg_vbo->tri_prim_head;
    while (p_tp) {
        // Upload p_tp->p_vertex to GPU
        // Draw with glDrawArrays(p_tp->type, ...)
        p_tp = p_tp->p_next;
    }
}
```

### When Triangulation Happens

Triangulation occurs **only during SENC creation** from source ENC:

```cpp
// libs/s52plib/src/mygeom.cpp:202-287
PolyTessGeo::PolyTessGeo(OGRPolygon *poly, ...) {
    BuildTessGLU();    // Calls GLU tessellator
}

// This constructor is NEVER called when reading SENC
// Only when creating SENC from .000 ENC file
```

### No Fallback Triangulation

There is **NO** code path like:
```cpp
if (triprim_count > 0) {
    use_triangles();
} else {
    triangulate_edges();    // THIS NEVER HAPPENS
}
```

All SENC area records contain pre-computed triangles.

### NavCore Rust Implementation

```rust
use byteorder::{LittleEndian, ReadBytesExt};

#[derive(Debug, Clone, Copy)]
#[repr(u8)]
enum TriPrimType {
    Triangles = 0x04,      // GL_TRIANGLES
    TriangleStrip = 0x05,  // GL_TRIANGLE_STRIP
    TriangleFan = 0x06,    // GL_TRIANGLE_FAN
}

struct TriPrim {
    prim_type: TriPrimType,
    vertices: Vec<[f32; 2]>,  // x,y pairs
    bbox: BBox,
}

struct AreaGeometry {
    extent: BBox,
    contour_count: u32,
    triangles: Vec<TriPrim>,   // ALWAYS populated from SENC
    edge_refs: Vec<EdgeReference>,  // For outline rendering
}

impl AreaGeometry {
    fn read_from_senc<R: Read>(reader: &mut R) -> Result<Self, Error> {
        let extent = BBox::read(reader)?;
        let contour_count = reader.read_u32::<LittleEndian>()?;
        let triprim_count = reader.read_u32::<LittleEndian>()?;
        let edge_count = reader.read_u32::<LittleEndian>()?;

        // Skip contour point counts (TODO: may need these)
        reader.seek(SeekFrom::Current((contour_count * 4) as i64))?;

        // Read pre-triangulated primitives
        let mut triangles = Vec::with_capacity(triprim_count as usize);
        for _ in 0..triprim_count {
            let prim_type = reader.read_u8()?;
            let nvert = reader.read_u32::<LittleEndian>()?;

            let bbox = BBox {
                min_x: reader.read_f64::<LittleEndian>()?,
                max_x: reader.read_f64::<LittleEndian>()?,
                min_y: reader.read_f64::<LittleEndian>()?,
                max_y: reader.read_f64::<LittleEndian>()?,
            };

            let mut vertices = Vec::with_capacity(nvert as usize);
            for _ in 0..nvert {
                vertices.push([
                    reader.read_f32::<LittleEndian>()?,
                    reader.read_f32::<LittleEndian>()?,
                ]);
            }

            triangles.push(TriPrim {
                prim_type: TriPrimType::from_u8(prim_type)?,
                vertices,
                bbox,
            });
        }

        // Read edge references for outline
        let edge_refs = EdgeReference::read_array(reader, edge_count)?;

        Ok(AreaGeometry {
            extent,
            contour_count,
            triangles,
            edge_refs,
        })
    }
}
```

---

## 4) CONNECTOR SEGMENTS (CE/EC/CC)

### Evidence Files
- **Types**: [`libs/s52plib/src/s52s57.h:527-533`]
- **Line assembly**: [`gui/src/s57chart.cpp:1027-1230`]
- **Line rendering**: [`libs/s52plib/src/s52plib.cpp:4640-4686`]
- **Area fill rendering**: [`libs/s52plib/src/s52plib.cpp:8015-8063`]

### Segment Type Definitions

```cpp
// libs/s52plib/src/s52s57.h:527-533
typedef enum {
    TYPE_CE = 0,      // Connector: Node → Edge start
    TYPE_CC,          // Connector: Node → Node
    TYPE_EC,          // Connector: Edge end → Node
    TYPE_EE,          // Edge: full edge geometry
    TYPE_EE_REV       // Edge: reversed direction
} SegmentType;
```

### Critical Discovery

**CE/EC/CC segments are ONLY for LINE rendering, NOT area fill:**

1. **Area FILL** uses `pPolyTessGeo` (pre-triangulated)
2. **Area OUTLINE** uses `m_ls_list` (CE/EC/CC/EE segments)
3. **Line features** use `m_ls_list` (CE/EC/CC/EE segments)

### Area Has TWO Geometries

```cpp
// gui/src/Osenc.cpp:860-883
case FEATURE_GEOMETRY_RECORD_AREA: {
    // 1. FILL GEOMETRY - pre-triangulated
    PolyTessGeo *pPTG = BuildPolyTessGeo(pPayload, &next_byte);
    obj->SetAreaGeometry(pPTG, m_ref_lat, m_ref_lon);

    // 2. OUTLINE GEOMETRY - edge references with CE/EC/CC
    LineGeometryDescriptor Descriptor;
    Descriptor.indexCount = pPayload->edgeVector_count;
    Descriptor.indexTable = (int *)malloc(edgeVector_count * 3 * sizeof(int));
    memcpy(Descriptor.indexTable, next_byte, ...);
    obj->SetLineGeometry(&Descriptor, GEO_AREA, m_ref_lat, m_ref_lon);
}
```

### Connector Segment Construction

CE segments (Node→Edge) [`gui/src/s57chart.cpp:1061-1115`]:
```cpp
// Connect start node to edge start
if (ipnode && pedge) {
    connector_segment *pcs = new connector_segment;

    if (edge_dir) {
        // Node → first point of edge
        pcs->points[0] = ipnode->pPoint;
        pcs->points[1] = pedge->pPoints[0];
    } else {
        // Node → last point of reversed edge
        pcs->points[0] = ipnode->pPoint;
        pcs->points[1] = pedge->pPoints[last_index];
    }

    line_segment_element *pls = new line_segment_element;
    pls->ls_type = TYPE_CE;
    pls->pcs = pcs;
}
```

EC segments (Edge→Node) [`gui/src/s57chart.cpp:1133-1182`]:
```cpp
// Connect edge end to end node
if (epnode && pedge) {
    connector_segment *pcs = new connector_segment;

    if (!edge_dir) {
        // Reversed edge: first point → node
        pcs->points[0] = pedge->pPoints[0];
        pcs->points[1] = epnode->pPoint;
    } else {
        // Normal edge: last point → node
        pcs->points[0] = pedge->pPoints[last_index];
        pcs->points[1] = epnode->pPoint;
    }

    pls->ls_type = TYPE_EC;
}
```

CC segments (Node→Node) [`gui/src/s57chart.cpp:1195-1227`]:
```cpp
// Direct node-to-node connection (no edge between)
pcs->points[0] = ipnode->pPoint;
pcs->points[1] = epnode->pPoint;
pls->ls_type = TYPE_CC;
```

### Line Rendering Uses All Segment Types

```cpp
// libs/s52plib/src/s52plib.cpp:4677-4686
line_segment_element *lsa = rzRules->obj->m_ls_list;
int max_points = 0;

while (lsa) {
    if ((lsa->ls_type == TYPE_EE) || (lsa->ls_type == TYPE_EE_REV))
        max_points += lsa->pedge->nCount;    // Edge: many points
    else
        max_points += 2;                     // CE/EC/CC: 2 points

    lsa = lsa->next;
}
```

### Area Fill Rendering IGNORES Segments

```cpp
// libs/s52plib/src/s52plib.cpp:8015-8026
int s52plib::RenderToGLAC_GLSL(ObjRazRules *rzRules, Rules *rules) {
    // Area fill ONLY uses pPolyTessGeo
    if (rzRules->obj->pPolyTessGeo) {
        PolyTriGroup *ppg_vbo = rzRules->obj->pPolyTessGeo->Get_PolyTriGroup_head();

        // Render triangle primitives
        // m_ls_list is NEVER accessed here
    }
}
```

### NavCore Implementation Recommendation

```rust
// For AREAS: Only need edge references for OUTLINE rendering
// CE/EC/CC can be ignored if not rendering outlines

struct AreaFeature {
    fill_geometry: Vec<TriPrim>,     // For area fill (REQUIRED)
    outline_edges: Vec<EdgeReference>, // For contour (OPTIONAL)
}

// For LINES: MUST support full segment chain

enum LineSegment {
    Edge {
        edge_id: u32,
        reversed: bool,
    },
    ConnectorCE {
        node_id: u32,
        edge_id: u32,
    },
    ConnectorEC {
        edge_id: u32,
        node_id: u32,
    },
    ConnectorCC {
        start_node: u32,
        end_node: u32,
    },
}

struct LineFeature {
    segments: Vec<LineSegment>,
}

impl LineFeature {
    fn assemble_polyline(&self,
                        edges: &HashMap<u32, Edge>,
                        nodes: &HashMap<u32, Point2D>) -> Vec<Point2D> {
        let mut points = Vec::new();

        for segment in &self.segments {
            match segment {
                LineSegment::Edge { edge_id, reversed } => {
                    let edge = &edges[edge_id];
                    points.extend(edge.get_points(*reversed));
                }
                LineSegment::ConnectorCE { node_id, edge_id } => {
                    points.push(nodes[node_id]);
                    points.push(edges[edge_id].points[0]);
                }
                LineSegment::ConnectorEC { edge_id, node_id } => {
                    let edge = &edges[edge_id];
                    points.push(*edge.points.last().unwrap());
                    points.push(nodes[node_id]);
                }
                LineSegment::ConnectorCC { start_node, end_node } => {
                    points.push(nodes[start_node]);
                    points.push(nodes[end_node]);
                }
            }
        }

        points
    }
}
```

---

## 5) FINAL SUMMARY

### Edge Direction Encoding
- **Storage**: Signed `i32` in edge reference triplets `[node, edge_id_signed, node]`
- **File**: `gui/src/Osenc.cpp:909-915`
- **Parsing**: `gui/src/s57chart.cpp:1037-1041`
- **Usage**: `if (venode < 0) { venode = -venode; edge_dir = false; }`
- **Critical**: NavCore MUST check sign bit and reverse point iteration

### SENC Version
- **Value**: `201` (uint16_t)
- **Constant**: `libs/s52plib/src/s52s57.h:38`
- **Location**: Second field in first record (type 1)
- **File**: `gui/src/Osenc.cpp:297-308`
- **Critical**: NavCore MUST validate version == 201

### Pre-Triangulated Areas
- **Always present**: triprim_count > 0 for all area features
- **Struct**: `gui/include/gui/Osenc.h:193-203`
- **Reading**: `gui/src/Osenc.cpp:3236-3335`
- **Format**: `[type:u8][nvert:u32][bbox:4xf64][vertices:nvert*2*f32]`
- **No fallback**: Runtime triangulation NEVER occurs during SENC load
- **Critical**: NavCore must read and use pre-triangulated data directly

### Connector Segments
- **For LINES**: CE/EC/CC/EE/EE_REV all used [`s52plib.cpp:4677-4686`]
- **For AREA FILL**: CE/EC/CC IGNORED, only TriPrims used [`s52plib.cpp:8015`]
- **For AREA OUTLINE**: CE/EC/CC/EE used for contour rendering
- **Types**: `libs/s52plib/src/s52s57.h:527-533`
- **Critical**: NavCore area rendering only needs TriPrims; segments optional for outlines

### Data Flow
```
SENC File
  ↓
[Record Type 82: FEATURE_GEOMETRY_RECORD_AREA]
  ├→ extent (8 doubles)
  ├→ contour_count (u32)
  ├→ triprim_count (u32)  ← ALWAYS > 0
  ├→ edgeVector_count (u32)
  ├→ contour_point_counts[contour_count] (u32 array)
  ├→ TriPrims[triprim_count]:
  │   ├→ type (u8: 4=triangles, 5=strip, 6=fan)
  │   ├→ nvert (u32)
  │   ├→ bbox (4 x f64)
  │   └→ vertices (nvert x 2 x f32)
  └→ edge_references[edgeVector_count]:
      └→ [start_node:u32, edge_id:i32, end_node:u32]
```

### Gotchas for NavCore

1. **Edge direction**: Negative edge ID is NOT an error, it's reverse flag
2. **Packed structs**: All SENC records use `#pragma pack(1)` - no padding
3. **Float vs Double**: Triangles use `f32` vertices, bbox uses `f64`
4. **No triangulation**: Do NOT implement runtime tessellation for areas
5. **Two geometries**: Areas have both fill (TriPrims) and outline (edges)
6. **Segment types**: CE/EC/CC are 2-point; EE is multi-point
7. **Version check**: Must reject version != 201
8. **Record order**: Version record (type 1) must be first

### Mandatory Implementations

For bit-compatible NavCore rendering:

- ✅ Parse signed edge IDs and reverse point order when negative
- ✅ Read SENC version as first record, validate == 201
- ✅ Read pre-triangulated TriPrims directly, render as GL_TRIANGLES/STRIP/FAN
- ✅ Support TYPE_EE/EE_REV for line rendering
- ⚠️ Support CE/EC/CC only if rendering area outlines or complex lines
- ❌ Do NOT implement GLU tessellator or runtime triangulation

### File Locations Quick Reference

| Component | File | Line Range |
|-----------|------|------------|
| Edge direction check | `gui/src/s57chart.cpp` | 1037-1041 |
| Edge triplet format | `gui/src/Osenc.cpp` | 909-915 |
| Version constant | `libs/s52plib/src/s52s57.h` | 38 |
| Version reading | `gui/src/Osenc.cpp` | 297-308 |
| Area record struct | `gui/include/gui/Osenc.h` | 193-203 |
| TriPrim reading | `gui/src/Osenc.cpp` | 3285-3332 |
| Area rendering | `libs/s52plib/src/s52plib.cpp` | 8015-8063 |
| Segment types enum | `libs/s52plib/src/s52s57.h` | 527-533 |
| Line assembly | `gui/src/s57chart.cpp` | 1027-1230 |
| Line rendering | `libs/s52plib/src/s52plib.cpp` | 4677-4686 |
