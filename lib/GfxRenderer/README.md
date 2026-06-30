# CPR-vCodex Rendering API

This documents every rendering method available in the GfxRenderer class and the
UI theme layer. The project runs on a **480×800 e-ink panel** (ESP32-C3, ~380 KB
RAM) with a **1-bit monochrome framebuffer** and optional **2-bit grayscale
dithering** for anti-aliased text.

---

## Architecture

```
Application / Activity
   └── BaseTheme / LyraTheme  (UI components: headers, lists, popups, etc.)
       └── GfxRenderer        (buffer drawing: primitives, text, bitmaps)
           └── HalDisplay     (hardware abstraction)
               └── EInkDisplay (SPI commands to SSD1677/UC81xx controller)
```

Coordinates are always **logical** (relative to the current orientation). The
renderer translates them to physical panel coordinates internally.

---

## GfxRenderer — Core Rendering Engine

Declared in `lib/GfxRenderer/GfxRenderer.h`, implemented in
`lib/GfxRenderer/GfxRenderer.cpp` (~1716 lines).

### Setup & Configuration

| Method | Description |
|---|---|
| `begin()` | Initialize framebuffer pointer and panel dimensions. Call once after `display.begin()` |
| `insertFont(int fontId, EpdFontFamily font)` | Register a font for later use with `drawText()` |
| `removeFont(int fontId)` | Unregister a font |
| `setOrientation(Orientation o)` | Set logical coordinate orientation (see Orientation table below) |
| `setDarkMode(bool)` | Enable/disable dark mode (inverts pixel state for BW rendering) |
| `setFadingFix(bool)` | Enable ghost-reduction by turning off panel after refresh |
| `setTextDarkness(uint8_t)` | 0=normal, 1=crisp, 2=dark, 3=extra dark. Affects grayscale AA |
| `setRenderMode(RenderMode)` | `BW`, `GRAYSCALE_LSB`, or `GRAYSCALE_MSB` |
| `setFontCacheManager(FontCacheManager*)` | Attach font cache for scan-mode prewarming |
| `requestNextRefresh(RefreshMode)` | Override refresh mode for the next `displayBuffer()` call |
| `requestNextFullRefresh()` | Shortcut to force a full refresh next frame |

### Pixel Operations

| Method | Description |
|---|---|
| `drawPixel(x, y, state=true)` | Single pixel. Respects dark mode inversion |
| `drawPixelDirect(x, y, state=true)` | Inline alias for `drawPixelRaw()` — skips dark mode |
| `drawPixelRaw(x, y, state)` | Direct framebuffer write. Used internally and in performance-critical code |
| `drawPixelDither<Color>(x, y)` | Templated dithered pixel write (see Color enum) |

`state=true` means black (ink), `state=false` means white (clear). On the
framebuffer, black = cleared bit (0), white = set bit (1). `0xFF` byte = all
white.

### Lines

| Method | Description |
|---|---|
| `drawLine(x1, y1, x2, y2, state=true)` | 1-pixel line. Horizontal/vertical paths are optimized; diagonal uses Bresenham |
| `drawLine(x1, y1, x2, y2, lineWidth, state)` | Thick line. Rendered as `lineWidth` parallel 1-pixel lines offset in Y |

### Rectangles

| Method | Description |
|---|---|
| `drawRect(x, y, w, h, state=true)` | 1-pixel hollow rectangle outline |
| `drawRect(x, y, w, h, lineWidth, state)` | Hollow rectangle with thick border drawn **inside** the rect bounds |
| `fillRect(x, y, w, h, state=true)` | Solid filled rectangle |
| `fillRectDither(x, y, w, h, Color)` | Filled rectangle with Bayer 4×4 dithering for grayscale tones |

### Rounded Rectangles

| Method | Description |
|---|---|
| `drawRoundedRect(x, y, w, h, lineWidth, cornerRadius, state)` | Hollow rounded rect (all 4 corners) |
| `drawRoundedRect(x, y, w, h, lineWidth, cornerRadius, roundTL, roundTR, roundBL, roundBR, state)` | Hollow rounded rect with per-corner control |
| `fillRoundedRect(x, y, w, h, cornerRadius, Color)` | Filled rounded rect (all 4 corners, dithered) |
| `fillRoundedRect(x, y, w, h, cornerRadius, roundTL, roundTR, roundBL, roundBR, Color)` | Filled rounded rect with per-corner control |
| `maskRoundedRectOutsideCorners(x, y, w, h, radius, Color)` | Clears pixels outside the rounded corner arcs. Used to clean up a filled rect into a rounded shape |

### Arcs

| Method | Description |
|---|---|
| `drawArc(maxRadius, cx, cy, xDir, yDir, lineWidth, state)` | Quarter-arc for rounded corners. `xDir`/`yDir` are -1 or 1 to select quadrant |

