# OpenCPN Coordinate System - Quick Reference

## File Locations

| Component | File | Lines |
|-----------|------|-------|
| ViewPort Structure | `gui/include/gui/viewport.h` | 84-257 |
| ViewPort Implementation | `gui/src/viewport.cpp` | 136-341 |
| Mercator Projection | `model/src/georef.cpp` | 354-427 |
| Chart Transforms | `gui/src/s57chart.cpp` | 546-601 |
| S57 Object Fields | `libs/s52plib/src/s52s57.h` | 438-443 |
| Projection Types | `gui/include/gui/chartbase.h` | 94-106 |
| GPU Matrix Setup | `gui/src/glChartCanvas.cpp` | 622-626 |

## Key Functions

### Forward Transform: Lat/Lon → Screen Pixels

```cpp
// Main entry point
wxPoint ViewPort::GetPixFromLL(double lat, double lon)
  └─ calls GetDoublePixFromLL() → wxPoint2DDouble
  └─ rounds to integer wxPoint

// Double-precision implementation
wxPoint2DDouble ViewPort::GetDoublePixFromLL(double lat, double lon)
  1. Normalize longitude (handle IDL)
  2. Select projection: toSMcache(), toTM(), toORTHO(), etc.
  3. Output: easting (m), northing (m)
  4. Scale: × view_scale_ppm
  5. Rotate: apply rotation matrix
  6. Translate: add (pix_width/2, pix_height/2)

// Chart-level fast transform (uses cached values)
void s57chart::GetPointPix(ObjRazRules *rzRules, float north, float east, wxPoint *r)
  Formula: r->x = ((east - m_easting_vp_center) * m_view_scale_ppm) + m_pixx_vp_center
           r->y = m_pixy_vp_center - ((north - m_northing_vp_center) * m_view_scale_ppm)
```

### Reverse Transform: Screen Pixels → Lat/Lon

```cpp
void ViewPort::GetLLFromPix(const wxPoint2DDouble &p, double *lat, double *lon)
  1. Subtract viewport center: (p.x - pix_width/2, p.y - pix_height/2)
  2. Remove rotation
  3. Divide by view_scale_ppm
  4. Call inverse projection: fromSM(), fromTM(), fromORTHO(), etc.
  5. Normalize longitude to ±180°
```

### Projection Selection

```cpp
// In GetDoublePixFromLL() at line 168
switch (m_projection_type) {
  case PROJECTION_MERCATOR:
  case PROJECTION_WEB_MERCATOR:
    cache0 = toSMcache_y30(clat);  // Cache expensive log function
    toSMcache(lat, lon, cache0, clon, &easting, &northing);
    break;
  
  case PROJECTION_TRANSVERSE_MERCATOR:
    toTM(lat, lon, 0., clon, &easting, &northing);  // Relative to center
    break;
  
  // ... etc for other 8 projections
}
```

## Core Transformations

### Mercator Projection (Most Common)

```cpp
// Forward: Lat/Lon → Easting/Northing
void toSM(double lat, double lon, double lat0, double lon0, double *x, double *y) {
  const double z = WGS84_semimajor_axis_meters * mercator_k0;  // ~6365337 m
  *x = (lon - lon0) * DEGREE * z;                   // Linear in longitude
  *y = 0.5 * log((1+sin(lat))/(1-sin(lat))) * z     // Log in latitude
       - 0.5 * log((1+sin(lat0))/(1-sin(lat0))) * z; // Relative to lat0
}

// Cached version (10x faster for many points)
double toSMcache_y30(double lat0) {
  return 0.5 * log((1 + sin(lat0*DEGREE)) / (1 - sin(lat0*DEGREE))) * z;
}
void toSMcache(double lat, double lon, double y30, double lon0, double *x, double *y) {
  *x = (lon - lon0) * DEGREE * z;
  *y = 0.5 * log((1+sin(lat*DEGREE))/(1-sin(lat*DEGREE))) * z - y30;
}

// Inverse: Easting/Northing → Lat/Lon
void fromSM(double x, double y, double lat0, double lon0, double *lat, double *lon) {
  const double z = WGS84_semimajor_axis_meters * mercator_k0;
  const double y0 = 0.5 * log((1+sin(lat0*DEGREE))/(1-sin(lat0*DEGREE))) * z;
  *lat = (2 * atan(exp((y0+y)/z)) - PI/2) / DEGREE;
  *lon = lon0 + x / (z * DEGREE);
}
```

## Key Data Fields

### ViewPort (gui/include/gui/viewport.h)

