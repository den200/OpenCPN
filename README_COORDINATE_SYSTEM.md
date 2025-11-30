# OpenCPN Coordinate Transformation System Documentation

This directory contains comprehensive documentation of OpenCPN's multi-level coordinate transformation and projection system.

## Files in This Documentation

### 1. **COORDINATE_SYSTEM_ANALYSIS.md** (27 KB, 691 lines)
   - **Best for**: In-depth technical reference
   - **Contains**:
     - Complete ViewPort structure documentation with all 16 fields explained
     - Detailed coordinate conversion algorithms (GetPixFromLL, GetDoublePixFromLL, GetLLFromPix)
     - Full Mercator projection mathematics with formulas
     - Chart-specific transform system (SetVPParms, GetPointPix)
     - Object-level transforms with S57Obj structure
     - Precision management strategy (3-level hierarchy)
     - All 10 supported projections with implementation details
     - Multi-level coordinate system hierarchy diagram
     - Performance and caching strategies
     - Every function documented with file:line citations

### 2. **COORDINATE_SYSTEM_SUMMARY.txt** (12 KB, 202 lines)
   - **Best for**: Quick overview and visual reference
   - **Contains**:
     - ASCII diagram of 6-level transformation pipeline
     - Key data structures in tree format
     - All 10 projections listed with function names
     - 3-level precision strategy explained
     - Critical functions and call chain
     - Constants and parameters table
     - Caching and optimization strategies

### 3. **COORDINATE_QUICK_REFERENCE.md** (7 KB)
   - **Best for**: Developer quick lookup during coding
   - **Contains**:
     - File locations with line numbers (quick jump table)
     - Key function signatures
     - Mercator projection code snippets
     - Data field tables (ViewPort, s57chart)
     - Constants reference
     - Performance tips and speedup factors
     - Code examples (IDL handling, HiDPI, rotation)

## Quick Navigation

### For Different Audiences:

**GIS/Cartography Engineers:**
- Start with: COORDINATE_SYSTEM_ANALYSIS.md Section 7 (Projection Types)
- Then read: Sections 3 (Mercator), 6 (Precision Management)

**GPU/Rendering Engineers:**
- Start with: COORDINATE_QUICK_REFERENCE.md (GPU Matrix Setup)
- Then read: COORDINATE_SYSTEM_ANALYSIS.md Section 1 (ViewPort)
- Also see: Section 8 (Multi-Level Hierarchy)

**Navigation/Chart Developers:**
- Start with: COORDINATE_SYSTEM_SUMMARY.txt
- Jump to: COORDINATE_SYSTEM_ANALYSIS.md Section 4 (Chart Transforms)
- Reference: COORDINATE_QUICK_REFERENCE.md (Data Fields)

**Performance Optimizers:**
- See: COORDINATE_SYSTEM_SUMMARY.txt (Caching section)
- Details: COORDINATE_SYSTEM_ANALYSIS.md Section 9 (Performance)
- Example: COORDINATE_QUICK_REFERENCE.md (Performance Tips)

### By Component:

**ViewPort (Core Transform Hub):**
- Definition: COORDINATE_QUICK_REFERENCE.md (File Locations table)
- Structure: COORDINATE_SYSTEM_ANALYSIS.md Section 1
- Implementation: COORDINATE_SYSTEM_ANALYSIS.md Section 2
- Usage: COORDINATE_SYSTEM_ANALYSIS.md Section 8

**Mercator Projection:**
- Overview: COORDINATE_SYSTEM_SUMMARY.txt
- Mathematics: COORDINATE_SYSTEM_ANALYSIS.md Section 3
- Code: COORDINATE_QUICK_REFERENCE.md (Core Transformations)

**Chart-Level Transforms:**
- Overview: COORDINATE_SYSTEM_ANALYSIS.md Section 4
- Function: COORDINATE_QUICK_REFERENCE.md (GetPointPix formula)
- Performance: COORDINATE_SYSTEM_ANALYSIS.md Section 9

**Projection Types:**
- List: COORDINATE_SYSTEM_SUMMARY.txt (Supported Projections)
- Details: COORDINATE_SYSTEM_ANALYSIS.md Section 7
- Selection: COORDINATE_QUICK_REFERENCE.md (Projection Selection)