Internally, `fillArc<Color>(maxRadius, cx, cy, xDir, yDir)` is the private
dithered counterpart used by `fillRoundedRect`.

### Images & Icons

| Method | Description |
|---|---|
| `drawImage(bitmap, x, y, w, h)` | Opaque 1-bit bitmap. Coordinates are rotated to physical panel. No dark-mode awareness |
| `drawIcon(bitmap, x, y, w, h)` | Transparent (0 = transparent) 1-bit bitmap. Dark-mode aware (inverts in dark mode) |
| `drawIconBlack(bitmap, x, y, w, h)` | Transparent, always black regardless of dark mode |
| `drawIconInverted(bitmap, x, y, w, h)` | Transparent, always white. Written directly to framebuffer for performance |

Bitmap data format: 1 bit per pixel, MSB first, packed row-major. Row stride =
`(width + 7) / 8` bytes.

### Bitmap (BMP file format)

| Method | Description |
|---|---|
| `drawBitmap(bitmap, x, y, maxW, maxH, cropX=0, cropY=0)` | Render a BMP file to the framebuffer. Supports dithering, uniform scaling to fit `maxW`×`maxH`, and cropping (0.0–1.0 fraction). Grayscale-aware |
| `drawBitmap1Bit(bitmap, x, y, maxW, maxH)` | Optimized path for 1-bit BMPs. Same scaling but simpler dither path |

The `Bitmap` class (`lib/GfxRenderer/Bitmap.h`) handles BMP header parsing and
row-by-row read with Atkinson or Floyd-Steinberg error-diffusion dithering.

### Polygons

| Method | Description |
|---|---|
| `fillPolygon(xPoints, yPoints, numPoints, state=true)` | Filled polygon using scanline algorithm. Allocates a temporary `nodeX` buffer on heap |

### Text Rendering

