# Model 3-Vulkan — Additional Terms under GNU GPL v3, Section 7

Copyright (C) 2026 **REXABYTE SOFTWARE INC.** (Igloo-Garage / Rex)  
REXABYTE SOFTWARE INC., British Columbia, Canada  
<https://github.com/Igloo-Garage/Model-3-Vulkan>

Model 3-Vulkan is free software distributed under the GNU General Public License,
version 3 or later.  See [`Docs/LICENSE.txt`](Docs/LICENSE.txt).

This file states the additional terms permitted by **GNU GPL v3 Section 7** that
apply to the portions of Model 3-Vulkan authored by Igloo-Garage /
REXABYTE SOFTWARE INC. ("the Author's Material").  They are additional terms, not further restrictions within the
meaning of Section 10: they do not limit the GPL rights to run, modify,
distribute, or sell the Program.  What they require is that the author's
attribution and the origin of the work are not erased or misrepresented.

## 1. Scope

These terms apply to the Author's Material, which includes the original work in:

- `Src/Graphics/Vulkan3D/**` — the Vulkan 1.3 renderer, post-processing chain,
  UI blur, motion interpolation, HD texture pipeline, DDS/BCn loader;
- `Src/Graphics/Vulkan3D/Shaders/**` — the original GLSL/SPIR-V shaders;
- `Src/OSD/SDL/Gui.cpp` — the settings window, game list, cover preview,
  save-state manager and interface theme;
- `Src/OSD/SDL/Overlay.{h,cpp}` — the in-game Tab panel;
- `Src/OSD/SDL/StatsOsd.{h,cpp}` — the statistics overlay;
- `Src/OSD/SDL/Localization.{h,cpp}` — the interface translation table;
- `Src/OSD/SDL/FontProbe.{h,cpp}` — the font safety probe;
- `Src/OSD/Windows/SplashScreen.{h,cpp}` — the startup artwork and notice;
- `Src/OSD/SDL3/**` — the SDL3/Vulkan startup and host-core bridge;
- `HostCore/**` — the Native AOT save-container module;
- every other file whose header states that these additional terms apply.

They do **not** apply to upstream Supermodel code, to third-party components, or
to game artwork whose rights remain with its owners.

## 2. Attribution must be preserved (Section 7(b))

The following attribution must be preserved in the Author's Material and must be
displayed in the startup notice and in the About / Statistics panel of any work
that contains the Author's Material:

    Model 3-Vulkan by Igloo-Garage (Rex)
    https://github.com/Igloo-Garage/Model-3-Vulkan

The display must be convenient and prominently visible to a user.  Removing,
hiding, obscuring, or commenting out the code that displays it is a breach of
this License and terminates the rights granted by GPL v3 Section 8.

This term does not prescribe a particular colour, font, size, or number of
seconds.  It requires that the attribution remains present and readable in the
startup notice, the settings interface, the in-game panel, and the statistics
panel of any work that contains the Author's Material.

## 3. Modified versions must be marked (Section 7(c))

Modified versions must be marked in a reasonable way as different from the
original Model 3-Vulkan, and must not be presented as the original work or as an
official release of Igloo-Garage.  A modified build must not claim to be
"official", "original", or endorsed by the author.

## 4. Publicity (Section 7(d))

The names "Igloo-Garage" and "Model 3-Vulkan" may not be used to promote derived
works without prior written permission.  Truthful statements such as "based on
Model 3-Vulkan" are permitted, provided they do not imply endorsement.

## 5. Trademarks (Section 7(e))

No rights under trademark law are granted.  "Model 3-Vulkan™" and
"Igloo-Garage™", the associated logos, the icon, the startup
artwork and the cabinet bezel are trademarks and brand assets of REXABYTE
SOFTWARE INC.  The ™ symbol denotes an unregistered / common-law mark used by the
Company; it does not mean the mark is registered in a trademark office.

See [`TRADEMARK.md`](TRADEMARK.md).  A fork must use a different name and icon,
and must state that it is modified and not affiliated with the Company.

## 6. Commercial distribution

GNU GPL v3 permits conveying the Program commercially.  These additional terms
do not forbid that, and cannot: adding a noncommercial restriction to a GPL work
would be a further restriction under Section 10 and would be void.  What remains
in force for a commercial conveyance is the attribution requirement above, the
requirement to mark modified versions, and the trademark policy.  A product that
removes the author's attribution, or that uses the author's marks as its own
branding, is not licensed to do so.  A commercial distributor that wants to use
the marks needs a separate written trademark licence from REXABYTE SOFTWARE INC.;
the GPLv3 does not grant one.

## 7. How to comply

A distributor that wishes to stay within these terms must:

1. keep the copyright and licence notices in the source files;
2. keep the startup notice, the settings About panel, the in-game panel footer,
   and the statistics panel attribution intact;
3. state clearly that a modified build is modified and unofficial;
4. use its own product name and icon, and remove the "Model 3-Vulkan" name and
   the original logos from the product, installer and store listing;
5. provide the Corresponding Source as required by GPL v3 Section 6.

If you believe a derivative has removed the attribution, see the enforcement
notes in `TRADEMARK.md`.  The author's preference is always a correction rather
than a takedown.
