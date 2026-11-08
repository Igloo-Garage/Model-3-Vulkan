# Reddit post draft (showcase, no release yet)

For r/emulation, r/SegaModel3, r/EmulationOnPC, r/arcade, or a Model 3 thread.

Framed as a **work-in-progress showcase**, because there is nothing to download
yet. That is a deliberate choice, not a limitation - see the first note below.
The post's job is therefore to show the work, get people's interest, and collect
bug reports from games you haven't tried.

---

## Good news on the licence question

Not publishing the source is fine **as long as you don't hand out the binary**.
GPL v3 only imposes its source-availability obligation when you *convey* the
object code - that is, when you give someone the compiled program. Using the
software, modifying it, and running it yourself are all explicitly permitted,
and so is showing it to people.

Posting a video, screenshots and a write-up conveys no object code, so nothing
is triggered. If you later upload the build somewhere, then the source has to
go with it - that is the point at which a public repo becomes necessary.

So: showcase now, ship later. Just be clear in the post that it isn't out yet,
so nobody goes hunting for a download that does not exist.

---

## Suggested title

Signal that it is a preview, so nobody expects a link:

```
Supermodel-VK: I rewrote Supermodel's renderer for Vulkan and added a settings GUI (work in progress)
```

Alternatives:

```
Work in progress: a Vulkan backend and a configurable CRT filter for Model 3 emulation
```

```
Daytona USA 2 in a Model 3 fork with a new Vulkan renderer and a real settings window
```

---

## Body

```
**Short version:** a personal fork of [Supermodel](https://github.com/trzy/Supermodel)
0.3a. Same emulation core, new Vulkan rendering backend, and a proper settings
window instead of a wall of command-line flags. Plus an in-game panel so you can
tune a CRT filter while the game is running.

**Nothing to download yet** - this is still work in progress and I haven't
published a build. Mostly I want to show where it's got to and find out what
breaks on games I haven't tried.

Not official and not affiliated with the Supermodel team; all credit for the
emulation core goes to them.

**Why bother when Supermodel already works?** The OpenGL backend had a few
Model 3 quirks I wanted to fix, and I wanted to tune a CRT filter without
relaunching the emulator forty times.

### What's in it

**Vulkan backend (new)**

- Real3D packs an entire mip pyramid into one texture page at fixed offsets. The
  original backend didn't account for that, so distant scenery and backgrounds
  came out black.
- Transparent texels are now discarded rather than polluting the depth buffer.
- Coplanar decals (sponsor logos, numbers, light details on car bodies) draw in
  the right pass instead of fighting the paint underneath.
- Fog matches OpenGL, so the horizon takes the right colour.
- The geometry-culling white flash no longer paints the letterbox bars white.

**Display**

- Hardware MSAA 2x/4x. It only affects polygon coverage, so textures and decals
  stay sharp - that's the point over a post-process AA.
- FXAA for weaker GPUs.
- Integer scaling, for point-perfect 2D.
- Exclusive fullscreen with G-SYNC/VRR, or a fixed 57.524 Hz to match the
  original arcade rate.
- Optional cabinet backdrop behind the letterbox bars.

**CRT filter**

- Scanlines integrated analytically over each output pixel, which removes the
  moire you otherwise get whenever the scanline pitch isn't a whole number of
  pixels - at 4K it never is.
- Three mask types (aperture grille, slot mask, dot mask) at adjustable size.
  The slot mask is what Sega's own cabinets used.
- Multi-scale glow, brightness compensation, highlight soft-compression.
- Presets, and a live readout that warns you when the scanline and mask pitches
  would beat against each other and produce colour banding.

**Interface**

- Persistent settings window; closing a game returns to it instead of exiting.
- English and Chinese, switchable live. A description on every setting.
- **Press Tab in-game** for a settings panel over the picture. Everything takes
  effect immediately, so you tune the filter by watching the image.
- No console window on startup.

**Other**

- The sound board runs on its own thread, so audio doesn't stutter when you drag
  the window or click another app.
- XInput by default, which suits standard Xbox-layout pads.
- Coin/Start are no longer bound to pressing the analogue sticks.

### Honest limitations

- The CRT filter is my own, not a CRT-Royale port: no screen curvature, no
  chromatic aberration, no phosphor persistence.
- No HDR output.
- The in-game panel and the CRT filter need the Vulkan backend.
- MSAA and integer scaling still need a restart, so they're deliberately left
  out of the in-game panel.
- The backdrop is stretched to fill the window rather than aspect-preserved.
- Tested mainly on Daytona USA 2 and a handful of others. I'd expect problems on
  games I haven't tried.

### ROMs

Not included, obviously - I can't help with those either.

### Questions

- If you'd want this, say so, and which game you'd try first.
- If something looks wrong in the video, tell me - rendering bugs are what I'm
  chasing.
- Anything Model 3 knowledgeable people think I've got wrong is very welcome;
  that's the main reason for posting before it's finished.
```

---

## Before you post

**1. Be explicit that it isn't released.** The body says so, twice, because
otherwise the top comment will be "link?" and the thread turns into that. It is
also what keeps the licensing situation simple.

**2. No ROM links.** Not in the post, not in the comments. The emulation subs
remove posts over that and it puts the project at risk. The draft already says
ROMs aren't included.

**3. Pick the subreddit deliberately.** r/emulation is general and large, so the
technical detail has to carry it; r/SegaModel3 is small and specific, where Model
3 detail matters more and less framing is needed. The draft is tuned for the
general case.

**4. Lead with a clip.** A post describing a renderer with nothing to look at
gets far less traction. A short capture of the CRT filter, ideally alongside the
OpenGL renderer for comparison, is what people will actually judge.

**5. Ask for the specific feedback you want.** "Thoughts?" gets nothing. "Which
games should I test? What looks wrong in the video?" gets answers.

---

## Optional: a shorter comment-ready version

For replying in someone else's thread, or for a comment that has to survive
being skimmed:

```
It's a fork of Supermodel 0.3a with a from-scratch Vulkan backend and a settings
GUI. The Vulkan side fixes the mip-packed texture handling (distant scenery
rendering black), decal/transparency issues, and the culling white-flash painting
the letterbox black bars. GUI side adds MSAA, FXAA, integer scaling, exclusive
fullscreen with G-SYNC or a fixed 57.5 Hz, a configurable CRT filter, an optional
cabinet backdrop, and a Tab overlay for adjusting the filter while the game runs.
Windows only, no runtime needed, no ROMs included. Emulation core is the
Supermodel team's, GPL v3.

Still work in progress, no build published yet - mostly putting it out there to
see what breaks.
```