| Method | Description |
|---|---|
| `drawText(fontId, x, y, text, black=true, style=REGULAR)` | Render UTF-8 text at (x,y). `black=false` draws white text. Style flags: `REGULAR`, `BOLD`, `ITALIC`, `SUP`, `SUB` (can be OR'd) |
| `drawCenteredText(fontId, y, text, black=true, style=REGULAR)` | Horizontally centered text |
| `drawTextRotated90CW(fontId, x, y, text, black=true, style=REGULAR)` | 90° clockwise rotated text (used for side button labels) |
| `getTextWidth(fontId, text, style=REGULAR)` | Pixel width of a text string |
| `getTextAdvanceX(fontId, text, style)` | Total advance in pixels (excludes trailing whitespace kerning) |
| `getTextHeight(fontId)` | Font bounding box height |
| `getFontAscenderSize(fontId)` | Ascender height above baseline |
| `getLineHeight(fontId)` | Recommended line-to-line spacing |
| `getSpaceWidth(fontId, style=REGULAR)` | Width of a space character |
| `getSpaceAdvance(fontId, leftCp, rightCp, style)` | Inter-word advance including kerning on both sides of the space |
| `getKerning(fontId, leftCp, rightCp, style)` | Kerning adjustment between two codepoints |
| `truncatedText(fontId, text, maxWidth, style=REGULAR)` | Returns text truncated with ellipsis (U+2026) to fit `maxWidth` |
| `wrappedText(fontId, text, maxWidth, maxLines, style=REGULAR)` | Word-wraps text into at most `maxLines` lines, truncating with ellipsis on overflow |

**Text rendering details:**
- Supports **combining marks** (accents etc.) via `utf8IsCombiningMark()`
- Supports **ligatures** via font-provided `applyLigatures()`
- **SUP/SUB styles** are rendered at 50% scale with halved advance
- **2-bit grayscale fonts** produce anti-aliased text when `renderMode` is
  `GRAYSCALE_LSB` or `GRAYSCALE_MSB`
- **textDarkness** controls how AA bucket values are mapped
- **Scan mode**: when `FontCacheManager::isScanning()` is true, `drawText()`
  records font usage instead of drawing (two-pass prewarm strategy)

### Screen Operations

| Method | Description |
|---|---|
| `clearScreen(color=0xFF)` | Fill entire display. `0xFF` = white, `0x00` = black. Respects dark mode |
| `invertScreen()` | Bitwise-invert every byte in the framebuffer |
| `displayBuffer(refreshMode=FAST_REFRESH)` | Flush framebuffer to panel. Respects `nextRefreshOverride` |
| `getScreenWidth()` | Logical screen width based on current orientation |
| `getScreenHeight()` | Logical screen height based on current orientation |

**Refresh modes** (HalDisplay):
- `FULL_REFRESH` — complete panel refresh, slowest but cleanest
- `HALF_REFRESH` — partial refresh
- `FAST_REFRESH` — quick update, may leave ghosting

### Grayscale Functions

| Method | Description |
|---|---|
| `copyGrayscaleLsbBuffers()` | Copy LSb plane from framebuffer to grayscale buffer |
| `copyGrayscaleMsbBuffers()` | Copy MSb plane from framebuffer to grayscale buffer |
| `displayGrayBuffer()` | Trigger grayscale panel refresh |
| `storeBwBuffer()` | Save current BW framebuffer to chunked backup (allows restoring after grayscale draws) |
| `restoreBwBuffer()` | Restore framebuffer from chunked backup |
| `cleanupGrayscaleWithFrameBuffer()` | Cleanup grayscale planes using the BW buffer as reference |

The grayscale pipeline: render text/UI into BW buffer → copy LSb/MSb planes to
grayscale buffers → `displayGrayBuffer()` pushes all planes to the controller.

### Framebuffer Access

| Method | Description |
|---|---|
| `getFrameBuffer()` | Raw framebuffer pointer (uint8_t*) |
| `getBufferSize()` | Framebuffer size in bytes |
| `getDisplayWidth()` | Physical panel width in pixels |
| `getDisplayHeight()` | Physical panel height in pixels |
| `getDisplayWidthBytes()` | Bytes per row (width / 8) |
| `getRegionByteSize(logicalX, logicalY, logicalW, logicalH)` | Byte count for a logical region |
| `copyRegionToBuffer(logicalX, logicalY, logicalW, logicalH, buf, bufSize)` | Read framebuffer region |
| `copyBufferToRegion(logicalX, logicalY, logicalW, logicalH, buf, bufSize)` | Write region to framebuffer |
| `getOrientedViewableTRBL(&top, &right, &bottom, &left)` | Viewable margins for current orientation |

---

## Color Enum & Dithering

```cpp
enum Color : uint8_t {
  Clear       = 0x00,  // Transparent (no-op)
  White       = 0x01,  // Always white pixel
  LightGray   = 0x05,  // White with checkerboard (x%2==0 && y%2==0)
  MediumGray  = 0x07,  // Bayer 4×4 ordered dithering
  DarkGray    = 0x0A,  // (x+y)%2 checkerboard
  ExtraDarkGray= 0x0D, // Bayer 4×4 ordered dithering
  Black       = 0x10   // Always black
};
```

On the 1-bit e-ink panel, gray tones are achieved through **ordered dithering**
(Bayer 4×4 matrix for MediumGray and ExtraDarkGray) or simple checkerboard
patterns (LightGray, DarkGray). This is a purely spatial dither — no temporal
modulation.

The `Bitmap` class and `BitmapHelpers` also implement **error-diffusion
dithering** (Atkinson and Floyd-Steinberg) for BMP image rendering.

---

## Orientation System

```cpp
enum Orientation {
  Portrait,                  // 480x800 logical → panel (90° CW)
  LandscapeClockwise,        // 800x480 logical → panel (180° rotated)
  PortraitInverted,          // 480x800 logical → panel (90° CCW)
  LandscapeCounterClockwise  // 800x480 logical → panel (native orientation)
};
```

The physical panel is 800×480 (landscape). In `Portrait` mode (the default),
callers work in 480×800 logical coordinates. `rotateCoordinates()` in
`GfxRenderer.cpp` maps every pixel through a compile-time-inlinable transform.

---

## BaseTheme — UI Component Rendering

Declared in `src/components/themes/BaseTheme.h`. All methods take a
`GfxRenderer&` and use the primitives above.

### Abstract Theme Interface

| Method | Description |
|---|---|
| `drawProgressBar(renderer, rect, current, total)` | Horizontal progress bar |
| `drawBatteryLeft(renderer, rect, showPct)` | Battery icon left-aligned (reader status bar) |
| `drawBatteryRight(renderer, rect, showPct)` | Battery icon right-aligned (UI headers) |
| `drawButtonHints(renderer, btn1..btn4)` | Bottom button labels (4 buttons) |
| `drawSideButtonHints(renderer, topBtn, bottomBtn)` | Side button labels (rotated 90° CW) |
| `drawList(renderer, rect, itemCount, selectedIdx, callbacks...)` | Scrollable list with icons, titles, subtitles, values |
| `drawHeader(renderer, rect, title, subtitle)` | Page header with title and optional subtitle |
| `drawSubHeader(renderer, rect, label, rightLabel)` | Sub-header row with left/right labels |
| `drawTabBar(renderer, rect, tabs, selected)` | Tab bar with compact/wide variants |
| `drawRecentBookCover(renderer, rect, books, idx, ...)` | Home screen book cover grid |
| `drawCarouselBorder(renderer, rect, inCarouselRow)` | Carousel highlight border (Lyra override) |
| `drawButtonMenu(renderer, rect, count, idx, callbacks...)` | Menu with icons and subtitles |
| `drawPopup(renderer, message)` | Modal popup with wrapped text, returns layout `Rect` |
| `fillPopupProgress(renderer, layout, progress)` | Fills popup background as a progress bar |
| `drawStatusBar(renderer, progress, page, total, title, ...)` | Reader bottom status bar with page info and progress |
| `drawHelpText(renderer, rect, label)` | Help/instruction text |
| `drawTextField(renderer, rect, textWidth, cursorMode, ...)` | Text input field with optional cursor |
| `drawKeyboardKey(renderer, rect, label, selected, ...)` | On-screen keyboard key |
| `drawBatteryOutline(renderer, x, y, w, h)` | Static helper: battery outline shape |
| `drawBatteryLightningBolt(renderer, boltX, boltY)` | Static helper: charging lightning bolt |

### Theme Variants

| Class | File | Description |
|---|---|---|
| `BaseTheme` | `src/components/themes/BaseTheme.cpp` | Classic theme (default) |
| `LyraTheme` | `src/components/themes/lyra/LyraTheme.cpp` | Modern theme with dithered fills, rounded rects |
| `LyraCarouselTheme` | `src/components/themes/lyra/LyraCarouselTheme.cpp` | Carousel home screen with large centered cover |
| `LyraCustomTheme` | `src/components/themes/lyra/LyraCustomTheme.cpp` | Custom home screen with 3-up covers |

Access the current theme via `GUI.theme` (a `UITheme&` singleton).

### Rect & ThemeMetrics

```cpp
struct Rect { int x; int y; int width; int height; };
```

`ThemeMetrics` (defined in `BaseTheme.h`) holds layout constants (battery size,
padding, row heights, keyboard geometry, etc.). Each theme can provide its own
metrics.

---

## HalDisplay & EInkDisplay — Low-Level Hardware

| Class | File | Role |
|---|---|---|
| `HalDisplay` | `lib/hal/HalDisplay.h` | Hardware abstraction. Globally available as `display` |
| `EInkDisplay` | `open-x4-sdk/libs/display/EInkDisplay/` | SPI driver for SSD1677 (X4) / UC81xx (X3) |

`HalDisplay` mirrors a subset of GfxRenderer methods but operates in **physical
panel coordinates**:

| Method | Description |
|---|---|
| `clearScreen(color)` | Fill panel pixels |
| `drawImage(data, x, y, w, h, progmem)` | Opaque 1-bit image |
| `drawImageTransparent(data, x, y, w, h, progmem)` | Transparent 1-bit image |
| `displayBuffer(mode, turnOff)` | Trigger physical refresh |
| `copyGrayscaleLsbBuffers(buf)` / `copyGrayscaleMsbBuffers(buf)` | Load grayscale planes |
| `copyGrayscaleBuffers(lsb, msb)` | Load both grayscale planes at once |
| `cleanupGrayscaleBuffers(bwBuf)` | Cleanup using BW reference |
| `displayGrayBuffer(turnOff)` | Trigger grayscale refresh |

---

## Conventions & Constraints

- **No heap allocation** in render loops, input loops, or parser hot paths.
- `fillPolygon()` and `drawBitmap()` allocate temporary buffers on heap — keep
  vertex counts and image dimensions reasonable (~380 KB RAM, no PSRAM).
- All drawing methods are `const` — the framebuffer is mutable state.
- Prefer `drawPixel()` over `drawPixelRaw()` in application code to get dark
  mode support.
- Use `requestNextFullRefresh()` before drawing a static UI to avoid ghosting
  on the next transition.
- For grayscale anti-aliased text, set `renderMode = GRAYSCALE_LSB` before
  calling `drawText()`, then `copyGrayscaleLsbBuffers()` and
  `displayGrayBuffer()`.

---

## Related Files

| File | Purpose |
|---|---|
| `lib/GfxRenderer/GfxRenderer.h` | Core renderer declaration |
| `lib/GfxRenderer/GfxRenderer.cpp` | Core renderer implementation |
| `lib/GfxRenderer/Bitmap.h` / `.cpp` | BMP file reader with dithering |
| `lib/GfxRenderer/BitmapHelpers.h` / `.cpp` | Dithering algorithms, gamma, BMP header creation |
| `lib/GfxRenderer/FontCacheManager.h` / `.cpp` | Glyph cache and two-pass scan prewarming |
| `lib/hal/HalDisplay.h` / `.cpp` | Hardware abstraction layer |
| `src/components/themes/BaseTheme.h` / `.cpp` | UI theme base class |
| `src/components/themes/lyra/` | Lyra theme variants |
| `src/util/QrUtils.cpp` | QR code renderer |
| `src/util/ScreenshotUtil.cpp` | Screenshot capture |
| `src/util/PngSleepRenderer.cpp` | PNG sleep screen rendering |