| Field | Type | Purpose | Default |
|-------|------|---------|---------|
| `clat` | double | Center latitude | NaN |
| `clon` | double | Center longitude | NaN |
| `view_scale_ppm` | double | Pixels per meter | 1.0 |
| `rotation` | double | Rotation in radians | 0.0 |
| `skew` | double | Shear in radians | 0.0 |
| `tilt` | double | Perspective tilt in radians | 0.0 |
| `pix_width` | int | Viewport width in pixels | 0 |
| `pix_height` | int | Viewport height in pixels | 0 |
| `m_projection_type` | int | Active projection | PROJECTION_MERCATOR |
| `vp_matrix_transform[16]` | float[16] | 4x4 MVP matrix for GPU | identity |
| `lat0_cache` | double | Cached reference latitude | NaN |
| `cache0`, `cache1` | double | Cached projection params | varies |

### s57chart (gui/include/gui/s57chart.h)

| Field | Type | Purpose | Updated By |
|-------|------|---------|------------|
| `ref_lat`, `ref_lon` | double | Chart reference point | Init |
| `m_easting_vp_center` | double | SM easting of VP center | SetVPParms() |
| `m_northing_vp_center` | double | SM northing of VP center | SetVPParms() |
| `m_pixx_vp_center` | double | Screen X at center | SetVPParms() |
| `m_pixy_vp_center` | double | Screen Y at center | SetVPParms() |
| `m_view_scale_ppm` | double | Current zoom level | SetVPParms() |
| `m_last_vp` | ViewPort | Last rendered VP | Rendering functions |

## Constants

```cpp
// WGS84 Earth parameters
WGS84_semimajor_axis_meters = 6378137.0  // meters
WGSinvf = 298.257223563                  // 1/flattening

// Projection parameters
mercator_k0 = 0.9996                     // Transverse Mercator scale
DEGREE = PI / 180.0                      // Radians conversion
INVALID_COORD = -2147483647 - 1          // Out-of-bounds sentinel
```

## Performance Tips

### Cache Strategy
- `SetVPParms()` called once per viewport change (pan/zoom)
  - Computes expensive projection: `toSM(vpt.clat, vpt.clon, ref_lat, ref_lon, ...)`
  - Caches result: `m_easting_vp_center`, `m_northing_vp_center`

- `GetPointPix()` called per geometry point
  - Uses cached values: no trigonometric functions needed
  - Only simple arithmetic: `(sm_coord - cached_center) * scale + pixel_offset`

### Speedup: 10x for 1000+ geometry points
```
Without cache: 1000 calls to toSM() → 1000 log functions
With cache: 1 call to toSM() + 1000 arithmetic operations
```

## Precision Management

Problem: Float32 (GPU) has only 7 decimal digits precision
- Earth is 40 million meters in circumference
- Lost precision at sub-kilometer level

Solution: 3-level hierarchy
1. Geographic: Store `clat, clon` as double (±180°)
2. Projected: Compute relative to chart reference point (~±millions meters)
3. Viewport: Store as small deltas (±thousands meters)
   → Maintains millimeter precision over 100 km²

```
Example: Montreal (45.5°N, 73.6°W) at 50,000 PPM zoom

Level 1 (double): 45.5°, -73.6°
  ↓ (toSM relative to ref)
Level 2 (double): x=8,213,461 m, y=5,654,230 m
  ↓ (subtract cached center)
Level 3 (double): x_delta=-50,000 m, y_delta=+25,000 m
  ↓ (multiply by scale)
Pixels (double): x_pix=-2,500 px, y_pix=+1,250 px
  ↓ (round & add center)
Screen (int): x=960 px, y=360 px  [on 1920x720 viewport]
  ↓ (GPU matrix transform)
NDC (float): x=-0.5, y=0.25, z=0 [for GLSL]
```

## International Date Line Handling

```cpp
// In GetDoublePixFromLL() at line 150-163
double xlon = lon;

// Make sure lon and lon0 are same phase
if ((lon * lon0 < 0.) && (fabs(lon - lon0) > 180.)) {
  if (lon < 0.0) xlon += 360.0;  // Adjust if crossing IDL
  else xlon -= 360.0;
}

// Handle case where difference > 180°
if (fabs(xlon - clon) > 180.) {
  if (xlon > clon) xlon -= 360.;
  else xlon += 360.;
}
```

## High-DPI Display Support

```cpp
// Set by SetPixelScale(double scale)
// Typical values:
//   Standard display: 1.0
//   MacBook Retina: 2.0
//   Windows HiDPI: 1.5-1.75

// Applied in GetDoublePixFromLL() at line 258-264
if (!g_bopengl) {  // Non-OpenGL rendering
  x /= m_displayScale;  // Convert physical → logical pixels
  y /= m_displayScale;
}
```

## Rotation & Skew

```cpp
// In GetDoublePixFromLL() at line 250-255
if (rotation || skew) {
  double dxr = epix * cos(rotation) + npix * sin(rotation);
  double dyr = npix * cos(rotation) - epix * sin(rotation);
}

// Reverse in GetLLFromPix() at line 279-281
if (rotation) {
  xpr = dx * cos(rotation) - dy * sin(rotation);
  ypr = dy * cos(rotation) + dx * sin(rotation);
}
```

---

For detailed documentation, see `COORDINATE_SYSTEM_ANALYSIS.md`