**Precision Strategy:**
- Overview: COORDINATE_SYSTEM_SUMMARY.txt (Precision Strategy)
- Details: COORDINATE_SYSTEM_ANALYSIS.md Section 6
- Example: COORDINATE_QUICK_REFERENCE.md (3-level hierarchy diagram)

## Key Facts at a Glance

### Coordinate Transformation Pipeline
```
Geographic (WGS84 Lat/Lon)
  ↓ [toSM/toTM/toORTHO/etc]
Projected (Easting/Northing in meters)
  ↓ [subtract viewport center cache]
Relative Projection (meters from VP center)
  ↓ [× view_scale_ppm]
Scaled Projection (pixels from center)
  ↓ [+ viewport center]
Screen Pixel (absolute viewport coordinates)
  ↓ [SetVPTransformMatrix]
GPU NDC (normalized device coordinates)
```

### 10 Supported Projections
1. Mercator (most common)
2. Web Mercator
3. Transverse Mercator (UTM variant)
4. Polyconic (USGS)
5. Orthographic
6. Polar Stereographic
7. Stereographic
8. Gnomonic
9. Equirectangular (simplest)
10. Unknown/fallback

### Precision Strategy Levels
1. **Geographic**: Double precision lat/lon (±180°)
2. **Regional**: Mercator relative to chart reference
3. **Viewport**: Small deltas from viewport center
→ Result: Sub-millimeter precision over 100 km²

### Performance Optimization
- SetVPParms() called once per viewport change
- GetPointPix() uses only cached values (10x faster)
- toSMcache avoids expensive logarithms

## Key Source Files

| Purpose | File | Lines |
|---------|------|-------|
| Core viewport | `gui/include/gui/viewport.h` | 84-257 |
| Transforms | `gui/src/viewport.cpp` | 136-341, 984-991 |
| Mercator | `model/src/georef.cpp` | 354-427 |
| Chart | `gui/src/s57chart.cpp` | 546-601 |
| Objects | `libs/s52plib/src/s52s57.h` | 438-443 |
| GPU | `gui/src/glChartCanvas.cpp` | 622-626 |

## Important Constants

```cpp
WGS84_semimajor_axis_meters = 6378137.0  // Earth radius
mercator_k0 = 0.9996                     // Scale factor
DEGREE = PI / 180.0                      // Radian conversion
INVALID_COORD = -2147483647 - 1          // Out-of-bounds sentinel
```

## Quick Code Example

Converting a geographic point to screen pixels:

```cpp
// ViewPort level (handles all 10 projections)
wxPoint screenPix = vp.GetPixFromLL(45.5, -73.6);  // Montreal

// Or chart level (faster, uses cache)
s57chart* chart = /* ... */;
wxPoint screenPix = { 0, 0 };
chart->GetPointPix(nullptr, 5654230.0, 8213461.0, &screenPix);
// Formula: (SM_coord - cached_center) * scale + screen_offset
```

Reverse transformation:

```cpp
double lat, lon;
vp.GetLLFromPix(wxPoint(960, 360), &lat, &lon);
// Returns: lat=45.5°, lon=-73.6°
```

## Document Statistics

- **Total lines of documentation**: 900+
- **Code examples**: 50+
- **Diagrams and tables**: 20+
- **File references**: 40+
- **Function citations**: 100+
- **Projections documented**: 10
- **Transformation levels**: 6

## How to Use These Documents

1. **First time reading**: Start with COORDINATE_SYSTEM_SUMMARY.txt
2. **Need details**: Jump to COORDINATE_SYSTEM_ANALYSIS.md (use table of contents)
3. **Writing code**: Use COORDINATE_QUICK_REFERENCE.md for function signatures
4. **Debugging**: Use file:line citations to jump directly to source code

## Related OpenCPN Components

- **Chart rendering**: Uses projected coordinates
- **Object visibility**: Uses coordinate transforms for bounding boxes
- **User interaction**: Uses reverse transforms for mouse clicks
- **S57 rendering**: Uses chart-level cached transforms
- **OpenGL rendering**: Uses vp_matrix_transform for GPU

## Last Updated

Documentation covers OpenCPN commit:
- c7b94f3: Merge pull request #1 from den200/codex/refactor-glchartcanvas-for-gles-3.1
- 4c43bec: Add OpenGL RAII wrappers

All file:line citations verified as of these commits.

---

For questions or corrections, refer to the source files directly with citations provided.
